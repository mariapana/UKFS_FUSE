/* SPDX-License-Identifier: BSD-3-Clause */
#include <uk/fuse.h>
#include <uk/print.h>
#include <uk/thread.h>
#include <uk/sched.h>
#include <uk/alloc.h>
#include <errno.h>

/* Daemon thread function */
static void fuse_daemon_thread(void *arg) __noreturn;

static void fuse_daemon_thread(void *arg)
{
	struct fuse_ukfs_queue *q = (struct fuse_ukfs_queue *)arg;
	
	uk_pr_info("FUSE daemon thread started\n");
	
	while (1) {
		struct fuse_ukfs_request *req;
		
		/* Block waiting for a request */
		req = fuse_ukfs_queue_pop(q);
		
		uk_pr_info("FUSE daemon: received request opcode=%d unique=%lu\n",
			   req->opcode, req->unique);
		
		/* TODO: Process request based on opcode (Milestone 3+) */
		/* For now, just acknowledge receipt with ENOSYS */
		req->error = -ENOSYS;
		req->reply_data = NULL;
		req->reply_len = 0;
		
		/* Signal that the request is complete */
		uk_semaphore_up(&req->done);
	}
	
	/* Never reached */
}

int fuse_daemon_start(struct fuse_ukfs_queue *q)
{
	struct uk_thread *daemon;
	struct uk_alloc *a;
	
	UK_ASSERT(q);
	
	/* Get default allocator */
	a = uk_alloc_get_default();
	if (!a) {
		uk_pr_err("FUSE daemon: no allocator available\n");
		return -ENOMEM;
	}
	
	/* Create daemon thread */
	daemon = uk_thread_create_fn1(a,
				      fuse_daemon_thread,
				      q,           /* arg */
				      a,           /* stack allocator */
				      0,           /* default stack size */
				      NULL,        /* no aux stack */
				      0,           /* no aux stack size */
				      a,           /* TLS allocator */
				      false,       /* use default ectx */
				      "fuse-daemon",
				      NULL,        /* no priv */
				      NULL);       /* no dtor */
	
	if (!daemon) {
		uk_pr_err("FUSE daemon: failed to create thread\n");
		return -ENOMEM;
	}
	
	/* Add thread to scheduler and start it */
	uk_sched_thread_add(uk_sched_current(), daemon);
	
	uk_pr_info("FUSE daemon thread created and started\n");
	
	return 0;
}
