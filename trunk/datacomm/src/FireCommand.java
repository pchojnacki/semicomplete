/*
 * FireCommand.java
 *
 * Version:
 *   $Id$
 *
 * Revisions:
 *   $Log$
 *   Revision 1.2  2004/01/19 04:54:07  psionic
 *   - Added generic framework for syntax checking
 *
 *   Revision 1.1  2004/01/18 23:54:53  psionic
 *   - Stub written for FireCommand
 *
 *
 */

import java.util.List;

public class FireCommand extends Command {

	public FireCommand() {
		super("fire");
	}

	public FireCommand(List args) {
		super("fire", args);
	}

	public FireCommand(String args) throws InvalidCommandArgumentsException {
		super("fire");
		this.args = this.parseArgs(args);
	}

	private boolean validateArguments(List args) 
	        throws InvalidCommandArgumentsException {
		System.err.println("Validating args: " + args);

		// First Argument
		String arg = (String) args.get(0);
		if ( (arg.length() > 1) || (!arg.matches("[a-jA-J]")) ) {
			return false;
		}

		// Second Argument
		try {
			Integer i = new Integer((String)args.get(1));
		} catch (Exception e) {
			return false;
		}

		return true;
	}

}
