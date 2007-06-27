/*
 * Visual user-directed binary search for something to point your mouse at.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/XTest.h>

#include "xdotool/xdo.h"

/* These come from xdo.o */
extern keysymcharmap_t keysymcharmap[];
extern char *symbol_map[];

Display *dpy;
Window root;
XWindowAttributes rootattr;
Window zone;
xdo_t *xdo;
int appstate = 0;

#define STATE_ACTIVE 0x0001 
#define STATE_DRAGGING 0x0002

struct wininfo {
  int x;
  int y;
  int w;
  int h;
} wininfo;

void cmd_cut_up(char *args);
void cmd_cut_down(char *args);
void cmd_cut_left(char *args);
void cmd_cut_right(char *args);
void cmd_move_up(char *args);
void cmd_move_down(char *args);
void cmd_move_left(char *args);
void cmd_move_right(char *args);
void cmd_warp(char *args);
void cmd_click(char *args);
void cmd_doubleclick(char *args);
void cmd_drag(char *args);
void cmd_start(char *args);
void cmd_end(char *args);

void update();
void handle_keypress(XKeyEvent *e);
void handle_commands(char *commands);
void parse_config();
void parse_config_line(char *line);

struct dispatch {
  char *command;
  void (*func)(char *args);
} dispatch[] = {
  "cut-up", cmd_cut_up,
  "cut-down", cmd_cut_down,
  "cut-left", cmd_cut_left,
  "cut-right", cmd_cut_right,
  "move-up", cmd_move_up,
  "move-down", cmd_move_down,
  "move-left", cmd_move_left,
  "move-right", cmd_move_right,

  "warp", cmd_warp,
  "click", cmd_click,     
  "doubleclick", cmd_doubleclick,
  "drag", cmd_drag,

  "start", cmd_start,
  "end", cmd_end, 
  NULL, NULL,
};

struct keybinding {
  char *commands;
  int keycode;
  int mods;
} *keybindings = NULL;

int nkeybindings = 0;
int keybindings_size = 10;

int parse_keycode(char *keyseq) {
  char *tokctx;
  char *strptr;
  char *tok;
  char *last_tok;

  strptr = keyseq;
  //printf("finding keycode for %s\n", keyseq);
  while ((tok = strtok_r(strptr, "+", &tokctx)) != NULL) {
    last_tok = tok;
    strptr = NULL;
  }

  return XKeysymToKeycode(dpy, XStringToKeysym(last_tok));
}

int parse_mods(char *keyseq) {
  char *tokctx;
  char *strptr;
  char *tok;
  char *last_tok;
  char **mods  = NULL;
  int modmask = 0;
  int nmods = 0;
  int mod_size = 10;

  mods = malloc(mod_size * sizeof(char *));

  //printf("finding mods for %s\n", keyseq);

  strptr = keyseq;
  while ((tok = strtok_r(strptr, "+", &tokctx)) != NULL) {
    strptr = NULL;
    //printf("mod: %s\n", tok);
    mods[nmods] = tok;
    nmods++;
    if (nmods == mod_size) {
      mod_size *= 2;
      mods = realloc(mods, mod_size * sizeof(char *));
    }
  }

  int i;

  /* Use all but the last token as modifiers */
  for (i = 0; i < nmods; i++) {
    char *mod = mods[i];
    KeySym keysym = 0;

    printf("mod: keysym for %s = %d\n", mod, keysym);
    // from xdo_util: Map "shift" -> "Shift_L", etc.
    for (i = 0; symbol_map[i] != NULL; i+=2)
      if (!strcasecmp(mod, symbol_map[i]))
        mod = symbol_map[i + 1];

    keysym = XStringToKeysym(mod);
    if ((keysym == XK_Shift_L) || (keysym == XK_Shift_R))
      modmask |= ShiftMask;
    if ((keysym == XK_Control_L) || (keysym == XK_Control_R))
      modmask |= ControlMask;
    if ((keysym == XK_Alt_L) || (keysym == XK_Alt_R))
      modmask |= Mod1Mask;

    /* XXX: What other masks do we want? 
     * Meta, Super, and Hyper? I have no idea what ModNMask these are. */
  }

  return modmask;
}

