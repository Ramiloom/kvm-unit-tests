/*
 * processor control and status functions
 *
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <libcflat.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/esr.h>

static char *vector_names[] = {
	"el1t_sync",
	"el1t_irq",
	"el1t_fiq",
	"el1t_error",
	"el1h_sync",
	"el1h_irq",
	"el1h_fiq",
	"el1h_error",
	"el0_sync_64",
	"el0_irq_64",
	"el0_fiq_64",
	"el0_error_64",
	"el0_sync_32",
	"el0_irq_32",
	"el0_fiq_32",
	"el0_error_32",
};

static char *ec_names[EC_MAX] = {
	[ESR_EL1_EC_UNKNOWN]		= "UNKNOWN",
	[ESR_EL1_EC_WFI]		= "WFI",
	[ESR_EL1_EC_CP15_32]		= "CP15_32",
	[ESR_EL1_EC_CP15_64]		= "CP15_64",
	[ESR_EL1_EC_CP14_MR]		= "CP14_MR",
	[ESR_EL1_EC_CP14_LS]		= "CP14_LS",
	[ESR_EL1_EC_FP_ASIMD]		= "FP_ASMID",
	[ESR_EL1_EC_CP10_ID]		= "CP10_ID",
	[ESR_EL1_EC_CP14_64]		= "CP14_64",
	[ESR_EL1_EC_ILL_ISS]		= "ILL_ISS",
	[ESR_EL1_EC_SVC32]		= "SVC32",
	[ESR_EL1_EC_SVC64]		= "SVC64",
	[ESR_EL1_EC_SYS64]		= "SYS64",
	[ESR_EL1_EC_IABT_EL0]		= "IABT_EL0",
	[ESR_EL1_EC_IABT_EL1]		= "IABT_EL1",
	[ESR_EL1_EC_PC_ALIGN]		= "PC_ALIGN",
	[ESR_EL1_EC_DABT_EL0]		= "DABT_EL0",
	[ESR_EL1_EC_DABT_EL1]		= "DABT_EL1",
	[ESR_EL1_EC_SP_ALIGN]		= "SP_ALIGN",
	[ESR_EL1_EC_FP_EXC32]		= "FP_EXC32",
	[ESR_EL1_EC_FP_EXC64]		= "FP_EXC64",
	[ESR_EL1_EC_SERROR]		= "SERROR",
	[ESR_EL1_EC_BREAKPT_EL0]	= "BREAKPT_EL0",
	[ESR_EL1_EC_BREAKPT_EL1]	= "BREAKPT_EL1",
	[ESR_EL1_EC_SOFTSTP_EL0]	= "SOFTSTP_EL0",
	[ESR_EL1_EC_SOFTSTP_EL1]	= "SOFTSTP_EL1",
	[ESR_EL1_EC_WATCHPT_EL0]	= "WATCHPT_EL0",
	[ESR_EL1_EC_WATCHPT_EL1]	= "WATCHPT_EL1",
	[ESR_EL1_EC_BKPT32]		= "BKPT32",
	[ESR_EL1_EC_BRK64]		= "BRK64",
};

void show_regs(struct pt_regs *regs)
{
	int i;

	printf("pc : [<%016llx>] lr : [<%016llx>] pstate: %08llx\n",
			regs->pc, regs->regs[30], regs->pstate);
	printf("sp : %016llx\n", regs->sp);

	for (i = 29; i >= 0; --i) {
		printf("x%-2d: %016llx ", i, regs->regs[i]);
		if (i % 2 == 0)
			printf("\n");
	}
	printf("\n");
}

void *get_sp(void)
{
	register unsigned long sp asm("sp");
	return (void *)sp;
}

bool get_far(unsigned int esr, unsigned long *far)
{
	unsigned int ec = esr >> ESR_EL1_EC_SHIFT;

	asm volatile("mrs %0, far_el1": "=r" (*far));

	switch (ec) {
	case ESR_EL1_EC_IABT_EL0:
	case ESR_EL1_EC_IABT_EL1:
	case ESR_EL1_EC_PC_ALIGN:
	case ESR_EL1_EC_DABT_EL0:
	case ESR_EL1_EC_DABT_EL1:
	case ESR_EL1_EC_WATCHPT_EL0:
	case ESR_EL1_EC_WATCHPT_EL1:
		if ((esr & 0x3f /* DFSC */) != 0x10
				|| !(esr & 0x400 /* FnV */))
			return true;
	}
	return false;
}

static void bad_exception(enum vector v, struct pt_regs *regs,
			  unsigned int esr, bool bad_vector)
{
	unsigned long far;
	bool far_valid = get_far(esr, &far);
	unsigned int ec = esr >> ESR_EL1_EC_SHIFT;

	if (bad_vector) {
		if (v < VECTOR_MAX)
			printf("Unhandled vector %d (%s)\n", v,
					vector_names[v]);
		else
			printf("Got bad vector=%d\n", v);
	} else {
		if (ec_names[ec])
			printf("Unhandled exception ec=0x%x (%s)\n", ec,
					ec_names[ec]);
		else
			printf("Got bad ec=0x%x\n", ec);
	}

	printf("Vector: %d (%s)\n", v, vector_names[v]);
	printf("ESR_EL1: %8s%08lx, ec=0x%x (%s)\n", "", esr, ec, ec_names[ec]);
	printf("FAR_EL1: %016lx (%svalid)\n", far, far_valid ? "" : "not ");
	printf("Exception frame registers:\n");
	show_regs(regs);
	abort();
}

static exception_fn exception_handlers[VECTOR_MAX][EC_MAX];

void install_exception_handler(enum vector v, unsigned int ec, exception_fn fn)
{
	if (v < VECTOR_MAX && ec < EC_MAX)
		exception_handlers[v][ec] = fn;
}

static void default_vector_handler(enum vector v, struct pt_regs *regs,
				   unsigned int esr)
{
	unsigned int ec = esr >> ESR_EL1_EC_SHIFT;

	if (ec < EC_MAX && exception_handlers[v][ec])
		exception_handlers[v][ec](regs, esr);
	else
		bad_exception(v, regs, esr, false);
}

static vector_fn vector_handlers[VECTOR_MAX] = {
	[EL1H_SYNC]	= default_vector_handler,
	[EL1H_IRQ]	= default_vector_handler,
	[EL0_SYNC_64]	= default_vector_handler,
	[EL0_IRQ_64]	= default_vector_handler,
};

void do_handle_exception(enum vector v, struct pt_regs *regs, unsigned int esr)
{
	if (v < VECTOR_MAX && vector_handlers[v])
		vector_handlers[v](v, regs, esr);
	else
		bad_exception(v, regs, esr, true);
}

void install_vector_handler(enum vector v, vector_fn fn)
{
	if (v < VECTOR_MAX)
		vector_handlers[v] = fn;
}

bool user_mode;
void start_usr(void (*func)(void *arg), void *arg, unsigned long sp_usr)
{
	sp_usr &= (~15UL); /* stack ptr needs 16-byte alignment */

	user_mode = true;

	asm volatile(
		"mov	x0, %0\n"
		"msr	sp_el0, %1\n"
		"msr	elr_el1, %2\n"
		"mov	x3, xzr\n"	/* clear and "set" PSR_MODE_EL0t */
		"msr	spsr_el1, x3\n"
		"eret\n"
	:: "r" (arg), "r" (sp_usr), "r" (func) : "x0", "x3");
}
