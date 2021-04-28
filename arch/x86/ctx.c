#include <uk/arch/ctx.h>
#include <uk/arch/lcpu.h>
#include <uk/arch/types.h>
#include <uk/ctors.h>
#include <uk/essentials.h>
#include <uk/assert.h>
#include <uk/print.h>
#include <string.h> /* memset */

enum x86_save_method {
	X86_SAVE_NONE = 0,
	X86_SAVE_FSAVE,
	X86_SAVE_FXSAVE,
	X86_SAVE_XSAVE,
	X86_SAVE_XSAVEOPT
};

static enum x86_save_method eregs_method;
static __sz eregs_size;
static __sz eregs_align;

static void _init_eregs_store(void)
{
	__u32 eax, ebx, ecx, edx;

	/* Why are we saving the eax register content to the eax variable with
	 * "=a(eax)", but then never use it?
	 * Because gcc otherwise will assume that the eax register still
	 * contains "1" after this asm expression. See the "Warning" note at
	 * https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html#InputOperands
	 */
	ukarch_x86_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	if (ecx & __X86_CPUID1_ECX_OSXSAVE) {
		ukarch_x86_cpuid(0xd, 1, &eax, &ebx, &ecx, &edx);
		if (eax & __X86_CPUIDD1_EAX_XSAVEOPT) {
			eregs_method = X86_SAVE_XSAVEOPT;
			uk_pr_debug("Load/store of extended CPU state: XSAVEOPT\n");
		} else {
			eregs_method = X86_SAVE_XSAVE;
			uk_pr_debug("Load/store of extended CPU state: XSAVE\n");
		}
		ukarch_x86_cpuid(0xd, 0, &eax, &ebx, &ecx, &edx);
		eregs_size = ebx;
		eregs_align = 64;
	} else if (edx & __X86_CPUID1_EDX_FXSR) {
		eregs_method = X86_SAVE_FXSAVE;
		eregs_size = 512;
		eregs_align = 16;
		uk_pr_debug("Load/store of extended CPU state: FXSAVE\n");
	} else {
		eregs_method = X86_SAVE_FSAVE;
		eregs_size = 108;
		eregs_align = 1;
		uk_pr_debug("Load/store of extended CPU state: FSAVE\n");
	}

	/* NOTE: In case a condition is added here that disables extregs
	 *       (size=0), please make sure that align is still set to 1
	 *       so that we can detect if _init_cpufeatures() was called.
	 */
}
UK_CTOR_PRIO(_init_eregs_store, 0);

__sz ukarch_eregs_size(void)
{
	UK_ASSERT(eregs_align); /* Do not call when not yet initialized */

	return eregs_size;
}

__sz ukarch_eregs_align(void)
{
	UK_ASSERT(eregs_align); /* Do not call when not yet initialized */

	return eregs_align;
}

void ukarch_eregs_init(__u8 state[])
{
	UK_ASSERT(eregs_align); /* Do not call when not yet initialized */
	UK_ASSERT(state);
	UK_ASSERT(IS_ALIGNED((__uptr) state, eregs_align));

	/* Initialize extregs area:
	 * Zero out and then save a valid layout to it.
	 */
	memset(state, 0, eregs_size);
	ukarch_eregs_store(state);
}

void ukarch_eregs_store(__u8 state[])
{
	UK_ASSERT(eregs_align); /* Do not call when not yet initialized */
	UK_ASSERT(state);
	UK_ASSERT(IS_ALIGNED((__uptr) state, eregs_align));

	switch (eregs_method) {
	case X86_SAVE_NONE:
		/* nothing to do */
		break;
	case X86_SAVE_FSAVE:
		asm volatile("fsave (%0)" :: "r"(state) : "memory");
		break;
	case X86_SAVE_FXSAVE:
		asm volatile("fxsave (%0)" :: "r"(state) : "memory");
		break;
	case X86_SAVE_XSAVE:
		asm volatile("xsave (%0)" :: "r"(state),
			     "a"(0xffffffff), "d"(0xffffffff) : "memory");
		break;
	case X86_SAVE_XSAVEOPT:
		asm volatile("xsaveopt (%0)" :: "r"(state),
			     "a"(0xffffffff), "d"(0xffffffff) : "memory");
		break;
	}
}

void ukarch_eregs_load(__u8 state[])
{
	UK_ASSERT(eregs_align); /* Do not call when not yet initialized */
	UK_ASSERT(state);
	UK_ASSERT(IS_ALIGNED((__uptr) state, eregs_align));

	switch (eregs_method) {
	case X86_SAVE_NONE:
		/* nothing to do */
		break;
	case X86_SAVE_FSAVE:
		asm volatile("frstor (%0)" :: "r"(state));
		break;
	case X86_SAVE_FXSAVE:
		asm volatile("fxrstor (%0)" :: "r"(state));
		break;
	case X86_SAVE_XSAVE:
	case X86_SAVE_XSAVEOPT:
		asm volatile("xrstor (%0)" :: "r"(state),
			     "a"(0xffffffff), "d"(0xffffffff));
		break;
	}
}
