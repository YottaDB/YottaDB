/****************************************************************
 *								*
 * Copyright 2001, 2013 Fidelity Information Services, Inc	*
 *								*
 * Copyright (c) 2017-2018 YottaDB LLC. and/or its subsidiaries.*
 * All rights reserved.						*
 *								*
 * Copyright (c) 2017-2018 Stephen L Johnson.			*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/* For the various Unix/Posix flavors, extract and fill in the appropriate information */

#include "mdef.h"

#include "gtm_string.h"

#include <sys/types.h>
#ifndef __MVS__
#  include <sys/param.h>
#endif
#include "gtm_inet.h"
#include "gtm_stdlib.h"
#include "gtm_stdio.h"
#include "gtm_unistd.h"

#if defined(__ia64) && defined(__hpux)
#include <sys/uc_access.h>
#include <machine/sys/reg_struct.h>
#endif /* __ia64 */

#include <signal.h>

#include "gtmsiginfo.h"

/* GCC on HPPA does not have SI_USER defined */
#if !defined(SI_USER) && defined(__GNUC__)
#  define SI_USER 0
#endif

/* OS/390 (R7) does not define SI_USER but for code expansions purposes, define the value it uses
   in its place */
#if !defined(SI_USER) && defined(__MVS__)
#  define SI_USER 0
#endif

error_def(ERR_SIGACCERR);
error_def(ERR_SIGADRALN);
error_def(ERR_SIGADRERR);
error_def(ERR_SIGBADSTK);
error_def(ERR_SIGCOPROC);
error_def(ERR_SIGFLTDIV);
error_def(ERR_SIGFLTINV);
error_def(ERR_SIGFLTOVF);
error_def(ERR_SIGFLTRES);
error_def(ERR_SIGFLTUND);
error_def(ERR_SIGILLADR);
error_def(ERR_SIGILLOPC);
error_def(ERR_SIGILLOPN);
error_def(ERR_SIGILLTRP);
error_def(ERR_SIGINTDIV);
error_def(ERR_SIGINTOVF);
error_def(ERR_SIGMAPERR);
error_def(ERR_SIGOBJERR);
error_def(ERR_SIGPRVOPC);
error_def(ERR_SIGPRVREG);

