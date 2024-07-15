// SPDX-License-Identifier: GPL-2.0
/* ptrace.c */
/* By Ross Biro 1/23/92 */
/* edited by Linus Torvalds */
/* mangled further by Bob Manson (manson@santafe.edu) */
/* more mutilation by David Mosberger (davidm@azstarnet.com) */

#include <linux/audit.h>
#include <linux/regset.h>
#include <linux/elf.h>
#include <linux/sched/task_stack.h>

#include <asm/asm-offsets.h>

#include "proto.h"
#include <asm/csr.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

#define BREAKINST	0x00000080 /* sys_call bpt */

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Processes always block with the following stack-layout:
 *
 *  +================================+ <---- task + 2*PAGE_SIZE
 *  | HMcode saved frame (ps, pc,    | ^
 *  | gp, a0, a1, a2)		     | |
 *  +================================+ | struct pt_regs
 *  |				     | |
 *  | frame generated by SAVE_ALL    | |
 *  |				     | v
 *  +================================+
 */

/*
 * The following table maps a register index into the stack offset at
 * which the register is saved.  Register indices are 0-31 for integer
 * regs, 32-63 for fp regs, and 64 for the pc.  Notice that sp and
 * zero have no stack-slot and need to be treated specially (see
 * get_reg/put_reg below).
 */
#define PCB_OFF(var)	offsetof(struct pcb_struct, var)

static int pcboff[] = {
	[PT_TP] = PCB_OFF(tp),
	[PT_DA_MATCH] = PCB_OFF(da_match),
	[PT_DA_MASK] = PCB_OFF(da_mask),
	[PT_DV_MATCH] = PCB_OFF(dv_match),
	[PT_DV_MASK] = PCB_OFF(dv_mask),
	[PT_DC_CTL] = PCB_OFF(dc_ctl),
	[PT_MATCH_CTL] = PCB_OFF(match_ctl),
	[PT_IA_MATCH] = PCB_OFF(ia_match),
	[PT_IA_MASK] = PCB_OFF(ia_mask),
	[PT_IV_MATCH] = PCB_OFF(iv_match),
	[PT_IDA_MATCH] = PCB_OFF(ida_match),
	[PT_IDA_MASK] = PCB_OFF(ida_mask)
};

static unsigned long zero;

/*
 * Get address of register REGNO in task TASK.
 */

static unsigned long *
get_reg_addr(struct task_struct *task, unsigned long regno)
{
	void *addr;
	int fno, vno;

	switch (regno) {
	case PT_UNIQUE:
	case PT_DA_MATCH:
	case PT_DA_MASK:
	case PT_DV_MATCH:
	case PT_DV_MASK:
	case PT_MATCH_CTL:
	case PT_IA_MATCH:
	case PT_IA_MASK:
	case PT_IV_MATCH:
	case PT_IDA_MATCH:
	case PT_IDA_MASK:
		addr = (void *)task_thread_info(task) + pcboff[regno];
		break;
	case PT_REG_BASE ... PT_REG_END:
		addr = &task_pt_regs(task)->regs[regno];
		break;
	case PT_FPREG_BASE ... PT_FPREG_END:
		fno = regno - PT_FPREG_BASE;
		addr = &task->thread.fpstate.fp[fno].v[0];
		break;
	case PT_VECREG_BASE ... PT_VECREG_END:
		/*
		 * return addr for zero value if we catch vectors of f31
		 * v0 and v3 of f31 are not in this range so ignore them
		 */
		if (regno == PT_F31_V1 || regno == PT_F31_V2) {
			addr = &zero;
			break;
		}
		fno = (regno - PT_VECREG_BASE) & 0x1f;
		vno = 1 + ((regno - PT_VECREG_BASE) >> 5);
		addr = &task->thread.fpstate.fp[fno].v[vno];
		break;
	case PT_FPCR:
		addr = &task->thread.fpstate.fpcr;
		break;
	case PT_PC:
		addr = (void *)task_pt_regs(task) + PT_REGS_PC;
		break;
	default:
		addr = &zero;
	}

	return addr;
}

/*
 * Get contents of register REGNO in task TASK.
 */
unsigned long
get_reg(struct task_struct *task, unsigned long regno)
{
	return *get_reg_addr(task, regno);
}

/*
 * Write contents of register REGNO in task TASK.
 */
