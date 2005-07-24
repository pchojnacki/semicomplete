/*
 * New, prettier, psm?
 */

#include <sys/cdefs.h>

#include <sys/types.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/rman.h> /* for RF_ macros */
#include <sys/uio.h> /* for uiomove, etc */
#include <sys/syslog.h> /* for log(9) */
#include <sys/mouse.h> /* for mousemode_t */
#include <machine/bus.h>
#include <machine/resource.h>
#include <dev/atkbdc/atkbdcreg.h> /* for KBDC_RID_AUX */

#define NEWPSM_MKMINOR(unit,block) (((unit) << 1) | ((block) ? 0 : 1))
#define NEWPSM_DRIVER_NAME "newpsm"

#define PSMCPNP_DRIVER_NAME "psmcpnp"

#define endprobe(v)  do { \
	kbdc_lock(sc->kbdc, FALSE); \
	return (v); \
} while (0)


/* Debugging sysctls */
SYSCTL_NODE(_debug, OID_AUTO, newpsm, CTLFLAG_RD, 0, "newpsm ps/2 mouse");

SYSCTL_INT(_debug_psm, OID_AUTO, loglevel, CTLFLAG_RW, &verbose, 0, "PS/2 Debugging Level");

void newpsm_identify(driver_t *, device_t);
int newpsm_probe(device_t);
int newpsm_attach(device_t);
int newpsm_detach(device_t);
int newpsm_resume(device_t);
int newpsm_shutdown(device_t);

d_open_t newpsm_open;
d_close_t newpsm_close;
d_read_t newpsm_read;
d_ioctl_t newpsm_ioctl;
d_poll_t newpsm_poll;

/* Helper Functions */
int restore_controller(KBDC kbdc, int command_byte);

/* NEWPSM_SOFTC */
/* XXX: Document these */
struct newpsm_softc {
	int unit;                   /* newpsmX device number */
	struct cdev *dev;           /* Our friend, the device */

	struct resource *intr;      /* The interrupt resource */
	KBDC kbdc;                  /* Keyboard device doohickey */
	int config;
	int flags;
	void *ih;
	int state;
};

static device_method_t newpsm_methods[] = {
	DEVMETHOD(device_identify, newpsm_identify),
	DEVMETHOD(device_probe,    newpsm_probe),
	DEVMETHOD(device_attach,   newpsm_attach),
	DEVMETHOD(device_detach,   newpsm_detach),
	DEVMETHOD(device_resume,   newpsm_resume),
	DEVMETHOD(device_shutdown, newpsm_shutdown),

	{ 0, 0 }
};

static devclass_t newpsm_devclass;

static driver_t newpsm_driver = {
	NEWPSM_DRIVER_NAME,
	newpsm_methods,
	sizeof(struct newpsm_softc),
};

static struct cdevsw newpsm_cdevsw = {
	.d_version =   D_VERSION,
	//.d_flags =  D_NEEDGIANT,
	.d_open =   newpsm_open,
	.d_close =  newpsm_close,
	.d_read =   newpsm_read,
	.d_ioctl =  newpsm_ioctl,
	.d_poll =   newpsm_poll,
	.d_name =   NEWPSM_DRIVER_NAME,
};

void
newpsm_identify(driver_t *driver, device_t parent)
{
	device_t psm;
	device_t psmc;
	u_long irq;
	int unit;
  
	unit = device_get_unit(parent);

	uprintf("newpsm_identify\n");

	/* Look for a mouse on the ps/2 aux port */

	psm = BUS_ADD_CHILD(parent, KBDC_RID_AUX, "newpsm", -1);
	if (psm == NULL)
		return;

	irq = bus_get_resource_start(psm, SYS_RES_IRQ, KBDC_RID_AUX);
	if (irq > 0)  {
		uprintf("success!\n");
		return;
	} 

	/* No mouse found yet, maybe psmcpnp found it earlier */

	psmc = device_find_child(device_get_parent(parent), PSMCPNP_DRIVER_NAME, unit);

	if (psmc == NULL)
		return;

	uprintf("PSMC probed\n");

	irq = bus_get_resource_start(psmc, SYS_RES_IRQ, 0);
	if (irq <= 0)
		return;

	device_printf(parent, "IRQ allocated\n");

	bus_set_resource(psm, SYS_RES_IRQ, KBDC_RID_AUX, irq, 1);
}

