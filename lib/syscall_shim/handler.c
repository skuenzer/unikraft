#include <uk/config.h>
#include <uk/syscall.h>
#include <uk/plat/syscall.h>
#include <uk/assert.h>
#include <uk/arch/ctx.h>
#include <uk/plat/ctx.h>
#include <uk/plat/tls.h>
#include <uk/essentials.h>
#include <arch/regmap_linuxabi.h>
#include "userland.h"

static inline void kernel_context(void)
{
	struct uk_thread *t = uk_thread_current();
	struct userland_ctx *uctx = ukthread2uctx(t);

	/* Save extended registers */
	ukarch_eregs_store(uctx->eregs);

#if CONFIG_LIBSYSCALL_SHIM_USERLANDTLS
	/* set kernel TLS */
	ukplat_tlsp_set(t->tlsp);
#endif /* CONFIG_LIBSYSCALL_SHIM_USERLANDTLS */
}

static inline void userland_context(void)
{
	struct userland_ctx *uctx = uctx_current();

	/* Restore extended registers */
	ukarch_eregs_load(uctx->eregs);

#if CONFIG_LIBSYSCALL_SHIM_USERLANDTLS
	/* set userland TLS */
	ukplat_tlsp_set(uctx->tlsp);
#endif /* CONFIG_LIBSYSCALL_SHIM_USERLANDTLS */
}

void ukplat_syscall_handler(struct __regs *r)
{
	UK_ASSERT(r);

	uk_pr_debug("Binary system call request \"%s\" (%lu) at ip:%p (arg0=0x%lx, arg1=0x%lx, ...)\n",
		    uk_syscall_name(r->rsyscall), r->rsyscall,
		    (void *) r->rip, r->rarg0, r->rarg1);

	/* Switch to kernel context
	 * (save extended regs, set kernel TLS)
	 */
	kernel_context();

	/* save return address on thread-local variable */
	uk_syscall_caller_retp = (__uptr) r->rip;
	r->rret0 = uk_syscall6_r(r->rsyscall,
				 r->rarg0, r->rarg1, r->rarg2,
				 r->rarg3, r->rarg4, r->rarg5);
	/* reset return address because it is becoming invalid on leave */
	uk_syscall_caller_retp = 0x0;

	/* Restore userland context
	 * (restore extended regs, set userland TLS)
	 */
	userland_context();
}
