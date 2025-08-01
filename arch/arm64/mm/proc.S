/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/mm/proc.S
 *
 * Copyright (C) 2001 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/pgtable.h>
#include <linux/cfi_types.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/asm_pointer_auth.h>
#include <asm/hwcap.h>
#include <asm/kernel-pgtable.h>
#include <asm/pgtable-hwdef.h>
#include <asm/cpufeature.h>
#include <asm/alternative.h>
#include <asm/smp.h>
#include <asm/sysreg.h>

#ifdef CONFIG_ARM64_64K_PAGES
#define TCR_TG_FLAGS	TCR_TG0_64K | TCR_TG1_64K
#elif defined(CONFIG_ARM64_16K_PAGES)
#define TCR_TG_FLAGS	TCR_TG0_16K | TCR_TG1_16K
#else /* CONFIG_ARM64_4K_PAGES */
#define TCR_TG_FLAGS	TCR_TG0_4K | TCR_TG1_4K
#endif

#ifdef CONFIG_RANDOMIZE_BASE
#define TCR_KASLR_FLAGS	TCR_NFD1
#else
#define TCR_KASLR_FLAGS	0
#endif

/* PTWs cacheable, inner/outer WBWA */
#define TCR_CACHE_FLAGS	TCR_IRGN_WBWA | TCR_ORGN_WBWA

#ifdef CONFIG_KASAN_SW_TAGS
#define TCR_KASAN_SW_FLAGS TCR_TBI1 | TCR_TBID1
#else
#define TCR_KASAN_SW_FLAGS 0
#endif

#ifdef CONFIG_KASAN_HW_TAGS
#define TCR_MTE_FLAGS TCR_TCMA1 | TCR_TBI1 | TCR_TBID1
#elif defined(CONFIG_ARM64_MTE)
/*
 * The mte_zero_clear_page_tags() implementation uses DC GZVA, which relies on
 * TBI being enabled at EL1.
 */
#define TCR_MTE_FLAGS TCR_TBI1 | TCR_TBID1
#else
#define TCR_MTE_FLAGS 0
#endif

/*
 * Default MAIR_EL1. MT_NORMAL_TAGGED is initially mapped as Normal memory and
 * changed during mte_cpu_setup to Normal Tagged if the system supports MTE.
 */
#define MAIR_EL1_SET							\
	(MAIR_ATTRIDX(MAIR_ATTR_DEVICE_nGnRnE, MT_DEVICE_nGnRnE) |	\
	 MAIR_ATTRIDX(MAIR_ATTR_DEVICE_nGnRE, MT_DEVICE_nGnRE) |	\
	 MAIR_ATTRIDX(MAIR_ATTR_NORMAL_NC, MT_NORMAL_NC) |		\
	 MAIR_ATTRIDX(MAIR_ATTR_NORMAL, MT_NORMAL) |			\
	 MAIR_ATTRIDX(MAIR_ATTR_NORMAL, MT_NORMAL_TAGGED))

#ifdef CONFIG_CPU_PM
/**
 * cpu_do_suspend - save CPU registers context
 *
 * x0: virtual address of context pointer
 *
 * This must be kept in sync with struct cpu_suspend_ctx in <asm/suspend.h>.
 */