void addbinding(int keycode, int mods, char *commands) {
  if (nkeybindings == keybindings_size) {
    keybindings_size *= 2;
    keybindings = realloc(keybindings, keybindings_size * sizeof(struct keybinding));
  }

  keybindings[nkeybindings].commands = strdup(commands);
  keybindings[nkeybindings].keycode = keycode;
  keybindings[nkeybindings].mods = mods;
  nkeybindings++;

  /* We don't need to "bind" a key here unless it's for 'start' */
  if (!strcmp(commands, "start")) {
    XGrabKey(dpy, keycode, mods, root, False, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, keycode, mods | LockMask, root, False, GrabModeAsync, GrabModeAsync);
  }
}

void parse_config() {
  char *homedir;
  char *default_config[] = {
    "ctrl+semicolon start",
    "Escape end",
    "h cut-left",
    "j cut-down",
    "k cut-up",
    "l cut-right",
    "shift+h move-left",
    "shift+j move-down",
    "shift+k move-up",
    "shift+l move-right",
    "space warp,click 1,end",
    "semicolon warp,end",
    "w warp",
    "e end",
    "1 click 1",
    "2 click 2",
    "3 click 3",
    NULL,
  };
  int i;

  keybindings = malloc(keybindings_size * sizeof(struct keybinding));

  homedir = getenv("HOME");

  if (homedir != NULL) {
    char *rcfile = NULL;
    FILE *fp = NULL;
#define LINEBUF_SIZE 512
    char line[LINEBUF_SIZE];
    asprintf(&rcfile, "%s/.keynavrc", homedir);
    fp = fopen(rcfile, "r");
    if (fp == NULL) {
      perror("Failed trying to read ~/.keynavrc");
      exit(1);
    }

    while (fgets(line, LINEBUF_SIZE, fp) != NULL) {
      /* Kill the newline */
      *(line + strlen(line) - 1) = '\0';
      parse_config_line(line);
    }
    free(rcfile);
  } else {
    fprintf(stderr, "No ~/.keynavrc found. Using defaults.");
    for (i = 0; default_config[i]; i++) {
      char *tmp;
      tmp = strdup(default_config[i]);
      parse_config_line(tmp);
      free(tmp);
    }
  }
}

void parse_config_line(char *line) {
  /* syntax:
   * keysequence cmd1,cmd2,cmd3
   *
   * ex: 
   * ctrl+semicolon start
   * space warp
   * semicolon warp,click
   */

  char *tokctx;
  char *keyseq;
  char *commands;
  int keycode, mods;

  printf("Config: %s\n", line);
  tokctx = line;
  keyseq = strdup(strtok_r(line, " ", &tokctx));
  commands = strdup(tokctx);

  keycode = parse_keycode(keyseq);
  mods = parse_mods(keyseq);

  addbinding(keycode, mods, commands);
}

GC creategc(Window win) {
  GC gc;
  XGCValues values;

  gc = XCreateGC(dpy, win, 0, &values);
  XSetForeground(dpy, gc, BlackPixel(dpy, 0));
  XSetBackground(dpy, gc, WhitePixel(dpy, 0));
  XSetLineAttributes(dpy, gc, 4, LineSolid, CapButt, JoinBevel);
  XSetFillStyle(dpy, gc, FillSolid);

  return gc;
}

