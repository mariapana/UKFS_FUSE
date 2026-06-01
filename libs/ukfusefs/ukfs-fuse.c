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
#include <uk/fs/dirent.h>

struct fusefs_file {
	struct uk_file f;
	uk_file_refcnt refcnt;
	struct uk_file_state fstate;
	uint64_t nodeid;
	uint64_t fh;              /* file handle from FUSE_OPEN; (uint64_t)-1 = not yet opened */
	const struct uk_file *upref; /* parent mount point, set by graft; NULL for non-root nodes */
};

struct fuse_ukfs_queue fuse_global_queue;

static uint64_t fuse_req_unique = 1;

static void fusefs_release(const struct uk_file *f, int what)
{
	struct fusefs_file *ff = (struct fusefs_file *)f;
	if (what & UK_FILE_RELEASE_OBJ) {
		if (ff->upref)
			uk_file_release(ff->upref);
		uk_free(uk_alloc_get_default(), ff);
	}
}

static int fusefs_do_open(struct fusefs_file *ff)
{
	struct fuse_ukfs_request req;

	memset(&req, 0, sizeof(req));
	req.opcode = FUSE_UKFS_OPEN;
	req.unique = __atomic_fetch_add(&fuse_req_unique, 1, __ATOMIC_SEQ_CST);
	req.in.open.ino   = ff->nodeid;
	req.in.open.flags = 0; /* O_RDONLY */
	uk_semaphore_init(&req.done, 0);

	fuse_ukfs_queue_push(&fuse_global_queue, &req);
	uk_semaphore_down(&req.done);

	if (req.error)
		return req.error;

	struct fuse_open_out *out = req.reply_data;
	if (!out)
		return -EIO;
	ff->fh = out->fh;
	free(out);
	return 0;
}

static ssize_t fusefs_read(const struct uk_file *f, const struct iovec *iov,
			   size_t iovcnt, size_t off, long flags __unused)
{
	struct fusefs_file *ff = (struct fusefs_file *)f;

	if (ff->fh == (uint64_t)-1) {
		int err = fusefs_do_open(ff);
		if (err)
			return err;
	}

	size_t total = 0;
	for (size_t i = 0; i < iovcnt; i++)
		total += iov[i].iov_len;
	if (total == 0)
		return 0;

	struct fuse_ukfs_request req;

	memset(&req, 0, sizeof(req));
	req.opcode = FUSE_UKFS_READ;
	req.unique = __atomic_fetch_add(&fuse_req_unique, 1, __ATOMIC_SEQ_CST);
	req.in.read.ino    = ff->nodeid;
	req.in.read.fh     = ff->fh;
	req.in.read.offset = (uint64_t)off;
	req.in.read.size   = (uint32_t)total;
	uk_semaphore_init(&req.done, 0);

	fuse_ukfs_queue_push(&fuse_global_queue, &req);
	uk_semaphore_down(&req.done);

	if (req.error)
		return req.error;

	if (!req.reply_data)
		return 0; /* EOF */

	/* Scatter reply buffer across the iov */
	char *src = req.reply_data;
	size_t remaining = req.reply_len;
	ssize_t copied = 0;
	for (size_t i = 0; i < iovcnt && remaining > 0; i++) {
		size_t n = iov[i].iov_len < remaining ? iov[i].iov_len : remaining;
		memcpy(iov[i].iov_base, src, n);
		src += n;
		remaining -= n;
		copied += n;
	}
	free(req.reply_data);
	return copied;
}


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

	if (len == 2 && path[0] == '.' && path[1] == '.') {
		if (ff->upref) {
			uk_file_acquire(ff->upref);
			out->target = ff->upref;
		} else {
			uk_file_acquire(f);
			out->target = f;
		}
		if (nout) *nout = len;
		return UKFS_STOP_MNT;
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
		child->fh = (uint64_t)-1;
		child->upref = NULL;

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

static int fusefs_graft(const struct uk_file *f, const struct uk_file *ref)
{
	struct fusefs_file *ff = (struct fusefs_file *)f;
	UK_ASSERT(!ff->upref);
	uk_file_acquire(ref);
	ff->upref = ref;
	return 0;
}

static ssize_t fusefs_listdir(const struct uk_file *f, size_t *curp,
			      void *buf, size_t len)
{
	struct fusefs_file *ff = (struct fusefs_file *)f;
	struct fuse_ukfs_request req;

	memset(&req, 0, sizeof(req));
	req.opcode = FUSE_UKFS_READDIR;
	req.unique = __atomic_fetch_add(&fuse_req_unique, 1, __ATOMIC_SEQ_CST);
	req.in.read.ino    = ff->nodeid;
	req.in.read.fh     = 0;
	req.in.read.offset = *curp; /* FUSE offset == entry index in hello_ll */
	req.in.read.size   = 4096;
	uk_semaphore_init(&req.done, 0);

	fuse_ukfs_queue_push(&fuse_global_queue, &req);
	uk_semaphore_down(&req.done);

	if (req.error)
		return req.error;
	if (!req.reply_data || req.reply_len == 0) {
		free(req.reply_data);
		return 0; /* end of directory */
	}

	/* Decode the first fuse_dirent and re-encode as uk_fs_dirent */
	struct fuse_dirent *fd = (struct fuse_dirent *)req.reply_data;
	size_t reclen = UKFS_DIRENT_RECLEN(fd->namelen);

	if (len < reclen) {
		free(req.reply_data);
		return -EINVAL;
	}

	struct uk_fs_dirent *out = buf;
	memset(out, 0, reclen);
	out->d_ino    = (ino_t)fd->ino;
	out->d_off    = (off_t)(*curp + 1);
	out->d_reclen = (unsigned short)reclen;
	out->d_type   = (unsigned char)fd->type;
	memcpy(out->d_name, fd->name, fd->namelen);
	out->d_name[fd->namelen] = '\0';

	free(req.reply_data);
	(*curp)++;
	return (ssize_t)reclen;
}

static const struct uk_fs_ops fusefs_fs_ops = {
	.lookup  = fusefs_lookup,
	.stat    = fusefs_statfs,
	.listdir = fusefs_listdir,
	.graft   = fusefs_graft,
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
	n->fh = (uint64_t)-1;
	n->upref = NULL;
	
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
