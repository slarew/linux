/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/processor.h
 *
 * Copyright (C) 1995-1999 Russell King
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_PROCESSOR_H
#define __ASM_PROCESSOR_H

/*
 * On arm64 systems, unaligned accesses by the CPU are cheap, and so there is
 * no point in shifting all network buffers by 2 bytes just to make some IP
 * header fields appear aligned in memory, potentially sacrificing some DMA
 * performance on some platforms.
 */
#define NET_IP_ALIGN	0

#define MTE_CTRL_GCR_USER_EXCL_SHIFT	0
#define MTE_CTRL_GCR_USER_EXCL_MASK	0xffff

#define MTE_CTRL_TCF_SYNC		(1UL << 16)
#define MTE_CTRL_TCF_ASYNC		(1UL << 17)
#define MTE_CTRL_TCF_ASYMM		(1UL << 18)

#define MTE_CTRL_STORE_ONLY		(1UL << 19)

#ifndef __ASSEMBLY__

#include <linux/build_bug.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/thread_info.h>

#include <vdso/processor.h>

#include <asm/alternative.h>
#include <asm/cpufeature.h>
#include <asm/hw_breakpoint.h>
#include <asm/kasan.h>
#include <asm/lse.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pointer_auth.h>
#include <asm/ptrace.h>
#include <asm/spectre.h>
#include <asm/types.h>

/*
 * TASK_SIZE - the maximum size of a user space task.
 * TASK_UNMAPPED_BASE - the lower boundary of the mmap VM area.
 */

#define DEFAULT_MAP_WINDOW_64	(UL(1) << VA_BITS_MIN)
#define TASK_SIZE_64		(UL(1) << vabits_actual)
#define TASK_SIZE_MAX		(UL(1) << VA_BITS)

#ifdef CONFIG_COMPAT
#if defined(CONFIG_ARM64_64K_PAGES) && defined(CONFIG_KUSER_HELPERS)
/*
 * With CONFIG_ARM64_64K_PAGES enabled, the last page is occupied
 * by the compat vectors page.
 */
#define TASK_SIZE_32		UL(0x100000000)
#else
#define TASK_SIZE_32		(UL(0x100000000) - PAGE_SIZE)
#endif /* CONFIG_ARM64_64K_PAGES */
#define TASK_SIZE		(test_thread_flag(TIF_32BIT) ? \
				TASK_SIZE_32 : TASK_SIZE_64)
#define TASK_SIZE_OF(tsk)	(test_tsk_thread_flag(tsk, TIF_32BIT) ? \
				TASK_SIZE_32 : TASK_SIZE_64)
#define DEFAULT_MAP_WINDOW	(test_thread_flag(TIF_32BIT) ? \
				TASK_SIZE_32 : DEFAULT_MAP_WINDOW_64)
#else
#define TASK_SIZE		TASK_SIZE_64
#define DEFAULT_MAP_WINDOW	DEFAULT_MAP_WINDOW_64
#endif /* CONFIG_COMPAT */

#ifdef CONFIG_ARM64_FORCE_52BIT
#define STACK_TOP_MAX		TASK_SIZE_64
#define TASK_UNMAPPED_BASE	(PAGE_ALIGN(TASK_SIZE / 4))
#else
#define STACK_TOP_MAX		DEFAULT_MAP_WINDOW_64
#define TASK_UNMAPPED_BASE	(PAGE_ALIGN(DEFAULT_MAP_WINDOW / 4))
#endif /* CONFIG_ARM64_FORCE_52BIT */

#ifdef CONFIG_COMPAT
#define AARCH32_VECTORS_BASE	0xffff0000
#define STACK_TOP		(test_thread_flag(TIF_32BIT) ? \
				AARCH32_VECTORS_BASE : STACK_TOP_MAX)
#else
#define STACK_TOP		STACK_TOP_MAX
#endif /* CONFIG_COMPAT */

#ifndef CONFIG_ARM64_FORCE_52BIT
#define arch_get_mmap_end(addr, len, flags) \
		(((addr) > DEFAULT_MAP_WINDOW) ? TASK_SIZE : DEFAULT_MAP_WINDOW)

