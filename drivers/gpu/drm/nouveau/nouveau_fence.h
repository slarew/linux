/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_FENCE_H__
#define __NOUVEAU_FENCE_H__

#include <linux/dma-fence.h>
#include <nvif/event.h>

struct nouveau_drm;
struct nouveau_bo;

struct nouveau_fence {
	struct dma_fence base;

	struct list_head head;

	struct nouveau_channel __rcu *channel;
	unsigned long timeout;
};

static inline struct nouveau_fence *
to_nouveau_fence(struct dma_fence *fence)
{
	return container_of(fence, struct nouveau_fence, base);
}

int  nouveau_fence_create(struct nouveau_fence **, struct nouveau_channel *);
int  nouveau_fence_new(struct nouveau_fence **, struct nouveau_channel *);
void nouveau_fence_unref(struct nouveau_fence **);

int  nouveau_fence_emit(struct nouveau_fence *);
bool nouveau_fence_done(struct nouveau_fence *);
void nouveau_fence_cancel(struct nouveau_fence *fence);
int  nouveau_fence_wait(struct nouveau_fence *, bool lazy, bool intr);
int  nouveau_fence_sync(struct nouveau_bo *, struct nouveau_channel *, bool exclusive, bool intr);

struct nouveau_fence_chan {
	spinlock_t lock;
	struct kref fence_ref;

	struct list_head pending;
	struct list_head flip;

	int  (*emit)(struct nouveau_fence *);
	int  (*sync)(struct nouveau_fence *, struct nouveau_channel *,
		     struct nouveau_channel *);
	u32  (*read)(struct nouveau_channel *);
	int  (*emit32)(struct nouveau_channel *, u64, u32);
	int  (*sync32)(struct nouveau_channel *, u64, u32);

	u32 sequence;
	u32 context;
	char name[32];

	struct work_struct uevent_work;
	struct nvif_event event;
	int notify_ref, dead, killed;
};

struct nouveau_fence_priv {
	void (*dtor)(struct nouveau_drm *);
	bool (*suspend)(struct nouveau_drm *);
	void (*resume)(struct nouveau_drm *);
	int  (*context_new)(struct nouveau_channel *);
	void (*context_del)(struct nouveau_channel *);

	bool uevent;
};

#define nouveau_fence(drm) ((struct nouveau_fence_priv *)(drm)->fence)

void nouveau_fence_context_new(struct nouveau_channel *, struct nouveau_fence_chan *);
void nouveau_fence_context_del(struct nouveau_fence_chan *);
void nouveau_fence_context_free(struct nouveau_fence_chan *);
void nouveau_fence_context_kill(struct nouveau_fence_chan *, int error);

int nv04_fence_create(struct nouveau_drm *);
int nv04_fence_mthd(struct nouveau_channel *, u32, u32, u32);

int  nv10_fence_emit(struct nouveau_fence *);
int  nv17_fence_sync(struct nouveau_fence *, struct nouveau_channel *,
		     struct nouveau_channel *);
u32  nv10_fence_read(struct nouveau_channel *);
void nv10_fence_context_del(struct nouveau_channel *);
void nv10_fence_destroy(struct nouveau_drm *);
int  nv10_fence_create(struct nouveau_drm *);

int  nv17_fence_create(struct nouveau_drm *);
void nv17_fence_resume(struct nouveau_drm *drm);

int nv50_fence_create(struct nouveau_drm *);
int nv84_fence_create(struct nouveau_drm *);
int nvc0_fence_create(struct nouveau_drm *);
int gv100_fence_create(struct nouveau_drm *);

struct nv84_fence_chan {
	struct nouveau_fence_chan base;
	struct nouveau_vma *vma;
};

struct nv84_fence_priv {
	struct nouveau_fence_priv base;
	struct nouveau_bo *bo;
	u32 *suspend;
	struct mutex mutex;
};

int  nv84_fence_context_new(struct nouveau_channel *);

#endif
