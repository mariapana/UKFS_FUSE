/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __UK_FUSE_H__
#define __UK_FUSE_H__

#include <uk/config.h>
#include <uk/list.h>
#include <uk/mutex.h>
#include <uk/semaphore.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * FUSE-UKFS Integration Header
 * Defines structures for communication between UKFS driver and libfuse
 */

/* FUSE request operation types */
enum fuse_ukfs_opcode {
	FUSE_UKFS_LOOKUP = 1,
	FUSE_UKFS_GETATTR,
	FUSE_UKFS_OPEN,
	FUSE_UKFS_READ,
	FUSE_UKFS_READDIR,
	/* More opcodes to be added in later milestones */
};

/* FUSE request structure */
struct fuse_ukfs_request {
	UK_TAILQ_ENTRY(struct fuse_ukfs_request) list;  /* Queue linkage */
	
	enum fuse_ukfs_opcode opcode;  /* Operation type */
	uint64_t unique;               /* Unique request ID */
	
	/* Input parameters (varies by opcode) */
	union {
		struct {
			uint64_t parent_ino;
			char *name;
		} lookup;
		
		struct {
			uint64_t ino;
		} getattr;
		
		/* More operation-specific data to be added */
	} in;
	
	/* Output/reply data */
	int error;                     /* Error code (0 = success) */
	void *reply_data;              /* Operation-specific reply */
	size_t reply_len;              /* Length of reply data */
	
	/* Synchronization */
	struct uk_semaphore done;      /* Signaled when request is complete */
};

/* Request queue structure */
struct fuse_ukfs_queue {
	UK_TAILQ_HEAD(, struct fuse_ukfs_request) head;
	struct uk_mutex lock;          /* Protects queue operations */
	struct uk_semaphore count;     /* Counts pending requests */
};

/* Queue API */

/**
 * Initialize a FUSE request queue
 * @param q Queue to initialize
 */
void fuse_ukfs_queue_init(struct fuse_ukfs_queue *q);

/**
 * Push a request to the queue (thread-safe)
 * @param q Queue to push to
 * @param req Request to enqueue
 */
void fuse_ukfs_queue_push(struct fuse_ukfs_queue *q,
			  struct fuse_ukfs_request *req);

/**
 * Pop a request from the queue (thread-safe, blocking)
 * Blocks if queue is empty until a request is available
 * @param q Queue to pop from
 * @return Pointer to dequeued request
 */
struct fuse_ukfs_request *fuse_ukfs_queue_pop(struct fuse_ukfs_queue *q);

/* Daemon thread API */

/**
 * Start the FUSE daemon thread
 * @param q Request queue for the daemon to process
 * @return 0 on success, negative error code on failure
 */
int fuse_daemon_start(struct fuse_ukfs_queue *q);

#ifdef __cplusplus
}
#endif

#endif /* __UK_FUSE_H__ */
