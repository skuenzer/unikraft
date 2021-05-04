#include <uk/config.h>
#include <uk/arch/types.h>
#include <uk/arch/ctx.h>
#include <uk/plat/ctx.h>
#include <uk/plat/tls.h>
#include <uk/thread.h>
#include <uk/essentials.h>
#include <uk/assert.h>
#include "userland.h"

static int init_userland_ctx(struct uk_thread *t)
{
	struct uk_alloc *a;
	struct userland_ctx *uctx;
	__sz eregs_len = 0;

	UK_ASSERT(!t->syscall_shim);

	eregs_len = ukarch_eregs_size() + ukarch_eregs_align();

	a = uk_alloc_get_default();
	uctx = (struct userland_ctx *) uk_zalloc(a,
						 sizeof(*uctx)
						 + eregs_len);
	if (!uctx)
		return -ENOMEM;

	uctx->eregs = (__u8 *) ALIGN_UP((__uptr)uctx->_eregs,
					ukarch_eregs_align());
	ukarch_eregs_init(uctx->eregs);

	t->syscall_shim = (void *) uctx;

	return 0;
}

static void free_userland_ctx(struct uk_thread *t)
{
	struct uk_alloc *a;
	struct userland_ctx *uctx;

	uctx = ukthread2uctx(t);
	a = uctx->a;
	uk_free(a, uctx);
	t->syscall_shim = NULL;
}

UK_THREAD_INIT(init_userland_ctx, free_userland_ctx);

#if CONFIG_LIBSYSCALL_SHIM_USERLANDTLS
void uk_syscall_userland_tlsp_sett(struct uk_thread *t, __uptr tlsp)
{
	struct userland_ctx *uctx;

	UK_ASSERT(t);
	uctx = ukthread2uctx(t);
	uctx->tlsp = tlsp;
}

__uptr uk_syscall_userland_tlsp_gett(struct uk_thread *t)
{
	struct userland_ctx *uctx;

	UK_ASSERT(t);
	uctx = ukthread2uctx(t);
	return uctx->tlsp;
}
#endif /* CONFIG_LIBSYSCALL_SHIM_USERLANDTLS */