static int
put_reg(struct task_struct *task, unsigned long regno, unsigned long data)
{
	*get_reg_addr(task, regno) = data;
	return 0;
}

static inline int
read_int(struct task_struct *task, unsigned long addr, int *data)
{
	int copied = access_process_vm(task, addr, data, sizeof(int), FOLL_FORCE);

	return (copied == sizeof(int)) ? 0 : -EIO;
}

static inline int
write_int(struct task_struct *task, unsigned long addr, int data)
{
	int copied = access_process_vm(task, addr, &data, sizeof(int),
			FOLL_FORCE | FOLL_WRITE);
	return (copied == sizeof(int)) ? 0 : -EIO;
}

/*
 * Set breakpoint.
 */
int
ptrace_set_bpt(struct task_struct *child)
{
	int displ, i, res, reg_b, nsaved = 0;
	unsigned int insn, op_code;
	unsigned long pc;

	pc = get_reg(child, PT_PC);
	res = read_int(child, pc, (int *)&insn);
	if (res < 0)
		return res;

	op_code = insn >> 26;
	/* br bsr beq bne blt ble bgt bge blbc blbs fbeq fbne fblt fble fbgt fbge */
	if ((1UL << op_code) & 0x3fff000000000030UL) {
		/*
		 * It's a branch: instead of trying to figure out
		 * whether the branch will be taken or not, we'll put
		 * a breakpoint at either location.  This is simpler,
		 * more reliable, and probably not a whole lot slower
		 * than the alternative approach of emulating the
		 * branch (emulation can be tricky for fp branches).
		 */
		displ = ((s32)(insn << 11)) >> 9;
		task_thread_info(child)->bpt_addr[nsaved++] = pc + 4;
		if (displ) /* guard against unoptimized code */
			task_thread_info(child)->bpt_addr[nsaved++]
				= pc + 4 + displ;
		/*call ret jmp*/
	} else if (op_code >= 0x1 && op_code <= 0x3) {
		reg_b = (insn >> 16) & 0x1f;
		task_thread_info(child)->bpt_addr[nsaved++] = get_reg(child, reg_b);
	} else {
		task_thread_info(child)->bpt_addr[nsaved++] = pc + 4;
	}

	/* install breakpoints: */
	for (i = 0; i < nsaved; ++i) {
		res = read_int(child, task_thread_info(child)->bpt_addr[i],
				(int *)&insn);
		if (res < 0)
			return res;
		task_thread_info(child)->bpt_insn[i] = insn;
		res = write_int(child, task_thread_info(child)->bpt_addr[i],
				BREAKINST);
		if (res < 0)
			return res;
	}
	task_thread_info(child)->bpt_nsaved = nsaved;
	return 0;
}

/*
 * Ensure no single-step breakpoint is pending.  Returns non-zero
 * value if child was being single-stepped.
 */
int
ptrace_cancel_bpt(struct task_struct *child)
{
	int i, nsaved = task_thread_info(child)->bpt_nsaved;

	task_thread_info(child)->bpt_nsaved = 0;

	if (nsaved > 2) {
		pr_info("%s: bogus nsaved: %d!\n", __func__, nsaved);
		nsaved = 2;
	}

	for (i = 0; i < nsaved; ++i) {
		write_int(child, task_thread_info(child)->bpt_addr[i],
				task_thread_info(child)->bpt_insn[i]);
	}
	return (nsaved != 0);
}

void user_enable_single_step(struct task_struct *child)
{
	/* Mark single stepping.  */
	task_thread_info(child)->bpt_nsaved = -1;
}

void user_disable_single_step(struct task_struct *child)
{
	ptrace_cancel_bpt(child);
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
}

static int gpr_get(struct task_struct *target,
			const struct user_regset *regset,
			struct membuf to)
{
	return membuf_write(&to, task_pt_regs(target), sizeof(struct user_pt_regs));
}

static int gpr_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				task_pt_regs(target), 0, sizeof(struct user_pt_regs));
}

static int fpr_get(struct task_struct *target,
			const struct user_regset *regset,
			struct membuf to)
{

	return membuf_write(&to, &target->thread.fpstate,
			sizeof(struct user_fpsimd_state));
}

static int fpr_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				&target->thread.fpstate, 0,
				sizeof(struct user_fpsimd_state));
}