#define arch_get_mmap_base(addr, base) ((addr > DEFAULT_MAP_WINDOW) ? \
					base + TASK_SIZE - DEFAULT_MAP_WINDOW :\
					base)
#endif /* CONFIG_ARM64_FORCE_52BIT */

extern phys_addr_t arm64_dma_phys_limit;
#define ARCH_LOW_ADDRESS_LIMIT	(arm64_dma_phys_limit - 1)

struct debug_info {
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	/* Have we suspended stepping by a debugger? */
	int			suspended_step;
	/* Allow breakpoints and watchpoints to be disabled for this thread. */
	int			bps_disabled;
	int			wps_disabled;
	/* Hardware breakpoints pinned to this task. */
	struct perf_event	*hbp_break[ARM_MAX_BRP];
	struct perf_event	*hbp_watch[ARM_MAX_WRP];
#endif
};

enum vec_type {
	ARM64_VEC_SVE = 0,
	ARM64_VEC_SME,
	ARM64_VEC_MAX,
};

enum fp_type {
	FP_STATE_CURRENT,	/* Save based on current task state. */
	FP_STATE_FPSIMD,
	FP_STATE_SVE,
};

struct cpu_context {
	unsigned long x19;
	unsigned long x20;
	unsigned long x21;
	unsigned long x22;
	unsigned long x23;
	unsigned long x24;
	unsigned long x25;
	unsigned long x26;
	unsigned long x27;
	unsigned long x28;
	unsigned long fp;
	unsigned long sp;
	unsigned long pc;
};

struct thread_struct {
	struct cpu_context	cpu_context;	/* cpu context */

	/*
	 * Whitelisted fields for hardened usercopy:
	 * Maintainers must ensure manually that this contains no
	 * implicit padding.
	 */
	struct {
		unsigned long	tp_value;	/* TLS register */
		unsigned long	tp2_value;
		u64		fpmr;
		unsigned long	pad;
		struct user_fpsimd_state fpsimd_state;
	} uw;

	enum fp_type		fp_type;	/* registers FPSIMD or SVE? */
	unsigned int		fpsimd_cpu;
	void			*sve_state;	/* SVE registers, if any */
	void			*sme_state;	/* ZA and ZT state, if any */
	unsigned int		vl[ARM64_VEC_MAX];	/* vector length */
	unsigned int		vl_onexec[ARM64_VEC_MAX]; /* vl after next exec */
	unsigned long		fault_address;	/* fault info */
	unsigned long		fault_code;	/* ESR_EL1 value */
	struct debug_info	debug;		/* debugging */

	struct user_fpsimd_state	kernel_fpsimd_state;
	unsigned int			kernel_fpsimd_cpu;
#ifdef CONFIG_ARM64_PTR_AUTH
	struct ptrauth_keys_user	keys_user;
#ifdef CONFIG_ARM64_PTR_AUTH_KERNEL
	struct ptrauth_keys_kernel	keys_kernel;
#endif
#endif
#ifdef CONFIG_ARM64_MTE
	u64			mte_ctrl;
#endif
	u64			sctlr_user;
	u64			svcr;
	u64			tpidr2_el0;
	u64			por_el0;
#ifdef CONFIG_ARM64_GCS
	unsigned int		gcs_el0_mode;
	unsigned int		gcs_el0_locked;
	u64			gcspr_el0;
	u64			gcs_base;
	u64			gcs_size;
#endif
};

static inline unsigned int thread_get_vl(struct thread_struct *thread,
					 enum vec_type type)
{
	return thread->vl[type];
}

static inline unsigned int thread_get_sve_vl(struct thread_struct *thread)
{
	return thread_get_vl(thread, ARM64_VEC_SVE);
}

static inline unsigned int thread_get_sme_vl(struct thread_struct *thread)
{
	return thread_get_vl(thread, ARM64_VEC_SME);
}

static inline unsigned int thread_get_cur_vl(struct thread_struct *thread)
{
	if (system_supports_sme() && (thread->svcr & SVCR_SM_MASK))
		return thread_get_sme_vl(thread);
	else
		return thread_get_sve_vl(thread);
}

unsigned int task_get_vl(const struct task_struct *task, enum vec_type type);
void task_set_vl(struct task_struct *task, enum vec_type type,
		 unsigned long vl);
