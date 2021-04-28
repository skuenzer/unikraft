#ifndef __LIB_SYSCALLSHIM_USERLAND_H__
#define __LIB_SYSCALLSHIM_USERLAND_H__

#include <uk/alloc.h>
#include <uk/thread.h>
#include <uk/assert.h>

struct userland_ctx {
	struct uk_alloc *a;

	__u8 *eregs;
	__u8 _eregs[];
};

static inline struct userland_ctx *ukthread2uctx(struct uk_thread *t)
{
	UK_ASSERT(t);
	UK_ASSERT(t->syscall_shim);

	return (struct userland_ctx *) t->syscall_shim;
}

#define uctx_current() \
	(ukthread2uctx(uk_thread_current()))

#endif /* __LIB_SYSCALLSHIM_USERLAND_H__ */