SYM_FUNC_START(cpu_do_suspend)
	mrs	x2, tpidr_el0
	mrs	x3, tpidrro_el0
	mrs	x4, contextidr_el1
	mrs	x5, osdlr_el1
	mrs	x6, cpacr_el1
	mrs	x7, tcr_el1
	mrs	x8, vbar_el1
	mrs	x9, mdscr_el1
	mrs	x10, oslsr_el1
	mrs	x11, sctlr_el1
	get_this_cpu_offset x12
	mrs	x13, sp_el0
	stp	x2, x3, [x0]
	stp	x4, x5, [x0, #16]
	stp	x6, x7, [x0, #32]
	stp	x8, x9, [x0, #48]
	stp	x10, x11, [x0, #64]
	stp	x12, x13, [x0, #80]
	/*
	 * Save x18 as it may be used as a platform register, e.g. by shadow
	 * call stack.
	 */
	str	x18, [x0, #96]
	ret
SYM_FUNC_END(cpu_do_suspend)

/**
 * cpu_do_resume - restore CPU register context
 *
 * x0: Address of context pointer
 */
SYM_FUNC_START(cpu_do_resume)
	ldp	x2, x3, [x0]
	ldp	x4, x5, [x0, #16]
	ldp	x6, x8, [x0, #32]
	ldp	x9, x10, [x0, #48]
	ldp	x11, x12, [x0, #64]
	ldp	x13, x14, [x0, #80]
	/*
	 * Restore x18, as it may be used as a platform register, and clear
	 * the buffer to minimize the risk of exposure when used for shadow
	 * call stack.
	 */
	ldr	x18, [x0, #96]
	str	xzr, [x0, #96]
	msr	tpidr_el0, x2
	msr	tpidrro_el0, x3
	msr	contextidr_el1, x4
	msr	cpacr_el1, x6

	/* Don't change t0sz here, mask those bits when restoring */
	mrs	x7, tcr_el1
	bfi	x8, x7, TCR_T0SZ_OFFSET, TCR_TxSZ_WIDTH

	msr	tcr_el1, x8
	msr	vbar_el1, x9
	msr	mdscr_el1, x10

	msr	sctlr_el1, x12
	set_this_cpu_offset x13
	msr	sp_el0, x14
	/*
	 * Restore oslsr_el1 by writing oslar_el1
	 */
	msr	osdlr_el1, x5
	ubfx	x11, x11, #1, #1
	msr	oslar_el1, x11
	reset_pmuserenr_el0 x0			// Disable PMU access from EL0
	reset_amuserenr_el0 x0			// Disable AMU access from EL0

alternative_if ARM64_HAS_RAS_EXTN
	msr_s	SYS_DISR_EL1, xzr
alternative_else_nop_endif

	ptrauth_keys_install_kernel_nosync x14, x1, x2, x3
	isb
	ret
SYM_FUNC_END(cpu_do_resume)
#endif

	.pushsection ".idmap.text", "a"

.macro	__idmap_cpu_set_reserved_ttbr1, tmp1, tmp2
	adrp	\tmp1, reserved_pg_dir
	phys_to_ttbr \tmp2, \tmp1
	offset_ttbr1 \tmp2, \tmp1
	msr	ttbr1_el1, \tmp2
	isb
	tlbi	vmalle1
	dsb	nsh
	isb
.endm

/*
 * void idmap_cpu_replace_ttbr1(phys_addr_t ttbr1)
 *
 * This is the low-level counterpart to cpu_replace_ttbr1, and should not be
 * called by anything else. It can only be executed from a TTBR0 mapping.
 */
SYM_TYPED_FUNC_START(idmap_cpu_replace_ttbr1)
	__idmap_cpu_set_reserved_ttbr1 x1, x3

	offset_ttbr1 x0, x3
	msr	ttbr1_el1, x0
	isb

	ret
SYM_FUNC_END(idmap_cpu_replace_ttbr1)
SYM_FUNC_ALIAS(__pi_idmap_cpu_replace_ttbr1, idmap_cpu_replace_ttbr1)
	.popsection

#ifdef CONFIG_UNMAP_KERNEL_AT_EL0

#define KPTI_NG_PTE_FLAGS	(PTE_ATTRINDX(MT_NORMAL) | PTE_TYPE_PAGE | \
				 PTE_AF | PTE_SHARED | PTE_UXN | PTE_WRITE)

	.pushsection ".idmap.text", "a"

	.macro	pte_to_phys, phys, pte
	and	\phys, \pte, #PTE_ADDR_LOW
#ifdef CONFIG_ARM64_PA_BITS_52
	and	\pte, \pte, #PTE_ADDR_HIGH
	orr	\phys, \phys, \pte, lsl #PTE_ADDR_HIGH_SHIFT
#endif
	.endm

	.macro	kpti_mk_tbl_ng, type, num_entries
	add	end_\type\()p, cur_\type\()p, #\num_entries * 8
.Ldo_\type:
	ldr	\type, [cur_\type\()p], #8	// Load the entry and advance
	tbz	\type, #0, .Lnext_\type		// Skip invalid and
	tbnz	\type, #11, .Lnext_\type	// non-global entries
	orr	\type, \type, #PTE_NG		// Same bit for blocks and pages
	str	\type, [cur_\type\()p, #-8]	// Update the entry
	.ifnc	\type, pte
	tbnz	\type, #1, .Lderef_\type
	.endif
.Lnext_\type:
	cmp	cur_\type\()p, end_\type\()p
	b.ne	.Ldo_\type
	.endm

	/*
	 * Dereference the current table entry and map it into the temporary
	 * fixmap slot associated with the current level.
	 */
	.macro	kpti_map_pgtbl, type, level
	str	xzr, [temp_pte, #8 * (\level + 2)]	// break before make
	dsb	nshst
	add	pte, temp_pte, #PAGE_SIZE * (\level + 2)
	lsr	pte, pte, #12
	tlbi	vaae1, pte
	dsb	nsh
	isb

	phys_to_pte pte, cur_\type\()p
	add	cur_\type\()p, temp_pte, #PAGE_SIZE * (\level + 2)
	orr	pte, pte, pte_flags
	str	pte, [temp_pte, #8 * (\level + 2)]
	dsb	nshst
	.endm

/*
 * void __kpti_install_ng_mappings(int cpu, int num_secondaries, phys_addr_t temp_pgd,
 *				   unsigned long temp_pte_va)
 *
 * Called exactly once from stop_machine context by each CPU found during boot.
 */
	.pushsection	".data", "aw", %progbits
SYM_DATA(__idmap_kpti_flag, .long 1)
	.popsection

SYM_TYPED_FUNC_START(idmap_kpti_install_ng_mappings)
	cpu		.req	w0
	temp_pte	.req	x0
	num_cpus	.req	w1
	pte_flags	.req	x1
	temp_pgd_phys	.req	x2
	swapper_ttb	.req	x3
	flag_ptr	.req	x4
	cur_pgdp	.req	x5
	end_pgdp	.req	x6
	pgd		.req	x7
	cur_pudp	.req	x8
	end_pudp	.req	x9
	cur_pmdp	.req	x11
	end_pmdp	.req	x12
	cur_ptep	.req	x14
	end_ptep	.req	x15
	pte		.req	x16
	valid		.req	x17
	cur_p4dp	.req	x19
	end_p4dp	.req	x20

	mov	x5, x3				// preserve temp_pte arg
	mrs	swapper_ttb, ttbr1_el1
	adr_l	flag_ptr, __idmap_kpti_flag

	cbnz	cpu, __idmap_kpti_secondary

#if CONFIG_PGTABLE_LEVELS > 4
	stp	x29, x30, [sp, #-32]!
	mov	x29, sp
	stp	x19, x20, [sp, #16]
#endif

	/* We're the boot CPU. Wait for the others to catch up */
	sevl
1:	wfe
	ldaxr	w17, [flag_ptr]
	eor	w17, w17, num_cpus
	cbnz	w17, 1b

	/* Switch to the temporary page tables on this CPU only */
	__idmap_cpu_set_reserved_ttbr1 x8, x9
	offset_ttbr1 temp_pgd_phys, x8
	msr	ttbr1_el1, temp_pgd_phys
	isb

	mov	temp_pte, x5
	mov_q	pte_flags, KPTI_NG_PTE_FLAGS

	/* Everybody is enjoying the idmap, so we can rewrite swapper. */

#ifdef CONFIG_ARM64_LPA2
	/*
	 * If LPA2 support is configured, but 52-bit virtual addressing is not
	 * enabled at runtime, we will fall back to one level of paging less,
	 * and so we have to walk swapper_pg_dir as if we dereferenced its
	 * address from a PGD level entry, and terminate the PGD level loop
	 * right after.
	 */
	adrp	pgd, swapper_pg_dir	// walk &swapper_pg_dir at the next level
	mov	cur_pgdp, end_pgdp	// must be equal to terminate the PGD loop
alternative_if_not ARM64_HAS_VA52
	b	.Lderef_pgd		// skip to the next level
alternative_else_nop_endif
	/*
	 * LPA2 based 52-bit virtual addressing requires 52-bit physical
	 * addressing to be enabled as well. In this case, the shareability
	 * bits are repurposed as physical address bits, and should not be
	 * set in pte_flags.
	 */
	bic	pte_flags, pte_flags, #PTE_SHARED
#endif

	/* PGD */
	adrp		cur_pgdp, swapper_pg_dir
	kpti_map_pgtbl	pgd, -1
	kpti_mk_tbl_ng	pgd, PTRS_PER_PGD

	/* Ensure all the updated entries are visible to secondary CPUs */
	dsb	ishst

	/* We're done: fire up swapper_pg_dir again */
	__idmap_cpu_set_reserved_ttbr1 x8, x9
	msr	ttbr1_el1, swapper_ttb
	isb

	/* Set the flag to zero to indicate that we're all done */
	str	wzr, [flag_ptr]
#if CONFIG_PGTABLE_LEVELS > 4
	ldp	x19, x20, [sp, #16]
	ldp	x29, x30, [sp], #32
#endif
	ret

.Lderef_pgd:
	/* P4D */
	.if		CONFIG_PGTABLE_LEVELS > 4
	p4d		.req	x30
	pte_to_phys	cur_p4dp, pgd
	kpti_map_pgtbl	p4d, 0
	kpti_mk_tbl_ng	p4d, PTRS_PER_P4D
	b		.Lnext_pgd
	.else		/* CONFIG_PGTABLE_LEVELS <= 4 */
	p4d		.req	pgd
	.set		.Lnext_p4d, .Lnext_pgd
	.endif

.Lderef_p4d:
	/* PUD */
	.if		CONFIG_PGTABLE_LEVELS > 3
	pud		.req	x10
	pte_to_phys	cur_pudp, p4d
	kpti_map_pgtbl	pud, 1
	kpti_mk_tbl_ng	pud, PTRS_PER_PUD
	b		.Lnext_p4d
	.else		/* CONFIG_PGTABLE_LEVELS <= 3 */
	pud		.req	pgd
	.set		.Lnext_pud, .Lnext_pgd
	.endif

.Lderef_pud:
	/* PMD */
	.if		CONFIG_PGTABLE_LEVELS > 2
	pmd		.req	x13
	pte_to_phys	cur_pmdp, pud
	kpti_map_pgtbl	pmd, 2
	kpti_mk_tbl_ng	pmd, PTRS_PER_PMD
	b		.Lnext_pud
	.else		/* CONFIG_PGTABLE_LEVELS <= 2 */
	pmd		.req	pgd
	.set		.Lnext_pmd, .Lnext_pgd
	.endif

.Lderef_pmd:
	/* PTE */
	pte_to_phys	cur_ptep, pmd
	kpti_map_pgtbl	pte, 3
	kpti_mk_tbl_ng	pte, PTRS_PER_PTE
	b		.Lnext_pmd

	.unreq	cpu
	.unreq	temp_pte
	.unreq	num_cpus
	.unreq	pte_flags
	.unreq	temp_pgd_phys
	.unreq	cur_pgdp
	.unreq	end_pgdp
	.unreq	pgd
	.unreq	cur_pudp
	.unreq	end_pudp
	.unreq	pud
	.unreq	cur_pmdp
	.unreq	end_pmdp
	.unreq	pmd
	.unreq	cur_ptep
	.unreq	end_ptep
	.unreq	pte
	.unreq	valid
	.unreq	cur_p4dp
	.unreq	end_p4dp
	.unreq	p4d

	/* Secondary CPUs end up here */
__idmap_kpti_secondary:
	/* Uninstall swapper before surgery begins */
	__idmap_cpu_set_reserved_ttbr1 x16, x17

	/* Increment the flag to let the boot CPU we're ready */
1:	ldxr	w16, [flag_ptr]
	add	w16, w16, #1
	stxr	w17, w16, [flag_ptr]
	cbnz	w17, 1b

	/* Wait for the boot CPU to finish messing around with swapper */
	sevl
1:	wfe
	ldxr	w16, [flag_ptr]
	cbnz	w16, 1b

	/* All done, act like nothing happened */
	msr	ttbr1_el1, swapper_ttb
	isb
	ret

	.unreq	swapper_ttb
	.unreq	flag_ptr
SYM_FUNC_END(idmap_kpti_install_ng_mappings)
	.popsection
#endif

/*
 *	__cpu_setup
 *
 *	Initialise the processor for turning the MMU on.
 *
 * Output:
 *	Return in x0 the value of the SCTLR_EL1 register.
 */
	.pushsection ".idmap.text", "a"
SYM_FUNC_START(__cpu_setup)
	tlbi	vmalle1				// Invalidate local TLB
	dsb	nsh

	msr	cpacr_el1, xzr			// Reset cpacr_el1
	mov	x1, MDSCR_EL1_TDCC		// Reset mdscr_el1 and disable
	msr	mdscr_el1, x1			// access to the DCC from EL0
	reset_pmuserenr_el0 x1			// Disable PMU access from EL0
	reset_amuserenr_el0 x1			// Disable AMU access from EL0

	/*
	 * Default values for VMSA control registers. These will be adjusted
	 * below depending on detected CPU features.
	 */
	mair	.req	x17
	tcr	.req	x16
	tcr2	.req	x15
	mov_q	mair, MAIR_EL1_SET
	mov_q	tcr, TCR_T0SZ(IDMAP_VA_BITS) | TCR_T1SZ(VA_BITS_MIN) | TCR_CACHE_FLAGS | \
		     TCR_SHARED | TCR_TG_FLAGS | TCR_KASLR_FLAGS | TCR_ASID16 | \
		     TCR_TBI0 | TCR_A1 | TCR_KASAN_SW_FLAGS | TCR_MTE_FLAGS
	mov	tcr2, xzr

	tcr_clear_errata_bits tcr, x9, x5

#ifdef CONFIG_ARM64_VA_BITS_52
	mov		x9, #64 - VA_BITS
alternative_if ARM64_HAS_VA52
	tcr_set_t1sz	tcr, x9
#ifdef CONFIG_ARM64_LPA2
	orr		tcr, tcr, #TCR_DS
#endif
alternative_else_nop_endif
#endif

	/*
	 * Set the IPS bits in TCR_EL1.
	 */
	tcr_compute_pa_size tcr, #TCR_IPS_SHIFT, x5, x6
#ifdef CONFIG_ARM64_HW_AFDBM
	/*
	 * Enable hardware update of the Access Flags bit.
	 * Hardware dirty bit management is enabled later,
	 * via capabilities.
	 */
	mrs	x9, ID_AA64MMFR1_EL1
	ubfx	x9, x9, ID_AA64MMFR1_EL1_HAFDBS_SHIFT, #4
	cbz	x9, 1f
	orr	tcr, tcr, #TCR_HA		// hardware Access flag update
#ifdef CONFIG_ARM64_HAFT
	cmp	x9, ID_AA64MMFR1_EL1_HAFDBS_HAFT
	b.lt	1f
	orr	tcr2, tcr2, TCR2_EL1_HAFT
#endif /* CONFIG_ARM64_HAFT */
1:
#endif	/* CONFIG_ARM64_HW_AFDBM */
	msr	mair_el1, mair
	msr	tcr_el1, tcr

	mrs_s	x1, SYS_ID_AA64MMFR3_EL1
	ubfx	x1, x1, #ID_AA64MMFR3_EL1_S1PIE_SHIFT, #4
	cbz	x1, .Lskip_indirection

	mov_q	x0, PIE_E0_ASM
	msr	REG_PIRE0_EL1, x0
	mov_q	x0, PIE_E1_ASM
	msr	REG_PIR_EL1, x0

	orr	tcr2, tcr2, TCR2_EL1_PIE

.Lskip_indirection:

	mrs_s	x1, SYS_ID_AA64MMFR3_EL1
	ubfx	x1, x1, #ID_AA64MMFR3_EL1_TCRX_SHIFT, #4
	cbz	x1, 1f
	msr	REG_TCR2_EL1, tcr2
1:

	/*
	 * Prepare SCTLR
	 */
	mov_q	x0, INIT_SCTLR_EL1_MMU_ON
	ret					// return to head.S

	.unreq	mair
	.unreq	tcr
	.unreq	tcr2
SYM_FUNC_END(__cpu_setup)