enum sw64_regset {
	REGSET_GPR,
	REGSET_FPR,
};

static const struct user_regset sw64_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = ELF_NGREG,
		.size = sizeof(elf_greg_t),
		.align = sizeof(elf_greg_t),
		.regset_get = gpr_get,
		.set = gpr_set
	},
	[REGSET_FPR] = {
		.core_note_type = NT_PRFPREG,
		.n = sizeof(struct user_fpsimd_state) / sizeof(u64),
		.size = sizeof(u64),
		.align = sizeof(u64),
		.regset_get = fpr_get,
		.set = fpr_set
	},
};

static const struct user_regset_view user_sw64_view = {
	.name = "sw64", .e_machine = EM_SW64,
	.regsets = sw64_regsets, .n = ARRAY_SIZE(sw64_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_sw64_view;
}

long arch_ptrace(struct task_struct *child, long request,
		unsigned long addr, unsigned long data)
{
	unsigned long tmp;
	size_t copied;
	long ret;

	switch (request) {
	/* When I and D space are separate, these will need to be fixed.  */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA:
		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), FOLL_FORCE);
		ret = -EIO;
		if (copied != sizeof(tmp))
			break;

		force_successful_syscall_return();
		ret = tmp;
		break;

	/* Read register number ADDR. */
	case PTRACE_PEEKUSR:
		force_successful_syscall_return();
		ret = get_reg(child, addr);
		break;

	/* When I and D space are separate, this will have to be fixed.  */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = generic_ptrace_pokedata(child, addr, data);
		break;

	case PTRACE_POKEUSR: /* write the specified register */
		ret = put_reg(child, addr, data);
		break;
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
	return ret;
}

asmlinkage unsigned long syscall_trace_enter(void)
{
	unsigned long ret = 0;
	struct pt_regs *regs = current_pt_regs();

	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
		ptrace_report_syscall_entry(regs))
		return NO_SYSCALL;

#ifdef CONFIG_SECCOMP
	/* Do seccomp after ptrace, to catch any tracer changes. */
	if (secure_computing() == -1)
		return NO_SYSCALL;
#endif

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_enter(regs, regs->regs[0]);
	audit_syscall_entry(regs->regs[0], regs->regs[16], regs->regs[17], regs->regs[18], regs->regs[19]);
	return ret ?: regs->regs[0];
}

asmlinkage void
syscall_trace_leave(void)
{
	struct pt_regs *regs = current_pt_regs();

	audit_syscall_exit(regs);
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		ptrace_report_syscall_exit(regs, 0);
	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_exit(regs, regs_return_value(regs));
}

#ifdef CONFIG_SUBARCH_C3B
static long rwcsr(int rw, unsigned long csr, unsigned long value)
{
	register unsigned long __r0 __asm__("$0");
	register unsigned long __r16 __asm__("$16") = rw;
	register unsigned long __r17 __asm__("$17") = csr;
	register unsigned long __r18 __asm__("$18") = value;

	__asm__ __volatile__(
			"sys_call %4"
			: "=r"(__r0), "=r"(__r16), "=r"(__r17), "=r"(__r18)
			: "i"(HMC_rwreg), "1"(__r16), "2"(__r17), "3"(__r18)
			: "$1", "$22", "$23", "$24", "$25");

	return __r0;
}

#define RCSR 0
#define WCSR 1

#define CSR_DA_MATCH	0
#define CSR_DA_MASK	1
#define CSR_IA_MATCH	2
#define CSR_IA_MASK	3
#define CSR_IDA_MATCH	6
#define CSR_IDA_MASK	7
#define CSR_DC_CTL	11
#define CSR_DV_MATCH	15
#define CSR_DV_MASK	16

#define DV_MATCH_EN_S	19
#define DAV_MATCH_EN_S	20

