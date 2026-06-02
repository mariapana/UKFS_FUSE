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

#define FUSE_DAEMON_BUF_SIZE 8192  /* fuse_in_header + largest per-opcode payload */

extern void fuse_session_process_buf_internal(struct fuse_session *se,
					      const struct fuse_buf *buf,
					      struct fuse_chan *ch);

struct fuse_daemon_args {
	struct fuse_ukfs_queue *q;
	struct fuse_session    *se;
};

/* Per-opcode buffer fill functions — each sets in->nodeid and appends its payload */

static void fill_lookup(char *buf, struct fuse_in_header *in,
			const struct fuse_ukfs_request *req)
{
	in->nodeid = req->in.lookup.parent_ino;
	size_t namelen = strlen(req->in.lookup.name) + 1;
	memcpy(buf + in->len, req->in.lookup.name, namelen);
	in->len += namelen;
}

static void fill_getattr(char *buf, struct fuse_in_header *in,
			 const struct fuse_ukfs_request *req)
{
	in->nodeid = req->in.getattr.ino;
	struct fuse_getattr_in arg = { .getattr_flags = 0 };
	memcpy(buf + in->len, &arg, sizeof(arg));
	in->len += sizeof(arg);
}

static void fill_open(char *buf, struct fuse_in_header *in,
		      const struct fuse_ukfs_request *req)
{
	in->nodeid = req->in.open.ino;
	struct fuse_open_in arg = { .flags = req->in.open.flags };
	memcpy(buf + in->len, &arg, sizeof(arg));
	in->len += sizeof(arg);
}

static void fill_read(char *buf, struct fuse_in_header *in,
		      const struct fuse_ukfs_request *req)
{
	in->nodeid = req->in.read.ino;
	struct fuse_read_in arg = {
		.fh     = req->in.read.fh,
		.offset = req->in.read.offset,
		.size   = req->in.read.size,
	};
	memcpy(buf + in->len, &arg, sizeof(arg));
	in->len += sizeof(arg);
}

static void fill_readdir(char *buf, struct fuse_in_header *in,
			 const struct fuse_ukfs_request *req)
{
	in->nodeid = req->in.readdir.ino;
	struct fuse_read_in arg = {
		.fh     = req->in.readdir.fh,
		.offset = req->in.readdir.offset,
		.size   = req->in.readdir.size,
	};
	memcpy(buf + in->len, &arg, sizeof(arg));
	in->len += sizeof(arg);
}

struct fuse_dispatch_entry {
	enum fuse_ukfs_opcode ukfs_opcode;
	int fuse_opcode;
	void (*fill)(char *buf, struct fuse_in_header *in,
		     const struct fuse_ukfs_request *req);
};

static const struct fuse_dispatch_entry dispatch_table[] = {
	{ FUSE_UKFS_LOOKUP,  FUSE_LOOKUP,  fill_lookup  },
	{ FUSE_UKFS_GETATTR, FUSE_GETATTR, fill_getattr },
	{ FUSE_UKFS_OPEN,    FUSE_OPEN,    fill_open    },
	{ FUSE_UKFS_READ,    FUSE_READ,    fill_read    },
	{ FUSE_UKFS_READDIR, FUSE_READDIR, fill_readdir },
};

#define DISPATCH_TABLE_SIZE (sizeof(dispatch_table) / sizeof(dispatch_table[0]))

static void fuse_daemon_thread(void *arg) __noreturn;

static void fuse_daemon_thread(void *arg)
{
	struct fuse_daemon_args *da = (struct fuse_daemon_args *)arg;
	struct fuse_ukfs_queue *q = da->q;
	struct fuse_session *se = da->se;

	uk_pr_info("FUSE daemon thread started\n");

	char *buf = uk_malloc(uk_alloc_get_default(), FUSE_DAEMON_BUF_SIZE);
	if (!buf) {
		uk_pr_err("FUSE daemon: failed to allocate buf!\n");
		return;
	}
	struct fuse_buf fbuf = { .mem = buf, .size = 0 };

	while (1) {
		struct fuse_ukfs_request *req = fuse_ukfs_queue_pop(q);
		struct fuse_in_header *in = (struct fuse_in_header *)buf;

		uk_pr_info("FUSE daemon: received request opcode=%d unique=%lu\n",
			   req->opcode, req->unique);

		memset(buf, 0, sizeof(*in));
		in->unique = req->unique;
		in->uid    = 0;
		in->gid    = 0;
		in->pid    = 1;
		in->len    = sizeof(*in);

		const struct fuse_dispatch_entry *de = NULL;
		for (size_t i = 0; i < DISPATCH_TABLE_SIZE; i++) {
			if (dispatch_table[i].ukfs_opcode == req->opcode) {
				de = &dispatch_table[i];
				break;
			}
		}

		if (!de) {
			req->error     = -ENOSYS;
			req->reply_data = NULL;
			req->reply_len  = 0;
			uk_semaphore_up(&req->done);
			continue;
		}

		in->opcode = de->fuse_opcode;
		de->fill(buf, in, req);
		fbuf.size = in->len;

		/* Pass the request pointer disguised as struct fuse_chan * */
		fuse_session_process_buf_internal(se, &fbuf, (struct fuse_chan *)req);
	}
}

int fuse_daemon_start(struct fuse_ukfs_queue *q, struct fuse_session *se)
{
	struct uk_alloc *a;
	struct fuse_daemon_args *da;
	struct uk_thread *daemon;

	UK_ASSERT(q && se);

	a = uk_alloc_get_default();
	if (!a) {
		uk_pr_err("FUSE daemon: no allocator available\n");
		return -ENOMEM;
	}

	da = uk_malloc(a, sizeof(*da));
	if (!da)
		return -ENOMEM;

	da->q  = q;
	da->se = se;

	daemon = uk_thread_create_fn1(a,
				      fuse_daemon_thread,
				      da,
				      a,
				      16384,
				      NULL,
				      0,
				      a,
				      false,
				      "fuse-daemon",
				      NULL,
				      NULL);

	if (!daemon) {
		uk_free(a, da);
		uk_pr_err("FUSE daemon: failed to create thread\n");
		return -ENOMEM;
	}

	uk_sched_thread_add(uk_sched_current(), daemon);

	uk_pr_info("FUSE daemon thread created and started\n");

	return 0;
}
