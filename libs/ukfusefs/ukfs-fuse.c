#define FUSE_USE_VERSION 31
#include <uk/fs/driver.h>
#include <sys/stat.h>
#include <fuse_lowlevel.h>
#include <uk/fs.h>
#include <uk/errptr.h>
#include <uk/print.h>
#include <uk/fuse.h>
#include <uk/alloc.h>
#include <fuse/fuse_kernel.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct fusefs_file {
	struct uk_file f;
	uk_file_refcnt refcnt;
	struct uk_file_state fstate;
	uint64_t nodeid;
};

struct fuse_ukfs_queue fuse_global_queue;

static void fusefs_release(const struct uk_file *f, int what)
{
	struct fusefs_file *ff = (struct fusefs_file *)f;
	if (what & UK_FILE_RELEASE_OBJ)
		uk_free(uk_alloc_get_default(), ff);
}

static ssize_t fusefs_read(const struct uk_file *f __unused, const struct iovec *iov __unused,
			   size_t iovcnt __unused, size_t off __unused, long flags __unused)
{
	return -ENOSYS;
}



static uint64_t fuse_req_unique = 1;

#include <stdio.h>
static int fusefs_getstat(const struct uk_file *f, unsigned int mask __unused, struct uk_statx *arg)
{
	struct fusefs_file *ff = (struct fusefs_file *)f;
	struct fuse_ukfs_request req;
	
	memset(&req, 0, sizeof(req));
	req.opcode = FUSE_UKFS_GETATTR;
	req.unique = __atomic_fetch_add(&fuse_req_unique, 1, __ATOMIC_SEQ_CST);
	req.in.getattr.ino = ff->nodeid;
	uk_semaphore_init(&req.done, 0);

	fuse_ukfs_queue_push(&fuse_global_queue, &req);
	uk_semaphore_down(&req.done);

	if (req.error)
		return req.error;

	struct stat *out = req.reply_data;
	if (out) {
		arg->stx_mask = UK_STATX_MODE | UK_STATX_SIZE | UK_STATX_INO | UK_STATX_NLINK;
		arg->stx_mode = out->st_mode;
		arg->stx_size = out->st_size;
		arg->stx_ino = out->st_ino;
		arg->stx_nlink = out->st_nlink;
		arg->stx_uid = out->st_uid;
		arg->stx_gid = out->st_gid;
		free(out);
	} else {
		return -EIO;
	}
	
	return 0;
}

static const struct uk_file_ops fusefs_file_ops_stat = {
	.read = fusefs_read,
	.getstat = fusefs_getstat,
};

static int fusefs_statfs(const struct uk_file *f __unused, struct statfs *buf __unused)
{
	return -ENOSYS;
}

static int fusefs_lookup(const struct uk_file *f, const char *path, size_t len,
			 unsigned int flags __unused, union uk_fs_lookup_out *out, size_t *nout)
{
	struct fusefs_file *ff = (struct fusefs_file *)f;
	
	if (len == 0 || (len == 1 && path[0] == '.')) {
		uk_file_acquire(f);
		out->target = f;
		if (nout) *nout = len;
		return UKFS_STOP_FILE;
	}
	
	struct fuse_ukfs_request req;
	
	char *name = strndup(path, len);
	if (!name) return -ENOMEM;

	memset(&req, 0, sizeof(req));
	req.opcode = FUSE_UKFS_LOOKUP;
	req.unique = __atomic_fetch_add(&fuse_req_unique, 1, __ATOMIC_SEQ_CST);
	req.in.lookup.parent_ino = ff->nodeid;
	req.in.lookup.name = name;
	uk_semaphore_init(&req.done, 0);

	fuse_ukfs_queue_push(&fuse_global_queue, &req);
	uk_semaphore_down(&req.done);

	free(name);

	if (req.error)
		return req.error;

	struct fuse_entry_param *e = req.reply_data;
	if (e) {
		struct fusefs_file *child = uk_malloc(uk_alloc_get_default(), sizeof(*child));
		if (!child) {
			free(e);
			return -ENOMEM;
		}

		child->refcnt = UK_FILE_REFCNT_INIT_VALUE(child->refcnt);
		child->fstate = UK_FILE_STATE_INIT_VALUE(child->fstate);
		child->nodeid = e->ino;

		child->f.vol = f->vol;
		child->f.node = child;
		child->f.ops = &fusefs_file_ops_stat;
		child->f.fsops = f->fsops;
		child->f.refcnt = &child->refcnt;
		child->f.state = &child->fstate;
		child->f._release = fusefs_release;

		out->target = &child->f;
		if (nout) *nout = len;
		
		free(e);
		return UKFS_STOP_FILE; /* Finished mapping string */
	}

	return -EIO;
}

static const struct uk_fs_ops fusefs_fs_ops = {
	.lookup = fusefs_lookup,
	.stat = fusefs_statfs,
};

static const struct uk_file *fusefs_vopen(union uk_fs_vopen_vol vol __unused,
					  unsigned long flags __unused,
					  union uk_fs_vopen_data data __unused,
					  size_t fmt __unused)
{
	printf("[TRACE] fusefs_vopen called!\n");
	struct fusefs_file *n = uk_malloc(uk_alloc_get_default(), sizeof(*n));
	if (!n) return ERR2PTR(-ENOMEM);
	
	n->refcnt = UK_FILE_REFCNT_INIT_VALUE(n->refcnt);
	uk_rwlock_init(&n->fstate.iolock);
	n->fstate.pollq.events = 0;
	n->fstate.pollq.waitmask = 0;
	uk_waitq_init(&n->fstate.pollq.waitq);
	uk_rwlock_init(&n->fstate.pollq.waitlock);
#if CONFIG_LIBUKFILE_CHAINUPDATE
	UK_STAILQ_INIT(&n->fstate.pollq.prop);
	uk_mutex_init(&n->fstate.pollq.proplock);
	n->fstate.pollq.propmask = 0;
#endif
#if CONFIG_LIBUKFILE_POLLED
	n->fstate.pollq.poll_fn = NULL;
#endif
	n->nodeid = 1; /* FUSE ROOT */
	
	n->f.vol = NULL;
	n->f.node = n;
	n->f.ops = &fusefs_file_ops_stat;
	n->f.fsops = &fusefs_fs_ops;
	n->f.refcnt = &n->refcnt;
	n->f.state = &n->fstate;
	n->f._release = fusefs_release;
	
	uk_pr_info("FUSE Root node successfully instantiated. Invoking Queue Init.\n");
	fuse_ukfs_queue_init(&fuse_global_queue);
	
	return &n->f;
}

/* Register the "fuse" filesystem driver */
UK_FS_DRIVER_REGISTER(fuse, fusefs_vopen, UK_FS_VOPEN_VOL_IGNORE);