void extract_signal_info(int sig, siginfo_t *info, gtm_sigcontext_t *context, gtmsiginfo_t *gtmsi)
{
	memset(gtmsi, 0, SIZEOF(*gtmsi));
	gtmsi->signal = sig;
	if (NULL != info)
	{
		switch(info->si_code)
		{
			case SI_USER:
				gtmsi->send_pid = info->si_pid;
				gtmsi->send_uid = info->si_uid;
				gtmsi->infotype |= GTMSIGINFO_USER;
				break;
			default:
#  if defined(__osf__)
				gtmsi->subcode = info->si_code;
				gtmsi->bad_vadr = info->si_addr;
				gtmsi->infotype |= GTMSIGINFO_BADR;
				if (NULL != context)
				{
					gtmsi->int_iadr = (caddr_t)context->sc_pc;
					gtmsi->infotype |= GTMSIGINFO_ILOC;
				}
				break;
#  elif defined(__hpux)
				gtmsi->subcode = info->si_code;
				gtmsi->bad_vadr = info->si_addr;
				gtmsi->infotype |= GTMSIGINFO_BADR;
				if (NULL != context)
				{
#if !defined(__ia64) && !defined(__GNUC__)
					if (0 == (context->uc_mcontext.ss_flags & SS_NARROWISINVALID))
					{
						/* Interrupt location is in narrow area */
						gtmsi->int_iadr = (caddr_t)(context->uc_mcontext.ss_narrow.ss_pcoq_head & ~3);
					} else
					{
						/* Interupt location is in wide area */
						gtmsi->int_iadr =
							(caddr_t)(context->uc_mcontext.ss_wide.ss_32.ss_pcoq_head_hi & ~3);
					}
#else /* __ia64 */
					__uc_get_ip(context, (uint64_t *) &gtmsi->int_iadr);
#endif /* __ia64 */
					gtmsi->infotype |= GTMSIGINFO_ILOC;
				}
				break;
#  elif defined(__linux__)
				gtmsi->subcode = info->si_code;
				gtmsi->bad_vadr = info->si_addr;
				gtmsi->infotype |= GTMSIGINFO_BADR;
				if (NULL != context)
				{
#    ifdef __ia64
					gtmsi->int_iadr = (caddr_t)context->uc_mcontext.sc_ip;
#    elif defined(__i386)
#      ifndef REG_EIP
#        define REG_EIP EIP
#      endif
					gtmsi->int_iadr = (caddr_t)context->uc_mcontext.gregs[REG_EIP];
#    elif defined(__x86_64__)
#      ifndef REG_RIP
#        define REG_RIP EIP
#      endif
					gtmsi->int_iadr = (caddr_t)context->uc_mcontext.gregs[REG_RIP];
#    elif defined(__s390__)
					gtmsi->int_iadr = (caddr_t)context->uc_mcontext.psw.addr;
#    elif defined(__armv6l__) || defined(__armv7l__)
					gtmsi->int_iadr = (caddr_t)context->uc_mcontext.arm_pc;
#    else
#      error "Unsupported Linux Platform"
#    endif
					gtmsi->infotype |= GTMSIGINFO_ILOC;
				}
				break;
#  elif defined(__sparc)
				gtmsi->subcode = info->si_code;
				gtmsi->bad_vadr = info->si_addr;
				gtmsi->infotype |= GTMSIGINFO_BADR;
				if (NULL != context)
				{
					gtmsi->int_iadr = (caddr_t)context->uc_mcontext.gregs[REG_PC];
					gtmsi->infotype |= GTMSIGINFO_ILOC;
				}
				break;
#  elif defined(__CYGWIN__)
				gtmsi->subcode = info->si_code;
				gtmsi->bad_vadr = info->si_addr;
				gtmsi->infotype |= GTMSIGINFO_BADR;
				break;
#  elif defined(__MVS__)
				if (0 > info->si_code)
				{	/* sent from another process */
					gtmsi->send_pid = info->si_pid;
					gtmsi->send_uid = info->si_uid;
					gtmsi->infotype |= GTMSIGINFO_USER;
				} else
				{
                                	gtmsi->subcode = info->si_code;
					if ((SIGBUS == sig) || (SIGFPE == sig) || (SIGILL == sig) || (SIGSEGV == sig))
					{	/* address of faulting instruction */
                                		gtmsi->int_iadr = info->si_addr;
                                		gtmsi->infotype |= GTMSIGINFO_ILOC;
					}
/* we don't know the format of the mcontext structure yet
					if (context != NULL)
                                	{
                                	        gtmsi->bad_vadr = (caddr_t)context->uc_mcontext[0];
                                	        gtmsi->infotype |= GTMSIGINFO_BADR;
                                	}
*/
				}
				break;
#  elif defined(_AIX)
				gtmsi->subcode = info->si_code;
				gtmsi->bad_vadr = info->si_addr;
				gtmsi->infotype |= GTMSIGINFO_BADR;
				if (context != NULL)
				{
					gtmsi->int_iadr = (caddr_t)context->sc_context.iar;
					gtmsi->infotype |= GTMSIGINFO_ILOC;
				}
				break;
#  elif defined(__APPLE__)
				gtmsi->subcode = info->si_code;
				gtmsi->bad_vadr = info->si_addr;
				gtmsi->infotype |= GTMSIGINFO_BADR;
				if (context != NULL)
				{
					/*TODO gtmsi->int_iadr = (caddr_t)context->uc_mcontext.gregs[REG_EIP];*/
					gtmsi->infotype |= GTMSIGINFO_ILOC;
				}
				break;
#  else
#  error "Unsupported Platform"
#  endif
		}

		/* See if additional information can be gleaned from the subcode */
		if (0 != gtmsi->subcode)
		{
			switch (sig)
			{
				case SIGILL :
					switch (gtmsi->subcode)
					{
						case ILL_ILLOPC : gtmsi->sig_err = ERR_SIGILLOPC;
							break;
						case ILL_ILLOPN : gtmsi->sig_err = ERR_SIGILLOPN;
							break;
						case ILL_ILLADR : gtmsi->sig_err = ERR_SIGILLADR;
							break;
						case ILL_ILLTRP : gtmsi->sig_err = ERR_SIGILLTRP;
							break;
						case ILL_PRVOPC : gtmsi->sig_err = ERR_SIGPRVOPC;
							break;
						case ILL_PRVREG : gtmsi->sig_err = ERR_SIGPRVREG;
							break;
						case ILL_COPROC : gtmsi->sig_err = ERR_SIGCOPROC;
							break;
						case ILL_BADSTK : gtmsi->sig_err = ERR_SIGBADSTK;
							break;
					}
					break;
				case SIGBUS :
					switch (gtmsi->subcode)
					{
						case BUS_ADRALN : gtmsi->sig_err = ERR_SIGADRALN;
							break;
						case BUS_ADRERR : gtmsi->sig_err = ERR_SIGADRERR;
							break;
						case BUS_OBJERR : gtmsi->sig_err = ERR_SIGOBJERR;
							break;
					}
					break;
				case SIGFPE :
					switch (gtmsi->subcode)
					{
						case FPE_INTDIV : gtmsi->sig_err = ERR_SIGINTDIV;
							break;
						case FPE_INTOVF : gtmsi->sig_err = ERR_SIGINTOVF;
							break;
						case FPE_FLTDIV : gtmsi->sig_err = ERR_SIGFLTDIV;
							break;
						case FPE_FLTOVF : gtmsi->sig_err = ERR_SIGFLTOVF;
							break;
						case FPE_FLTUND : gtmsi->sig_err = ERR_SIGFLTUND;
							break;
						case FPE_FLTRES : gtmsi->sig_err = ERR_SIGFLTRES;
							break;
						case FPE_FLTINV	: gtmsi->sig_err = ERR_SIGFLTINV;
							break;
					}
					break;
				case SIGSEGV :
					switch (gtmsi->subcode)
					{
						case SEGV_MAPERR : gtmsi->sig_err = ERR_SIGMAPERR;
							break;
						case SEGV_ACCERR : gtmsi->sig_err = ERR_SIGACCERR;
							break;
					}
					break;
			}
		}
	}
}
