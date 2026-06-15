/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  Extended hello_ll backend with a /bigfile entry for throughput benchmarks.
  All callbacks use the standard libfuse low-level API — no Unikraft-specific
  modifications. The only difference from the minimal upstream hello_ll is the
  addition of ino=3 (/bigfile, 100 MB) for throughput testing.
*/

#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static const char *hello_str = "Hello World!\n";
static const char *hello_name = "hello";
static const char *bigfile_name = "bigfile";
#define BIGFILE_SIZE (100 * 1024 * 1024)

static int hello_stat(fuse_ino_t ino, struct stat *stbuf)
{
	stbuf->st_ino = ino;
	switch (ino) {
	case 1:
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		break;
	case 2:
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(hello_str);
		break;
	case 3:
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = BIGFILE_SIZE;
		break;
	default:
		return -1;
	}
	return 0;
}

static void hello_ll_getattr(fuse_req_t req, fuse_ino_t ino,
			     struct fuse_file_info *fi)
{
	struct stat stbuf;
	(void)fi;
	memset(&stbuf, 0, sizeof(stbuf));
	if (hello_stat(ino, &stbuf) == -1)
		fuse_reply_err(req, ENOENT);
	else
		fuse_reply_attr(req, &stbuf, 1.0);
}

static void hello_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct fuse_entry_param e;

	if (parent != 1) {
		fuse_reply_err(req, ENOENT);
		return;
	}

	fuse_ino_t ino = 0;
	if (strcmp(name, hello_name) == 0)
		ino = 2;
	else if (strcmp(name, bigfile_name) == 0)
		ino = 3;
	else {
		fuse_reply_err(req, ENOENT);
		return;
	}

	memset(&e, 0, sizeof(e));
	e.ino = ino;
	e.attr_timeout = 1.0;
	e.entry_timeout = 1.0;
	hello_stat(ino, &e.attr);
	fuse_reply_entry(req, &e);
}

struct dirbuf {
	char *p;
	size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name,
		       fuse_ino_t ino)
{
	struct stat stbuf;
	size_t oldsize = b->size;
	b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
	b->p = (char *)realloc(b->p, b->size);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_ino = ino;
	fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf,
			  b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
			     off_t off, size_t maxsize)
{
	if (off < (off_t)bufsize)
		return fuse_reply_buf(req, buf + off,
				      min(bufsize - off, maxsize));
	else
		return fuse_reply_buf(req, NULL, 0);
}

static void hello_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
			     off_t off, struct fuse_file_info *fi)
{
	(void)fi;

	if (ino != 1) {
		fuse_reply_err(req, ENOTDIR);
		return;
	}

	struct dirbuf b;
	memset(&b, 0, sizeof(b));
	dirbuf_add(req, &b, ".", 1);
	dirbuf_add(req, &b, "..", 1);
	dirbuf_add(req, &b, hello_name, 2);
	dirbuf_add(req, &b, bigfile_name, 3);
	reply_buf_limited(req, b.p, b.size, off, size);
	free(b.p);
}

static void hello_ll_open(fuse_req_t req, fuse_ino_t ino,
			  struct fuse_file_info *fi)
{
	if (ino == 1)
		fuse_reply_err(req, EISDIR);
	else if ((fi->flags & O_ACCMODE) != O_RDONLY)
		fuse_reply_err(req, EACCES);
	else
		fuse_reply_open(req, fi);
}

static void hello_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			  off_t off, struct fuse_file_info *fi)
{
	(void)fi;

	if (ino == 2) {
		reply_buf_limited(req, hello_str, strlen(hello_str), off, size);
	} else if (ino == 3) {
		if (off >= BIGFILE_SIZE) {
			fuse_reply_buf(req, NULL, 0);
			return;
		}
		size_t to_read = BIGFILE_SIZE - (size_t)off;
		if (to_read > size)
			to_read = size;
		char *tmp = malloc(to_read);
		if (!tmp) {
			fuse_reply_err(req, ENOMEM);
			return;
		}
		memset(tmp, 'A', to_read);
		fuse_reply_buf(req, tmp, to_read);
		free(tmp);
	} else {
		fuse_reply_err(req, EISDIR);
	}
}

static const struct fuse_lowlevel_ops hello_ll_oper = {
	.lookup  = hello_ll_lookup,
	.getattr = hello_ll_getattr,
	.readdir = hello_ll_readdir,
	.open    = hello_ll_open,
	.read    = hello_ll_read,
};

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_session *se;
	struct fuse_cmdline_opts opts;
	struct fuse_loop_config *config;
	int ret = -1;

	if (fuse_parse_cmdline(&args, &opts) != 0)
		return 1;
	if (opts.show_help) {
		printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
		fuse_cmdline_help();
		fuse_lowlevel_help();
		ret = 0;
		goto err_out1;
	} else if (opts.show_version) {
		printf("FUSE library version %s\n", fuse_pkgversion());
		fuse_lowlevel_version();
		ret = 0;
		goto err_out1;
	}

	if (opts.mountpoint == NULL) {
		printf("usage: %s [options] <mountpoint>\n", argv[0]);
		printf("       %s --help\n", argv[0]);
		ret = 1;
		goto err_out1;
	}

	se = fuse_session_new(&args, &hello_ll_oper,
			      sizeof(hello_ll_oper), NULL);
	if (se == NULL)
		goto err_out1;

	if (fuse_set_signal_handlers(se) != 0)
		goto err_out2;

	if (fuse_session_mount(se, opts.mountpoint) != 0)
		goto err_out3;

	fuse_daemonize(opts.foreground);

	if (opts.singlethread)
		ret = fuse_session_loop(se);
	else {
		config = fuse_loop_cfg_create();
		fuse_loop_cfg_set_clone_fd(config, opts.clone_fd);
		fuse_loop_cfg_set_max_threads(config, opts.max_threads);
		ret = fuse_session_loop_mt(se, config);
		fuse_loop_cfg_destroy(config);
		config = NULL;
	}

	fuse_session_unmount(se);
err_out3:
	fuse_remove_signal_handlers(se);
err_out2:
	fuse_session_destroy(se);
err_out1:
	free(opts.mountpoint);
	fuse_opt_free_args(&args);

	return ret ? 1 : 0;
}