void drawquadrants(Window win, int w, int h) {
  GC gc;
  XRectangle clip[20];
  int idx = 0;
  Colormap colormap;
  XColor red;

  gc = creategc(win);
  colormap = DefaultColormap(dpy, 0);

  XAllocNamedColor(dpy, colormap, "darkred", &red, &red);

# define BORDER 6
# define PEN 6

  /*left*/ clip[idx].x = 0; clip[idx].y = 0; clip[idx].width = BORDER; clip[idx].height = h;
  idx++;
  /*right*/ clip[idx].x = w-BORDER; clip[idx].y = 0; clip[idx].width = BORDER; clip[idx].height = h;
  idx++;
  /*top*/ clip[idx].x = 0; clip[idx].y = 0; clip[idx].width = w; clip[idx].height = BORDER;
  idx++;
  /*bottom*/ clip[idx].x = 0; clip[idx].y = h-BORDER; clip[idx].width = w; clip[idx].height = BORDER;
  idx++;
  /*horiz*/
  clip[idx].x = 0; clip[idx].y = h/2 - BORDER/2;
  clip[idx].width = w; clip[idx].height = BORDER;
  idx++;
  /*vert*/
  clip[idx].x = w/2 - BORDER/2; clip[idx].y = 0;
  clip[idx].width = BORDER; clip[idx].height = h;
  idx++;

  XShapeCombineRectangles(dpy, win, ShapeBounding, 0, 0, clip, idx, ShapeSet, 0);

#define CUTSIZE 4
  clip[idx].x = (w/2 - (CUTSIZE/2));
  clip[idx].y = (h/2 - (CUTSIZE/2));
  clip[idx].width = CUTSIZE;
  clip[idx].height = CUTSIZE;
  idx++;

  XShapeCombineRectangles(dpy, win, ShapeBounding, 0, 0, clip + idx - 1, 1, ShapeSubtract, 0);

  XSetForeground(dpy, gc, red.pixel);
  XDrawLine(dpy, win, gc, w/2, 0, w/2, h); // vert line
  XDrawLine(dpy, win, gc, 0, h/2, w, h/2); // horiz line
  XDrawLine(dpy, win, gc, BORDER - PEN, BORDER - PEN, w - PEN, BORDER - PEN); //top line
  XDrawLine(dpy, win, gc, BORDER - PEN, h - PEN, w - PEN, h - PEN); //bottom line
  XDrawLine(dpy, win, gc, BORDER - PEN, BORDER - PEN, BORDER - PEN, h - PEN); //left line
  XDrawLine(dpy, win, gc, w - PEN, BORDER - PEN, w - PEN, h - PEN); //left line
  XFlush(dpy);
}

/*
 * move/cut window
 * drawquadrants again
 */

void cmd_start(char *args) {
  XSetWindowAttributes winattr;

  if (appstate & STATE_ACTIVE)
    return;

  appstate |= STATE_ACTIVE;
  XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
  XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);

  wininfo.x = 0;
  wininfo.y = 0;
  wininfo.w = rootattr.width;
  wininfo.h = rootattr.height;

  zone = XCreateSimpleWindow(dpy, root, wininfo.x, wininfo.y, 
                             wininfo.w, wininfo.h, 0, 
                             BlackPixel(dpy, 0), WhitePixel(dpy, 0));

  /* Tell the window manager not to manage us */
  winattr.override_redirect = 1;
  XChangeWindowAttributes(dpy, zone, CWOverrideRedirect, &winattr);

  drawquadrants(zone, wininfo.w, wininfo.h);
  XMapWindow(dpy, zone);
  drawquadrants(zone, wininfo.w, wininfo.h);
}

void cmd_end(char *args) {
  if (!(appstate & STATE_ACTIVE))
    return;

  appstate &= ~(STATE_ACTIVE);
  XDestroyWindow(dpy, zone);
  XUngrabKeyboard(dpy, CurrentTime);
  XFlush(dpy);
}

void cmd_cut_up(char *args) {
  wininfo.h /= 2;
  update();
}

void cmd_cut_down(char *args) {
  wininfo.h /= 2;
  wininfo.y += wininfo.h;
  update();
}

void cmd_cut_left(char *args) {
  wininfo.w /= 2;
  update();
}

void cmd_cut_right(char *args) {
  wininfo.w /= 2;
  wininfo.x += wininfo.w;
  update();
}

