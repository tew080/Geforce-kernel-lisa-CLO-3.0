// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "sde_hw_mdss.h"
#include "sde_hw_blk.h"

/* Serialization lock for sde_hw_blk_list */
static DEFINE_MUTEX(sde_hw_blk_lock);

/* List of all hw block objects */
static LIST_HEAD(sde_hw_blk_list);

/**
 * sde_hw_blk_init - initialize hw block object
 * @type: hw block type - enum sde_hw_blk_type
 * @id: instance id of the hw block
 * @ops: Pointer to block operations
 * return: 0 if success; error code otherwise
 */
int sde_hw_blk_init(struct sde_hw_blk *hw_blk, u32 type, int id,
		struct sde_hw_blk_ops *ops)
{
	if (!hw_blk) {
		pr_debug("invalid parameters\n");
		return -EINVAL;
	}

	INIT_LIST_HEAD(&hw_blk->list);
	hw_blk->type = type;
	hw_blk->id = id;
	atomic_set(&hw_blk->refcount, 0);

	if (ops)
		hw_blk->ops = *ops;

	mutex_lock(&sde_hw_blk_lock);
	list_add(&hw_blk->list, &sde_hw_blk_list);
	mutex_unlock(&sde_hw_blk_lock);

	return 0;
}

/**
 * sde_hw_blk_destroy - destroy hw block object.
 * @hw_blk:  pointer to hw block object
 * return: none
 */
void sde_hw_blk_destroy(struct sde_hw_blk *hw_blk)
{
	if (!hw_blk) {
		pr_debug("invalid parameters\n");
		return;
	}

	if (atomic_read(&hw_blk->refcount))
		pr_debug("hw_blk:%d.%d invalid refcount\n", hw_blk->type,
				hw_blk->id);

	mutex_lock(&sde_hw_blk_lock);
	list_del(&hw_blk->list);
	mutex_unlock(&sde_hw_blk_lock);
}

/**
 * sde_hw_blk_get - get hw_blk from free pool
 * @hw_blk: if specified, increment reference count only
 * @type: if hw_blk is not specified, allocate the next available of this type
 * @id: if specified (>= 0), allocate the given instance of the above type
 * return: pointer to hw block object
 */
struct sde_hw_blk *sde_hw_blk_get(struct sde_hw_blk *hw_blk, u32 type, int id)
{
	struct sde_hw_blk *curr;
	int rc, refcount;

	if (!hw_blk) {
		mutex_lock(&sde_hw_blk_lock);
		list_for_each_entry(curr, &sde_hw_blk_list, list) {
			if ((curr->type != type) ||
					(id >= 0 && curr->id != id) ||
					(id < 0 &&
						atomic_read(&curr->refcount)))
				continue;

			hw_blk = curr;
			break;
		}
		mutex_unlock(&sde_hw_blk_lock);
	}

	if (!hw_blk) {
		pr_debug("no hw_blk:%d\n", type);
		return NULL;
	}

	refcount = atomic_inc_return(&hw_blk->refcount);

	if (refcount == 1 && hw_blk->ops.start) {
		rc = hw_blk->ops.start(hw_blk);
		if (rc) {
			pr_debug("failed to start  hw_blk:%d rc:%d\n", type, rc);
			goto error_start;
		}
	}

	pr_debug("hw_blk:%d.%d refcount:%d\n", hw_blk->type,
			hw_blk->id, refcount);
	return hw_blk;

error_start:
	sde_hw_blk_put(hw_blk);
	return ERR_PTR(rc);
}

/**
 * sde_hw_blk_put - put hw_blk to free pool if decremented refcount is zero
 * @hw_blk: hw block to be freed
 * @free_blk: function to be called when reference count goes to zero
 */
void sde_hw_blk_put(struct sde_hw_blk *hw_blk)
{
	if (!hw_blk) {
		pr_debug("invalid parameters\n");
		return;
	}

	pr_debug("hw_blk:%d.%d refcount:%d\n", hw_blk->type, hw_blk->id,
			atomic_read(&hw_blk->refcount));

	if (!atomic_read(&hw_blk->refcount)) {
		pr_debug("hw_blk:%d.%d invalid put\n", hw_blk->type, hw_blk->id);
		return;
	}

	if (atomic_dec_return(&hw_blk->refcount))
		return;

	if (hw_blk->ops.stop)
		hw_blk->ops.stop(hw_blk);
}
