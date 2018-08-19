/****************************************************************
 *								*
 * Copyright 2001, 2013 Fidelity Information Services, Inc	*
 *								*
 * Copyright (c) 2017-2018 YottaDB LLC. and/or its subsidiaries.*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/* Define handling of ALL signals. Most are now ignored but nothing should interrupt us
 * without our knowledge and a handler defined.
 */
#include "mdef.h"

#include "gtm_string.h"

#include <signal.h>
#include "continue_handler.h"
#include "sig_init.h"
#include "gtmci_signals.h"
#include "invocation_mode.h"

#ifdef GTM_PTHREAD
GBLREF	boolean_t		gtm_jvm_process;
#endif

#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif

void	null_handler(int sig);

void sig_init(void (*signal_handler)(), void (*ctrlc_handler)(), void (*suspsig_handler)(), void (*continue_handler)())
{
	struct sigaction 	ignore, null_action, def_action, susp_action, gen_action, ctrlc_action, cont_action, check;
	int			sig;
        DCL_THREADGBL_ACCESS;

        SETUP_THREADGBL_ACCESS;
	memset(&ignore, 0, SIZEOF(ignore));
	sigemptyset(&ignore.sa_mask);

	/* Copying only up to susp_action here to later specify SA_SIGINFO flag and continue copying with it. */
	null_action = def_action = susp_action = ignore;
	ignore.sa_handler = SIG_IGN;
	null_action.sa_handler = null_handler;
	def_action.sa_handler = SIG_DFL;

	/* Give us extra info on the following signals and a full core if necessary. */
	susp_action.sa_flags = SA_SIGINFO;
	gen_action = ctrlc_action = cont_action = susp_action;
	susp_action.sa_sigaction = suspsig_handler;
	gen_action.sa_sigaction = signal_handler;
	ctrlc_action.sa_sigaction = ctrlc_handler;
	cont_action.sa_sigaction = continue_handler;

	for (sig = 1; sig <= NSIG; sig++)
	{
		switch (sig)
		{
			case SIGHUP:
        			/* Tandem hack:  rather than ignore SIGHUP, we must catch it and do nothing with the signal. This
				 * prevents ioctls on modem/tty devices from hanging when carrier drops during the system call.
        			 */
				sigaction(sig, &null_action, NULL);
				break;
			case SIGCLD:
				/* Default handling necessary for SIGCLD signal. CAUTION: consider the affect on JOB (timeout)
				 * implementation before changing this behavior (like defuncts, ECHILD errors, etc.).
				 */
				sigaction(sig, &def_action, NULL);
				break;
			case SIGTSTP:
			case SIGTTIN:
			case SIGTTOU:
				/* These are all signals that suspend a process. */
				if (NULL != suspsig_handler)
					sigaction(sig, &susp_action, NULL);
				else
					sigaction(sig, &ignore, NULL);
				break;
			case SIGINT:
				/* If supplied with a control-C handler, install it now. */
				if (MUMPS_CALLIN != invocation_mode)
				{
					if (NULL != ctrlc_handler)
						sigaction(sig, &ctrlc_action, NULL);
					else
						sigaction(sig, &ignore, NULL);
				} else
				{	/* ^C for both call-ins and simpleapi allow the user to set a handler prior to making a
					 * call that would cause the YottaDB runtime to initialize and not have YDB override it.
					 * So if we find a handler defined other than the default, we allow it to remain.
					 * untouched.
					 */
					sigaction(sig, NULL, &check);			/* Fetch current ^C handler */
					if (SIG_DFL == check.sa_handler)
					{	/* No handler defined - it gets the passed in handler instead */
						sigaction(sig, &ctrlc_action, NULL);
					} /* else we leave the handler alone */
				}
				break;
			case SIGCONT:
				/* Special handling for SIGCONT. */
				if (NULL != continue_handler)
				{
#					ifndef DISABLE_SIGCONT_PROCESSING
					if (FALSE == TREF(disable_sigcont))
						sigaction(SIGCONT, &cont_action, NULL);
#					else
					TREF(disable_sigcont) = TRUE;
#					endif
				} else
				{
					sigaction(sig, &ignore, NULL);
					TREF(disable_sigcont) = TRUE;
				}
				break;
			case SIGSEGV:
#				ifdef GTM_PTHREAD
				if (gtm_jvm_process)
					break;
#				endif
			case SIGABRT:
#				ifdef GTM_PTHREAD
				if (gtm_jvm_process)
					break;
#				endif
			case SIGBUS:
#			ifdef _AIX
			case SIGDANGER:
#			endif
			case SIGFPE:
#			ifdef __MVS__
			case SIGABND:
#			else
				/* On Linux SIGIOT is commonly same as SIGABRT, so to avoid duplicate cases, check for that. */
#				if !defined(__CYGWIN__) && defined (SIGIOT) && (SIGIOT != SIGABRT)
			case SIGIOT:
#				endif
#				ifndef __linux__
			case SIGEMT:
#				endif
#			endif
			case SIGILL:
			case SIGQUIT:
#			ifndef __linux__
			case SIGSYS:
#			endif
			case SIGTERM:
			case SIGTRAP:
				/* These are all being handled by the generic_signal_handler. */
				sigaction(sig, &gen_action, NULL);
				break;
			default:
				sigaction(sig, &ignore, NULL);
		}
	}
}

/* Provide null signal handler */
void	null_handler(int sig)
{
	/* */
}
