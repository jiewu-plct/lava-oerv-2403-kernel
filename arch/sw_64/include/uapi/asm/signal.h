/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_SW64_SIGNAL_H
#define _UAPI_ASM_SW64_SIGNAL_H

#include <linux/types.h>

/* Avoid too many header ordering problems.  */
struct siginfo;

#ifndef __KERNEL__
/* Here we must cater to libcs that poke about in kernel headers.  */

#define NSIG		32
typedef unsigned long sigset_t;

#endif /* __KERNEL__ */


/*
 * Linux/sw64 different signal numbers that Linux/i386.
 */
#define SIGHUP		 1
#define SIGINT		 2
#define SIGQUIT		 3
#define SIGILL		 4
#define SIGTRAP		 5
#define SIGABRT		 6
#define SIGEMT		 7
#define SIGFPE		 8
#define SIGKILL		 9
#define SIGBUS		10
#define SIGSEGV		11
#define SIGSYS		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGURG		16
#define SIGSTOP		17
#define SIGTSTP		18
#define SIGCONT		19
#define SIGCHLD		20
#define SIGTTIN		21
#define SIGTTOU		22
#define SIGIO		23
#define SIGXCPU		24
#define SIGXFSZ		25
#define SIGVTALRM	26
#define SIGPROF		27
#define SIGWINCH	28
#define SIGINFO		29
#define SIGUSR1		30
#define SIGUSR2		31

#define SIGPOLL		SIGIO
#define SIGPWR		SIGINFO
#define SIGIOT		SIGABRT

/* These should not be considered constants from userland.  */
#define SIGRTMIN	32
#define SIGRTMAX	_NSIG

/*
 * SA_FLAGS values:
 *
 * SA_ONSTACK indicates that a registered stack_t will be used.
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 * SA_NOCLDSTOP flag to turn off SIGCHLD when children stop.
 * SA_RESETHAND clears the handler when the signal is delivered.
 * SA_NOCLDWAIT flag on SIGCHLD to inhibit zombies.
 * SA_NODEFER prevents the current signal from being masked in the handler.
 *
 * SA_ONESHOT and SA_NOMASK are the historical Linux names for the Single
 * Unix names RESETHAND and NODEFER respectively.
 */

#define SA_ONSTACK	0x00000001
#define SA_RESTART	0x00000002
#define SA_NOCLDSTOP	0x00000004
#define SA_NODEFER	0x00000008
#define SA_RESETHAND	0x00000010
#define SA_NOCLDWAIT	0x00000020
#define SA_SIGINFO	0x00000040

#define SA_ONESHOT	SA_RESETHAND
#define SA_NOMASK	SA_NODEFER

#define MINSIGSTKSZ	4096
#define SIGSTKSZ	16384

#define SIG_BLOCK	1	/* for blocking signals */
#define SIG_UNBLOCK	2	/* for unblocking signals */
#define SIG_SETMASK	3	/* for setting the signal mask */

#include <asm-generic/signal-defs.h>

#ifndef __KERNEL__
/* Here we must cater to libcs that poke about in kernel headers.  */

struct sigaction {
	union {
		__sighandler_t	_sa_handler;
		void (*_sa_sigaction)(int sig, struct siginfo *info, void *ucontext);
	} _u;
	sigset_t	sa_mask;
	int		sa_flags;
};

#define sa_handler	_u._sa_handler
#define sa_sigaction	_u._sa_sigaction

#endif /* __KERNEL__ */

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	size_t ss_size;
} stack_t;

#endif /* _UAPI_ASM_SW64_SIGNAL_H */