void task_set_vl_onexec(struct task_struct *task, enum vec_type type,
			unsigned long vl);
unsigned int task_get_vl_onexec(const struct task_struct *task,
				enum vec_type type);

static inline unsigned int task_get_sve_vl(const struct task_struct *task)
{
	return task_get_vl(task, ARM64_VEC_SVE);
}

static inline unsigned int task_get_sme_vl(const struct task_struct *task)
{
	return task_get_vl(task, ARM64_VEC_SME);
}

static inline void task_set_sve_vl(struct task_struct *task, unsigned long vl)
{
	task_set_vl(task, ARM64_VEC_SVE, vl);
}

static inline unsigned int task_get_sve_vl_onexec(const struct task_struct *task)
{
	return task_get_vl_onexec(task, ARM64_VEC_SVE);
}

static inline void task_set_sve_vl_onexec(struct task_struct *task,
					  unsigned long vl)
{
	task_set_vl_onexec(task, ARM64_VEC_SVE, vl);
}

#define SCTLR_USER_MASK                                                        \
	(SCTLR_ELx_ENIA | SCTLR_ELx_ENIB | SCTLR_ELx_ENDA | SCTLR_ELx_ENDB |   \
	 SCTLR_EL1_TCF0_MASK)

static inline void arch_thread_struct_whitelist(unsigned long *offset,
						unsigned long *size)
{
	/* Verify that there is no padding among the whitelisted fields: */
	BUILD_BUG_ON(sizeof_field(struct thread_struct, uw) !=
		     sizeof_field(struct thread_struct, uw.tp_value) +
		     sizeof_field(struct thread_struct, uw.tp2_value) +
		     sizeof_field(struct thread_struct, uw.fpmr) +
		     sizeof_field(struct thread_struct, uw.pad) +
		     sizeof_field(struct thread_struct, uw.fpsimd_state));

	*offset = offsetof(struct thread_struct, uw);
	*size = sizeof_field(struct thread_struct, uw);
}

#ifdef CONFIG_COMPAT
#define task_user_tls(t)						\
({									\
	unsigned long *__tls;						\
	if (is_compat_thread(task_thread_info(t)))			\
		__tls = &(t)->thread.uw.tp2_value;			\
	else								\
		__tls = &(t)->thread.uw.tp_value;			\
	__tls;								\
 })
#else
#define task_user_tls(t)	(&(t)->thread.uw.tp_value)
#endif

/* Sync TPIDR_EL0 back to thread_struct for current */
void tls_preserve_current_state(void);

#define INIT_THREAD {				\
	.fpsimd_cpu = NR_CPUS,			\
}

static inline void start_thread_common(struct pt_regs *regs, unsigned long pc,
				       unsigned long pstate)
{
	/*
	 * Ensure all GPRs are zeroed, and initialize PC + PSTATE.
	 * The SP (or compat SP) will be initialized later.
	 */
	regs->user_regs = (struct user_pt_regs) {
		.pc = pc,
		.pstate = pstate,
	};

	/*
	 * To allow the syscalls:sys_exit_execve tracepoint we need to preserve
	 * syscallno, but do not need orig_x0 or the original GPRs.
	 */
	regs->orig_x0 = 0;

	/*
	 * An exec from a kernel thread won't have an existing PMR value.
	 */
	if (system_uses_irq_prio_masking())
		regs->pmr = GIC_PRIO_IRQON;

	/*
	 * The pt_regs::stackframe field must remain valid throughout this
	 * function as a stacktrace can be taken at any time. Any user or
	 * kernel task should have a valid final frame.
	 */
	WARN_ON_ONCE(regs->stackframe.record.fp != 0);
	WARN_ON_ONCE(regs->stackframe.record.lr != 0);
	WARN_ON_ONCE(regs->stackframe.type != FRAME_META_TYPE_FINAL);
}

static inline void start_thread(struct pt_regs *regs, unsigned long pc,
				unsigned long sp)
{
	start_thread_common(regs, pc, PSR_MODE_EL0t);
	spectre_v4_enable_task_mitigation(current);
	regs->sp = sp;
}

