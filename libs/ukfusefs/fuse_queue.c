/* SPDX-License-Identifier: BSD-3-Clause */
#include <uk/fuse.h>
#include <uk/print.h>
#include <uk/assert.h>

void fuse_ukfs_queue_init(struct fuse_ukfs_queue *q)
{
	UK_ASSERT(q);
	
	/* Initialize the TAILQ head */
	UK_TAILQ_INIT(&q->head);
	
	/* Initialize mutex for thread-safe access */
	uk_mutex_init(&q->lock);
	
	/* Initialize semaphore with count 0 (no pending requests) */
	uk_semaphore_init(&q->count, 0);
	
	uk_pr_debug("FUSE queue initialized at %p\n", q);
}

void fuse_ukfs_queue_push(struct fuse_ukfs_queue *q,
			  struct fuse_ukfs_request *req)
{
	UK_ASSERT(q);
	UK_ASSERT(req);
	
	uk_pr_debug("FUSE queue: pushing request opcode=%d unique=%lu\n",
		    req->opcode, req->unique);
	
	/* Lock the queue */
	uk_mutex_lock(&q->lock);
	
	/* Add request to tail (FIFO) */
	UK_TAILQ_INSERT_TAIL(&q->head, req, list);
	
	/* Unlock the queue */
	uk_mutex_unlock(&q->lock);
	
	/* Signal that a request is available */
	uk_semaphore_up(&q->count);
}

struct fuse_ukfs_request *fuse_ukfs_queue_pop(struct fuse_ukfs_queue *q)
{
	struct fuse_ukfs_request *req;
	
	UK_ASSERT(q);
	
	/* Wait for a request to be available (blocks if count == 0) */
	uk_semaphore_down(&q->count);
	
	/* Lock the queue */
	uk_mutex_lock(&q->lock);
	
	/* Remove request from head (FIFO) */
	req = UK_TAILQ_FIRST(&q->head);
	UK_ASSERT(req); /* Should never be NULL after semaphore_down */
	UK_TAILQ_REMOVE(&q->head, req, list);
	
	/* Unlock the queue */
	uk_mutex_unlock(&q->lock);
	
	uk_pr_debug("FUSE queue: popped request opcode=%d unique=%lu\n",
		    req->opcode, req->unique);
	
	return req;
}