int do_match(unsigned long address, unsigned long mmcsr, long cause, struct pt_regs *regs)
{
	unsigned long dc_ctl;
	unsigned long value;

	pr_info("%s: pid %d, name = %s,cause = %#lx, mmcsr = %#lx, address = %#lx, pc %#lx\n",
			__func__, current->pid, current->comm, cause, mmcsr, address, regs->pc);

	switch (mmcsr) {
	case MMCSR__DA_MATCH:
	case MMCSR__DV_MATCH:
	case MMCSR__DAV_MATCH:
		show_regs(regs);

		if (!(current->ptrace & PT_PTRACED)) {
			pr_notice(" pid %d %s not be ptraced, return\n", current->pid, current->comm);
			if (mmcsr == MMCSR__DA_MATCH)
				rwcsr(WCSR, CSR_DA_MATCH, 0);   //clear da_match
			if (mmcsr == MMCSR__DV_MATCH) {
				value = rwcsr(RCSR, CSR_DV_MATCH, 0);
				pr_notice("value is %#lx\n", value);
				value = rwcsr(RCSR, CSR_DV_MASK, 0);
				pr_notice("value is %#lx\n", value);
				dc_ctl = rwcsr(RCSR, CSR_DC_CTL, 0);
				dc_ctl &= ~(0x1UL << DV_MATCH_EN_S);
				rwcsr(WCSR, CSR_DC_CTL, dc_ctl);
			}
			if (mmcsr == MMCSR__DAV_MATCH) {
				dc_ctl = rwcsr(RCSR, CSR_DC_CTL, 0);
				dc_ctl &= ~((0x1UL << DV_MATCH_EN_S) | (0x1UL << DAV_MATCH_EN_S));
				rwcsr(WCSR, CSR_DC_CTL, dc_ctl);
				rwcsr(WCSR, CSR_DA_MATCH, 0);   //clear da_match
			}
			task_thread_info(current)->pcb.da_match = 0;
			task_thread_info(current)->pcb.dv_match = 0;
			task_thread_info(current)->pcb.dc_ctl = 0;
			return 1;
		}

		if (mmcsr == MMCSR__DA_MATCH) {
			rwcsr(WCSR, CSR_DA_MATCH, 0);   //clear da_match
			task_thread_info(current)->pcb.da_match = 0;
		}
		if (mmcsr == MMCSR__DV_MATCH) {
			dc_ctl = rwcsr(RCSR, CSR_DC_CTL, 0);
			dc_ctl &= ~(0x1UL << DV_MATCH_EN_S);
			rwcsr(WCSR, CSR_DC_CTL, dc_ctl);
		}
		if (mmcsr == MMCSR__DAV_MATCH) {
			dc_ctl = rwcsr(RCSR, CSR_DC_CTL, 0);
			dc_ctl &= ~((0x1UL << DV_MATCH_EN_S) | (0x1UL << DAV_MATCH_EN_S));
			rwcsr(WCSR, CSR_DC_CTL, dc_ctl);
			rwcsr(WCSR, CSR_DA_MATCH, 0);   //clear da_match
		}
		task_thread_info(current)->pcb.dv_match = 0;
		task_thread_info(current)->pcb.dc_ctl = 0;
		pr_notice("do_page_fault: want to send SIGTRAP, pid = %d\n", current->pid);
		force_sig_fault(SIGTRAP, TRAP_HWBKPT, (void *) address);
		return 1;

	case MMCSR__IA_MATCH:
		rwcsr(WCSR, CSR_IA_MATCH, 0);       //clear ia_match
		return 1;
	case MMCSR__IDA_MATCH:
		rwcsr(WCSR, CSR_IDA_MATCH, 0);       //clear ida_match
		return 1;
	}

	return 0;
}

void restore_da_match_after_sched(void)
{
	unsigned long dc_ctl_mode;
	unsigned long dc_ctl;
	struct pcb_struct *pcb = &task_thread_info(current)->pcb;

	rwcsr(WCSR, CSR_DA_MATCH, 0);
	rwcsr(WCSR, CSR_DA_MASK, pcb->da_mask);
	rwcsr(WCSR, CSR_DA_MATCH, pcb->da_match);
	dc_ctl_mode = pcb->dc_ctl;
	dc_ctl = rwcsr(RCSR, CSR_DC_CTL, 0);
	dc_ctl &= ~((0x1UL << DV_MATCH_EN_S) | (0x1UL << DAV_MATCH_EN_S));
	dc_ctl |= ((dc_ctl_mode << DV_MATCH_EN_S) & ((0x1UL << DV_MATCH_EN_S) | (0x1UL << DAV_MATCH_EN_S)));
	if (dc_ctl_mode & 0x1) {
		rwcsr(WCSR, CSR_DV_MATCH, pcb->dv_match);
		rwcsr(WCSR, CSR_DV_MASK, pcb->dv_mask);
		rwcsr(WCSR, CSR_DC_CTL, dc_ctl);
	}
}