#ifdef CONFIG_COMPAT
static inline void compat_start_thread(struct pt_regs *regs, unsigned long pc,
				       unsigned long sp)
{
	unsigned long pstate = PSR_AA32_MODE_USR;
	if (pc & 1)
		pstate |= PSR_AA32_T_BIT;
	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		pstate |= PSR_AA32_E_BIT;

	start_thread_common(regs, pc, pstate);
	spectre_v4_enable_task_mitigation(current);
	regs->compat_sp = sp;
}
#endif

static __always_inline bool is_ttbr0_addr(unsigned long addr)
{
	/* entry assembly clears tags for TTBR0 addrs */
	return addr < TASK_SIZE;
}

static __always_inline bool is_ttbr1_addr(unsigned long addr)
{
	/* TTBR1 addresses may have a tag if KASAN_SW_TAGS is in use */
	return arch_kasan_reset_tag(addr) >= PAGE_OFFSET;
}

/* Forward declaration, a strange C thing */
struct task_struct;

unsigned long __get_wchan(struct task_struct *p);

void update_sctlr_el1(u64 sctlr);

/* Thread switching */
extern struct task_struct *cpu_switch_to(struct task_struct *prev,
					 struct task_struct *next);

#define task_pt_regs(p) \
	((struct pt_regs *)(THREAD_SIZE + task_stack_page(p)) - 1)

#define KSTK_EIP(tsk)	((unsigned long)task_pt_regs(tsk)->pc)
#define KSTK_ESP(tsk)	user_stack_pointer(task_pt_regs(tsk))

/*
 * Prefetching support
 */
#define ARCH_HAS_PREFETCH
static inline void prefetch(const void *ptr)
{
	asm volatile("prfm pldl1keep, %a0\n" : : "p" (ptr));
}

#define ARCH_HAS_PREFETCHW
static inline void prefetchw(const void *ptr)
{
	asm volatile("prfm pstl1keep, %a0\n" : : "p" (ptr));
}

extern unsigned long __ro_after_init signal_minsigstksz; /* sigframe size */
extern void __init minsigstksz_setup(void);

/*
 * Not at the top of the file due to a direct #include cycle between
 * <asm/fpsimd.h> and <asm/processor.h>.  Deferring this #include
 * ensures that contents of processor.h are visible to fpsimd.h even if
 * processor.h is included first.
 *
 * These prctl helpers are the only things in this file that require
 * fpsimd.h.  The core code expects them to be in this header.
 */
#include <asm/fpsimd.h>

/* Userspace interface for PR_S[MV]E_{SET,GET}_VL prctl()s: */
#define SVE_SET_VL(arg)	sve_set_current_vl(arg)
#define SVE_GET_VL()	sve_get_current_vl()
#define SME_SET_VL(arg)	sme_set_current_vl(arg)
#define SME_GET_VL()	sme_get_current_vl()

/* PR_PAC_RESET_KEYS prctl */
#define PAC_RESET_KEYS(tsk, arg)	ptrauth_prctl_reset_keys(tsk, arg)

/* PR_PAC_{SET,GET}_ENABLED_KEYS prctl */
#define PAC_SET_ENABLED_KEYS(tsk, keys, enabled)				\
	ptrauth_set_enabled_keys(tsk, keys, enabled)
#define PAC_GET_ENABLED_KEYS(tsk) ptrauth_get_enabled_keys(tsk)

#ifdef CONFIG_ARM64_TAGGED_ADDR_ABI
/* PR_{SET,GET}_TAGGED_ADDR_CTRL prctl */
long set_tagged_addr_ctrl(struct task_struct *task, unsigned long arg);
long get_tagged_addr_ctrl(struct task_struct *task);
#define SET_TAGGED_ADDR_CTRL(arg)	set_tagged_addr_ctrl(current, arg)
#define GET_TAGGED_ADDR_CTRL()		get_tagged_addr_ctrl(current)
#endif

int get_tsc_mode(unsigned long adr);
int set_tsc_mode(unsigned int val);
#define GET_TSC_CTL(adr)        get_tsc_mode((adr))
#define SET_TSC_CTL(val)        set_tsc_mode((val))

#endif /* __ASSEMBLY__ */
#endif /* __ASM_PROCESSOR_H */