int
newpsm_probe(device_t dev)
{
	int unit;
	struct newpsm_softc *sc;
	int rid;
	int mask;
	int control_byte;
	int i; /* Used to store test_aux_port()'s return value */

	uprintf("newpsm_probe\n");
	unit = device_get_unit(dev);
	sc = device_get_softc(dev);

	rid = KBDC_RID_AUX;
	sc->intr = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_SHAREABLE | RF_ACTIVE);

	if (sc->intr == NULL) {
		return ENXIO;
	}

	bus_release_resource(dev, SYS_RES_IRQ, rid, sc->intr);

	sc->unit = unit;
	sc->kbdc == atkbdc_open(device_get_unit(device_get_parent(dev)));

	device_set_desc(dev, "PS/2 Mouse [newpsm]");

	/* Now that we have a device to talk to , let's make sure it's a mouse? */

	if (!kbdc_lock(sc->kbdc, TRUE)) {
		uprintf("psm%d: unable to lock the controller.\n", unit);
		return ENXIO;
	}

	/* wipe out both keyboard and aux buffers */
	empty_both_buffers(sc->kbdc, 10);

	mask = kbdc_get_device_mask(sc->kbdc) & ~KBD_AUX_CONTROL_BITS;
	command_byte = get_controller_command_byte(sc->kbdc);

	if (verbose)
		printf("psm%d: current command byte %04x\n", unit, command_byte);

	if (command_byte == -1) {
		printf("psm%d: unable to get current command byte value.\n", unit);
		endprobe(ENXIO);
	}

	/*
	 * disable the keyboard port while probing the aux port.
	 * Also, enable the aux port so we can use it during the probe
	 */
	if (!set_controller_command_byte(sc->kbdc, 
		 KBD_KBD_CONTROL_BITS | KBD_AUX_CONTROL_BITS,
		 KBD_DISABLE_KBD_PORT | KBD_DISABLE_KBD_INT
		   | KBD_ENABLE_AUX_PORT | KBD_DISABLE_AUX_INT)) {
		/* Controller error if we end up in this block. */

		restore_controller(sc->kbdc, command_byte);
		printf("psm%d: unable to send the command byte.\n", unit);
		endprobe(ENXIO);
	}

	/* Enable the aux port */
	write_controller_command(sc->kbdc, KBDC_ENABLE_AUX_PORT);

	/*
	 * NOTE: `test_aux_port()' is designed to return with zero if the aux
	 * port exists and is functioning. However, some controllers appears
	 * to respond with zero even when the aux port doesn't exist. (It may
	 * be that this is only the case when the controller DOES have the aux
	 * port but the port is not wired on the motherboard.) The keyboard
	 * controllers without the port, such as the original AT, are
	 * supporsed to return with an error code or simply time out. In any
	 * case, we have to continue probing the port even when the controller
	 * passes this test.
	 *
	 * XXX: some controllers erroneously return the error code 1, 2 or 3
	 * when it has the perfectly functional aux port. We have to ignore
	 * this error code. Even if the controller HAS error with the aux
	 * port, it will be detected later...
	 * XXX: another incompatible controller returns PSM_ACK (0xfa)...
	 */

	switch((i = test_aux_port(sc->kbdc))) {
		case 1:
		case 2:
		case 3:
		case PSM_ACK:
			if (verbose)
				printf("psm%d: strange result for test_aux_port (%d).\n", unit, i);
			break;
		case 0:       /* No Error */
			break;
		case -1:      /* Timeout */
		default:      /* Error */
			recover_from_error(sc->kbdc);
			/* XXX: Define PSM_CONFIG_IGNPORTERROR */
			/*
			 if (sc->config & PSM_CONFIG_IGNPORTERROR);
			 break;
			 */
			restore_controller(sc->kbdc, command_byte);
			if (verbose)
				printf("psm%d: the aux port is not functioning (%d).\n", unit, i);
			endprobe(ENXIO);
	}

	kbdc_lock(sc->kbdc, FALSE);
	return 0;
}

int
newpsm_attach(device_t dev)
{
	int unit = device_get_unit(dev);
	struct newpsm_softc *sc = device_get_softc(dev);
	sc->dev = make_dev(&newpsm_cdevsw, NEWPSM_MKMINOR(unit, FALSE), 0, 0, 0666, "newpsm%d", unit);
	return 0;
}

int
newpsm_detach(device_t dev)
{
	struct newpsm_softc *sc = device_get_softc(dev);

	uprintf("newpsm_detach\n");
	destroy_dev(sc->dev);

	return 0;
}

int
newpsm_resume(device_t dev)
{
	uprintf("newpsm_resume\n");

	return 0;
}

int
newpsm_shutdown(device_t dev)
{
	uprintf("newpsm_shutdown\n");
	return 0;
}

int newpsm_open(struct cdev *dev, int flag, int fmt, struct thread *td) {
	return 0;
}

int newpsm_close(struct cdev *dev, int flag, int fmt, struct thread *td) {

	return 0;
}

int newpsm_read(struct cdev *dev, struct uio *uio, int flag) {
	int err;

	err = uiomove("Hello\n", 6, uio);
	return err;
}

int newpsm_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag, struct thread *td) {

	return 0;
}

int newpsm_poll(struct cdev *dev, int events, struct thread *td) {

	return 0;
}

DRIVER_MODULE(newpsm, atkbdc, newpsm_driver, newpsm_devclass, 0, 0);


/* Helper Functions */

/* 
 * Try and restor the controller to the state it was at before we
 * started poking it 
 */
int
restore_controller(KBDC kbdc, int command_byte)
{
	empty_both_buffers(kbdc, 10);

	if (!set_controller_command_byte(kbdc, 0xff, command_byte)) {
		log(LOG_ERR, "psm: failed to restore the keyboard controller command byte.\n");
		empty_both_buffers(kbdc, 10);
		return FALSE;
	} else {
		empty_both_buffers(kbdc, 10);
		return TRUE;
	}
}