#elif defined(CONFIG_SUBARCH_C4)
int do_match(unsigned long address, unsigned long mmcsr, long cause, struct pt_regs *regs)
{
	kernel_siginfo_t info;
	unsigned long match_ctl, ia_match;
	sigval_t sw64_value;

	pr_info("%s: pid %d, name = %s, cause = %#lx, mmcsr = %#lx, address = %#lx, pc %#lx\n",
			__func__, current->pid, current->comm, cause, mmcsr, address, regs->pc);

	switch (mmcsr) {
	case MMCSR__DA_MATCH:
	case MMCSR__DV_MATCH:
	case MMCSR__DAV_MATCH:
	case MMCSR__IA_MATCH:
	case MMCSR__IDA_MATCH:
	case MMCSR__IV_MATCH:
		show_regs(regs);

		if (!(current->ptrace & PT_PTRACED)) {
			pr_notice(" pid %d %s not be ptraced, return\n", current->pid, current->comm);
			if (mmcsr == MMCSR__DA_MATCH) {
				match_ctl = read_csr(CSR_DC_CTLP);
				match_ctl &= ~(0x3UL << DA_MATCH_EN_S);
				write_csr(match_ctl, CSR_DC_CTLP);
				write_csr(0, CSR_DA_MATCH);		// clear da_match
				task_thread_info(current)->pcb.match_ctl &= ~0x1;
				task_thread_info(current)->pcb.da_match = 0;
			}
			if (mmcsr == MMCSR__DV_MATCH) {
				match_ctl = read_csr(CSR_DC_CTLP);
				match_ctl &= ~(0x1UL << DV_MATCH_EN_S);
				write_csr(match_ctl, CSR_DC_CTLP);
				write_csr(0, CSR_DV_MATCH);		// clear dv_match
				task_thread_info(current)->pcb.match_ctl &= ~(0x1 << 1);
				task_thread_info(current)->pcb.dv_match = 0;
			}
			if (mmcsr == MMCSR__DAV_MATCH) {
				match_ctl = read_csr(CSR_DC_CTLP);
				match_ctl &= ~((0x3UL << DA_MATCH_EN_S) | (0x1UL << DV_MATCH_EN_S) | (0x1UL << DAV_MATCH_EN_S));
				write_csr(match_ctl, CSR_DC_CTLP);
				write_csr(0, CSR_DA_MATCH);		// clear da_match
				write_csr(0, CSR_DV_MATCH);		// clear dv_match
				task_thread_info(current)->pcb.match_ctl &= ~(0x1 | (0x1 << 1) | (0x1 << 2));
				task_thread_info(current)->pcb.da_match = 0;
				task_thread_info(current)->pcb.dv_match = 0;
			}
			if (mmcsr == MMCSR__IA_MATCH) {
				ia_match = read_csr(CSR_IA_MATCH);
				ia_match &= ~((0x1UL << IA_MATCH_EN_S) | (0x7ffffffffffffUL << 2));
				write_csr(ia_match, CSR_IA_MATCH);	// clear ia_match
				task_thread_info(current)->pcb.match_ctl &= ~(0x1 << 3);
				task_thread_info(current)->pcb.ia_match = 0;
			}
			if (mmcsr == MMCSR__IV_MATCH) {
				ia_match = read_csr(CSR_IA_MATCH);
				ia_match &= ~((0x1UL << IV_MATCH_EN_S) | (0x1UL << IV_PM_EN_S));
				write_csr(ia_match, CSR_IA_MATCH);	// clear ia_match
				write_csr(0, CSR_IV_MATCH);		// clear iv_match
				task_thread_info(current)->pcb.match_ctl &= ~(0x1 << 4);
				task_thread_info(current)->pcb.ia_match &= ~((0x1UL << IV_MATCH_EN_S) | (0x1UL << IV_PM_EN_S));
				task_thread_info(current)->pcb.iv_match = 0;
			}
			if (mmcsr == MMCSR__IDA_MATCH) {
				write_csr(0, CSR_IDA_MATCH);		// clear ida_match
				task_thread_info(current)->pcb.match_ctl &= ~(0x1 << 5);
				task_thread_info(current)->pcb.ida_match = 0;
			}
			return 1;
		}

		info.si_signo = SIGTRAP;
		info.si_addr = (void *) address;
		sw64_value.sival_ptr = (void *)(regs->pc);
		info.si_value = sw64_value;
		info.si_code = TRAP_HWBKPT;

		if (mmcsr == MMCSR__DA_MATCH) {
			info.si_errno = 1;
			match_ctl = read_csr(CSR_DC_CTLP);
			match_ctl &= ~(0x3UL << DA_MATCH_EN_S);
			write_csr(match_ctl, CSR_DC_CTLP);
			write_csr(0, CSR_DA_MATCH);		// clear da_match
			task_thread_info(current)->pcb.match_ctl &= ~0x1;
			task_thread_info(current)->pcb.da_match = 0;
		}
		if (mmcsr == MMCSR__DV_MATCH) {
			info.si_errno = 2;
			match_ctl = read_csr(CSR_DC_CTLP);
			match_ctl &= ~(0x1UL << DV_MATCH_EN_S);
			write_csr(match_ctl, CSR_DC_CTLP);
			write_csr(0, CSR_DV_MATCH);		// clear dv_match
			task_thread_info(current)->pcb.match_ctl &= ~(0x1 << 1);
			task_thread_info(current)->pcb.dv_match = 0;
		}
		if (mmcsr == MMCSR__DAV_MATCH) {
			info.si_errno = 3;
			match_ctl = read_csr(CSR_DC_CTLP);
			match_ctl &= ~((0x3UL << DA_MATCH_EN_S) | (0x1UL << DV_MATCH_EN_S) | (0x1UL << DAV_MATCH_EN_S));
			write_csr(match_ctl, CSR_DC_CTLP);
			write_csr(0, CSR_DA_MATCH);		// clear da_match
			write_csr(0, CSR_DV_MATCH);		// clear dv_match
			task_thread_info(current)->pcb.match_ctl &= ~(0x1 | (0x1 << 1) | (0x1 << 2));
			task_thread_info(current)->pcb.da_match = 0;
			task_thread_info(current)->pcb.dv_match = 0;
		}
		if (mmcsr == MMCSR__IA_MATCH) {
			info.si_errno = 4;
			ia_match = read_csr(CSR_IA_MATCH);
			ia_match &= ~((0x1UL << IA_MATCH_EN_S) | (0x7ffffffffffffUL << 2));
			write_csr(ia_match, CSR_IA_MATCH);	// clear ia_match
			task_thread_info(current)->pcb.match_ctl &= ~(0x1 << 3);
			task_thread_info(current)->pcb.ia_match = 0;
		}
		if (mmcsr == MMCSR__IV_MATCH) {
			info.si_errno = 5;
			ia_match = read_csr(CSR_IA_MATCH);
			ia_match &= ~((0x1UL << IV_MATCH_EN_S) | (0x1UL << IV_PM_EN_S));
			write_csr(ia_match, CSR_IA_MATCH);	// clear ia_match
			write_csr(0, CSR_IV_MATCH);		// clear iv_match
			task_thread_info(current)->pcb.match_ctl &= ~(0x1 << 4);
			task_thread_info(current)->pcb.ia_match &= ~((0x1UL << IV_MATCH_EN_S) | (0x1UL << IV_PM_EN_S));
			task_thread_info(current)->pcb.iv_match = 0;
		}
		if (mmcsr == MMCSR__IDA_MATCH) {
			info.si_errno = 6;
			write_csr(0, CSR_IDA_MATCH);		// clear ida_match
			task_thread_info(current)->pcb.match_ctl &= ~(0x1 << 5);
			task_thread_info(current)->pcb.ida_match = 0;
		}
		pr_notice("do_page_fault: want to send SIGTRAP, pid = %d\n", current->pid);
		force_sig_info(&info);
		return 1;
	}

	return 0;
}