void cmd_move_up(char *args) {
  wininfo.y -= wininfo.h;
  update();
}

void cmd_move_down(char *args) {
  wininfo.y += wininfo.h;
  update();
}

void cmd_move_left(char *args) {
  wininfo.x -= wininfo.w;
  update();
}

void cmd_move_right(char *args) {
  wininfo.x += wininfo.w;
  update();
}

void cmd_warp(char *args) {
  xdo_mousemove(xdo, wininfo.x + wininfo.w / 2, wininfo.y + wininfo.h / 2);
}

void cmd_click(char *args) {
  int button;
  button = atoi(args);
  if (button > 0)
    xdo_click(xdo, button);
  else
    fprintf(stderr, "Negative mouse button is invalid: %d\n", button);
}

void cmd_doubleclick(char *args) {
  cmd_click(args);
  cmd_click(args);
}

void cmd_drag(char *args) {
  int button;
  button = atoi(args);
  printf("Arg: %s\n", args);
  if (button <= 0) {
    fprintf(stderr, "Negative mouse button is invalid: %d\n", button);
    return;
  }

  if (appstate & STATE_DRAGGING) {
    printf("Ending drag\n");
    xdo_mouseup(xdo, button);
    appstate &= ~(STATE_DRAGGING);
  } else {
    printf("Starting drag\n");
    xdo_mousedown(xdo, button);
    appstate |= STATE_DRAGGING;
  }
}


void update() {
  XMoveResizeWindow(dpy, zone, wininfo.x, wininfo.y, wininfo.w, wininfo.h);
  drawquadrants(zone, wininfo.w, wininfo.h);
}

void handle_keypress(XKeyEvent *e) {
  int i;

  /* Loop over known keybindings */
  //printf("nkeys: %d\n", nkeybindings);
  for (i = 0; i < nkeybindings; i++) {
    //printf("%d/%d vs %d/%d\n", keybindings[i].keycode, keybindings[i].mods, e->keycode, e->state);
    if ((keybindings[i].keycode == e->keycode) &&
        (keybindings[i].mods == e->state)) {
      handle_commands(keybindings[i].commands);
    }
  }
}

void handle_commands(char *commands) {
  char *cmdcopy;
  char *tokctx, *tok, *strptr;
  
  cmdcopy = strdup(commands);
  strptr = cmdcopy;
  while ((tok = strtok_r(strptr, ",", &tokctx)) != NULL) {
    int i;
    //printf("cmd: %s\n", tok);

    strptr = NULL;
    for (i = 0; dispatch[i].command; i++) {
      /* If this command starts with a dispatch function, call it */
      if (!strncmp(tok, dispatch[i].command, strlen(dispatch[i].command))) {
        //printf("%s starts with %s\n", tok, dispatch[i].command);
        /* tok + len + 1 is
         * "command arg1 arg2"
         *          ^^^^^^^^^ <-- this 
         */
        dispatch[i].func(tok + strlen(dispatch[i].command) + 1);
      }
    }
  }
  free(cmdcopy);
}

int main(int argc, char **argv) {
  char *pcDisplay;
  int ret;

  if ( (pcDisplay = getenv("DISPLAY")) == NULL) {
    fprintf(stderr, "Error: DISPLAY environment variable not set\n");
    exit(1);
  }

  if ( (dpy = XOpenDisplay(pcDisplay)) == NULL) {
    fprintf(stderr, "Error: Can't open display: %s", pcDisplay);
    exit(1);
  }

  root = XDefaultRootWindow(dpy);
  xdo = xdo_new_with_opened_display(dpy, pcDisplay, False);

  XGetWindowAttributes(dpy, root, &rootattr);

  /* Parse config */
  parse_config();

  while (1) {
    XEvent e;
    XNextEvent(dpy, &e);
    switch (e.type) {
      case KeyPress:
        handle_keypress((XKeyEvent *)&e);
        break;
      case KeyRelease:
      default:
        break;
    }
  }

  xdo_free(xdo);
}
