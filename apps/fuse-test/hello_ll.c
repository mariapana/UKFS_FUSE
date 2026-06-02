#define FUSE_USE_VERSION 31
#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fuse_i.h>
#include <uk/fuse.h>

static const char *hello_str = "Hello World!\n";
static const char *hello_name = "hello";

static void hello_ll_getattr(fuse_req_t req, fuse_ino_t ino,
			     struct fuse_file_info *fi)
{
	printf("[TRACE] hello_ll_getattr called for ino=%lu\n", (unsigned long)ino);
	struct stat stbuf = {0};
	(void) fi;

	stbuf.st_ino = ino;
	if (ino == 1) {
		stbuf.st_mode = S_IFDIR | 0755;
		stbuf.st_nlink = 2;
	} else if (ino == 2) {
		stbuf.st_mode = S_IFREG | 0444;
		stbuf.st_nlink = 1;
		stbuf.st_size = strlen(hello_str);
	} else {
		fuse_reply_err(req, ENOENT);
		return;
	}
	fuse_reply_attr(req, &stbuf, 1.0);
}

static void hello_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct fuse_entry_param e = {0};

	if (parent != 1 || strcmp(name, hello_name) != 0) {
		fuse_reply_err(req, ENOENT);
		return;
	}

	e.ino = 2;
	e.attr_timeout = 1.0;
	e.entry_timeout = 1.0;
	e.attr.st_ino = 2;
	e.attr.st_mode = S_IFREG | 0444;
	e.attr.st_nlink = 1;
	e.attr.st_size = strlen(hello_str);

	fuse_reply_entry(req, &e);
}

static void hello_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			  off_t off, struct fuse_file_info *fi)
{
	(void)fi;
	if (ino != 2) {
		fuse_reply_err(req, EISDIR);
		return;
	}
	size_t len = strlen(hello_str);
	if (off >= (off_t)len) {
		fuse_reply_buf(req, NULL, 0);
		return;
	}
	size_t to_read = len - (size_t)off;
	if (to_read > size)
		to_read = size;
	fuse_reply_buf(req, hello_str + off, to_read);
}

static void hello_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
			     off_t off, struct fuse_file_info *fi)
{
	(void)fi;
	if (ino != 1) {
		fuse_reply_err(req, ENOTDIR);
		return;
	}

	static const struct {
		const char *name;
		fuse_ino_t  ino;
		unsigned int mode;
	} entries[] = {
		{".",     1, S_IFDIR},
		{"..",    1, S_IFDIR},
		{"hello", 2, S_IFREG},
	};
	int nentries = 3;

	char buf[4096];
	size_t pos = 0;

	for (int i = (int)off; i < nentries; i++) {
		struct stat stbuf = {0};
		stbuf.st_ino  = entries[i].ino;
		stbuf.st_mode = entries[i].mode;
		size_t entsize = fuse_add_direntry(req, buf + pos, sizeof(buf) - pos,
						   entries[i].name, &stbuf, i + 1);
		if (pos + entsize > size)
			break;
		pos += entsize;
	}

	fuse_reply_buf(req, buf, pos);
}

static const struct fuse_lowlevel_ops hello_ll_oper = {
	.lookup		= hello_ll_lookup,
	.getattr	= hello_ll_getattr,
	.read		= hello_ll_read,
	.readdir	= hello_ll_readdir,
};

/* Start the hello_ll FUSE backend */
struct fuse_session *hellofs_start(void)
{
	struct fuse_session *se;

	se = calloc(1, sizeof(*se));
	if (!se)
		return NULL;
	
	uk_mutex_init(&se->lock);
	uk_mutex_init(&se->mt_lock);
	
	se->got_init = 1;
	se->conn.proto_major = 7;
	se->conn.proto_minor = 31;
	se->conn.max_read = 131072;
	se->conn.max_write = 131072;
	
	se->list.next = se->list.prev = &se->list;
	se->interrupts.next = se->interrupts.prev = (struct fuse_req *)&se->interrupts;
	se->notify_list.next = se->notify_list.prev = &se->notify_list;
	se->notify_ctr = 1;
	
	memcpy(&se->op, &hello_ll_oper, sizeof(hello_ll_oper));

	return se;
}