/*
 *pcb->match_ctl:
 * [0] DA_MATCH
 * [1] DV_MATCH
 * [2] DAV_MATCH
 * [3] IA_MATCH
 * [4] IV_MATCH
 * [5] IDA_MATCH
 * [8:9] match_ctl_mode
 *
 */
#define DA_MATCH	0x1
#define DV_MATCH	0x2
#define DAV_MATCH	0x4
#define IA_MATCH	0x8
#define IV_MATCH	0x10
#define IDA_MATCH	0x20

void restore_da_match_after_sched(void)
{
	unsigned long match_ctl_mode;
	unsigned long match_ctl;
	struct pcb_struct *pcb = &task_thread_info(current)->pcb;
	unsigned long vpn, upn;

	if (!pcb->match_ctl)
		return;
	pr_info("Restroe MATCH status, pid: %d\n", current->pid);

	if (pcb->match_ctl & DA_MATCH) {
		write_csr(pcb->da_match, CSR_DA_MATCH);
		write_csr(pcb->da_mask, CSR_DA_MASK);
		match_ctl_mode = (pcb->match_ctl >> 8) & 0x3;
		match_ctl = read_csr(CSR_DC_CTLP);
		match_ctl &= ~((0x1UL << 3) | (0x3UL << DA_MATCH_EN_S) | (0x1UL << DV_MATCH_EN_S) | (0x1UL << DAV_MATCH_EN_S));
		match_ctl |= (match_ctl_mode << DA_MATCH_EN_S) | (0x1UL << DPM_MATCH_EN_S) | (0x3UL << DPM_MATCH);
		write_csr(match_ctl, CSR_DC_CTLP);
		pr_info("da_match:%#lx da_mask:%#lx match_ctl:%#lx\n", pcb->da_match, pcb->da_mask, match_ctl);
	}

	if (pcb->match_ctl & DV_MATCH) {
		write_csr(pcb->dv_match, CSR_DV_MATCH);
		write_csr(pcb->dv_mask, CSR_DV_MASK);
		match_ctl = read_csr(CSR_DC_CTLP);
		match_ctl &= ~((0x1UL << 3) | (0x3UL << DA_MATCH_EN_S) | (0x1UL << DV_MATCH_EN_S) | (0x1UL << DAV_MATCH_EN_S));
		match_ctl |= (0x1UL << DV_MATCH_EN_S) | (0x1UL << DPM_MATCH_EN_S) | (0x3UL << DPM_MATCH);
		write_csr(match_ctl, CSR_DC_CTLP);
		pr_info("dv_match:%#lx dv_mask:%#lx match_ctl:%#lx\n", pcb->dv_match, pcb->dv_mask, match_ctl);
	}

	if (pcb->match_ctl & DAV_MATCH) {
		write_csr(pcb->da_match, CSR_DA_MATCH);
		write_csr(pcb->da_mask, CSR_DA_MASK);
		write_csr(pcb->dv_match, CSR_DV_MATCH);
		write_csr(pcb->dv_mask, CSR_DV_MASK);
		write_csr(0xfffffffff, CSR_DA_MATCH_MODE);
		match_ctl_mode = (pcb->match_ctl >> 8) & 0x3;
		match_ctl = read_csr(CSR_DC_CTLP);
		match_ctl &= ~((0x3UL << DA_MATCH_EN_S) | (0x1UL << DV_MATCH_EN_S) | (0x1UL << DAV_MATCH_EN_S));
		match_ctl |= (match_ctl_mode << DA_MATCH_EN_S) | (0x1UL << DV_MATCH_EN_S)
				| (0x1UL << DAV_MATCH_EN_S) | (0x1UL << DPM_MATCH_EN_S)
				| (0x3UL << DPM_MATCH);
		write_csr(match_ctl, CSR_DC_CTLP);
		pr_info("da_match:%#lx da_mask:%#lx dv_match:%#lx dv_mask:%#lx match_ctl:%#lx\n",
				pcb->da_match, pcb->da_mask, pcb->dv_match, pcb->dv_mask, match_ctl);
	}

	if (pcb->match_ctl & IA_MATCH) {
		pcb->ia_match |= (0x1UL << IA_MATCH_EN_S) | 0x3;
		pcb->ia_mask |= 0x3;
		write_csr(pcb->ia_match, CSR_IA_MATCH);
		write_csr(pcb->ia_mask, CSR_IA_MASK);
		vpn = read_csr(CSR_VPCR) >> 44;
		vpn &= 0x3ff;
		upn = read_csr(CSR_UPCR);
		upn &= 0x3ff;
		write_csr(((0x3ff << 18) | vpn), CSR_IA_VPNMATCH);
		write_csr(((0x3ff << 18) | upn), CSR_IA_UPNMATCH);
		pr_info("ia_match:%#lx ia_mask:%#lx\n", pcb->ia_match, pcb->ia_mask);
	}
	if (pcb->match_ctl & IV_MATCH) {
		pcb->ia_match |= (0x1UL << IV_MATCH_EN_S) | (0x1UL << IV_PM_EN_S) | 0x3;
		write_csr(pcb->ia_match, CSR_IA_MATCH);
		write_csr(pcb->iv_match, CSR_IV_MATCH);
		pr_info("ia_match:%#lx iv_match:%#lx\n", pcb->ia_match, pcb->iv_match);
	}
	if (pcb->match_ctl & IDA_MATCH) {
		pcb->ida_match |= (0x1UL << IDA_MATCH_EN_S) | 0x3;
		pcb->ida_mask |= 0x3;
		write_csr(pcb->ida_match, CSR_IDA_MATCH);
		write_csr(pcb->ida_mask, CSR_IDA_MASK);
		pr_info("ida_match:%#lx ida_mask:%#lx\n", pcb->ida_match, pcb->ida_mask);
	}
}
#endif

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define GPR_OFFSET_NAME(r) {					\
	.name = "r" #r,						\
	.offset = offsetof(struct pt_regs, regs[r])		\
}

