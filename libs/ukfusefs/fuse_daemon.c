/* SPDX-License-Identifier: BSD-3-Clause */
#include <uk/fuse.h>
#include <uk/print.h>
#include <uk/thread.h>
#include <uk/sched.h>
#include <uk/alloc.h>
#include <errno.h>
#include <fuse/fuse_kernel.h>
#include <fuse_i.h>
#include <string.h>

extern void fuse_session_process_buf_internal(struct fuse_session *se, const struct fuse_buf *buf, struct fuse_chan *ch);

/* Daemon thread function */
static void fuse_daemon_thread(void *arg) __noreturn;

static void fuse_daemon_thread(void *arg)
{
	void **args = (void **)arg;
	struct fuse_ukfs_queue *q = args[0];
	struct fuse_session *se = args[1];
	
	uk_pr_info("FUSE daemon thread started\n");
	
	/* We provide a dynamically-allocated single-threaded buffer for deserializing.
	 * Max FUSE buffer is essentially pagesize plus header.
	 */
	char *buf = uk_malloc(uk_alloc_get_default(), 8192);
	if (!buf) {
		uk_pr_err("FUSE daemon: failed to allocate buf!\n");
		return;
	}
	struct fuse_buf fbuf = { .mem = buf, .size = 0 };

	while (1) {
		struct fuse_ukfs_request *req;
		struct fuse_in_header *in = (struct fuse_in_header *)buf;
		
		/* Block waiting for a request */
		req = fuse_ukfs_queue_pop(q);
		
		uk_pr_info("FUSE daemon: received request opcode=%d unique=%lu\n",
			   req->opcode, req->unique);
		
		memset(buf, 0, sizeof(*in));
		in->unique = req->unique;
		in->uid = 0;
		in->gid = 0;
		in->pid = 1;
		in->len = sizeof(*in);
		
		if (req->opcode == FUSE_UKFS_LOOKUP) {
			in->opcode = FUSE_LOOKUP;
			in->nodeid = req->in.lookup.parent_ino;
			size_t namelen = strlen(req->in.lookup.name) + 1;
			memcpy(buf + in->len, req->in.lookup.name, namelen);
			in->len += namelen;
		} else if (req->opcode == FUSE_UKFS_GETATTR) {
			in->opcode = FUSE_GETATTR;
			in->nodeid = req->in.getattr.ino;
			struct fuse_getattr_in arg = { .getattr_flags = 0 };
			memcpy(buf + in->len, &arg, sizeof(arg));
			in->len += sizeof(arg);
		} else if (req->opcode == FUSE_UKFS_OPEN) {
			in->opcode = FUSE_OPEN;
			in->nodeid = req->in.open.ino;
			struct fuse_open_in arg = { .flags = req->in.open.flags };
			memcpy(buf + in->len, &arg, sizeof(arg));
			in->len += sizeof(arg);
		} else if (req->opcode == FUSE_UKFS_READ) {
			in->opcode = FUSE_READ;
			in->nodeid = req->in.read.ino;
			struct fuse_read_in arg = {
				.fh     = req->in.read.fh,
				.offset = req->in.read.offset,
				.size   = req->in.read.size,
			};
			memcpy(buf + in->len, &arg, sizeof(arg));
			in->len += sizeof(arg);
		} else {
			req->error = -ENOSYS;
			req->reply_data = NULL;
			req->reply_len = 0;
			uk_semaphore_up(&req->done);
			continue;
		}
		fbuf.size = in->len;
		
		/* Pass the request pointer disguised as struct fuse_chan * */
		fuse_session_process_buf_internal(se, &fbuf, (struct fuse_chan *)req);
	}
}

int fuse_daemon_start(struct fuse_ukfs_queue *q, struct fuse_session *se)
{
	struct uk_thread *daemon;
	struct uk_alloc *a;
	void **args;
	
	UK_ASSERT(q && se);
	
	a = uk_alloc_get_default();
	if (!a) {
		uk_pr_err("FUSE daemon: no allocator available\n");
		return -ENOMEM;
	}
	
	args = uk_malloc(a, sizeof(void *) * 2);
	if (!args)
		return -ENOMEM;
	
	args[0] = q;
	args[1] = se;
	
	/* Create daemon thread */
	daemon = uk_thread_create_fn1(a,
				      fuse_daemon_thread,
				      args,        /* arg */
				      a,           /* stack allocator */
				      16384,       /* explicit stack size */
				      NULL,        /* no aux stack */
				      0,           /* no aux stack size */
				      a,           /* TLS allocator */
				      false,       /* use default ectx */
				      "fuse-daemon",
				      NULL,        /* no priv */
				      NULL);       /* no dtor */
	
	if (!daemon) {
		uk_free(a, args);
		uk_pr_err("FUSE daemon: failed to create thread\n");
		return -ENOMEM;
	}
	
	/* Add thread to scheduler and start it */
	uk_sched_thread_add(uk_sched_current(), daemon);
	
	uk_pr_info("FUSE daemon thread created and started\n");
	
	return 0;
}