#define REG_OFFSET_NAME(r) {					\
	.name = #r,						\
	.offset = offsetof(struct pt_regs, r)			\
}

#define REG_OFFSET_END {					\
	.name = NULL,						\
	.offset = 0						\
}

static const struct pt_regs_offset regoffset_table[] = {
	GPR_OFFSET_NAME(0),
	GPR_OFFSET_NAME(1),
	GPR_OFFSET_NAME(2),
	GPR_OFFSET_NAME(3),
	GPR_OFFSET_NAME(4),
	GPR_OFFSET_NAME(5),
	GPR_OFFSET_NAME(6),
	GPR_OFFSET_NAME(7),
	GPR_OFFSET_NAME(8),
	GPR_OFFSET_NAME(9),
	GPR_OFFSET_NAME(10),
	GPR_OFFSET_NAME(11),
	GPR_OFFSET_NAME(12),
	GPR_OFFSET_NAME(13),
	GPR_OFFSET_NAME(14),
	GPR_OFFSET_NAME(15),
	GPR_OFFSET_NAME(16),
	GPR_OFFSET_NAME(17),
	GPR_OFFSET_NAME(18),
	GPR_OFFSET_NAME(19),
	GPR_OFFSET_NAME(20),
	GPR_OFFSET_NAME(21),
	GPR_OFFSET_NAME(22),
	GPR_OFFSET_NAME(23),
	GPR_OFFSET_NAME(24),
	GPR_OFFSET_NAME(25),
	GPR_OFFSET_NAME(26),
	GPR_OFFSET_NAME(27),
	GPR_OFFSET_NAME(28),
	GPR_OFFSET_NAME(29),
	GPR_OFFSET_NAME(30),
	REG_OFFSET_NAME(pc),
	REG_OFFSET_NAME(ps),
	REG_OFFSET_END,
};

/**
 * regs_query_register_offset() - query register offset from its name
 * @name:       the name of a register
 *
 * regs_query_register_offset() returns the offset of a register in struct
 * pt_regs from its name. If the name is invalid, this returns -EINVAL;
 */
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;

	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

static int regs_within_kernel_stack(struct pt_regs *regs, unsigned long addr)
{
	unsigned long ksp = kernel_stack_pointer(regs);

	return (addr & ~(THREAD_SIZE - 1)) == (ksp & ~(THREAD_SIZE - 1));
}

/**
 * regs_get_kernel_stack_nth() - get Nth entry of the stack
 * @regs:pt_regs which contains kernel stack pointer.
 * @n:stack entry number.
 *
 * regs_get_kernel_stack_nth() returns @n th entry of the kernel stack which
 * is specifined by @regs. If the @n th entry is NOT in the kernel stack,
 * this returns 0.
 */
unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs, unsigned int n)
{
	unsigned long addr;

	addr = kernel_stack_pointer(regs) + n * sizeof(long);
	if (!regs_within_kernel_stack(regs, addr))
		return 0;
	return *(unsigned long *)addr;
}
