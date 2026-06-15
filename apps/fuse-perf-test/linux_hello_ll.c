#define FUSE_USE_VERSION 31
#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *hello_str = "Hello World!\n";
static const char *hello_name = "hello";
static const char *bigfile_name = "bigfile";
#define BIGFILE_SIZE (100 * 1024 * 1024) // 100 MB

static void hello_ll_getattr(fuse_req_t req, fuse_ino_t ino,
			     struct fuse_file_info *fi)
{
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
	} else if (ino == 3) {
		stbuf.st_mode = S_IFREG | 0444;
		stbuf.st_nlink = 1;
		stbuf.st_size = BIGFILE_SIZE;
	} else {
		fuse_reply_err(req, ENOENT);
		return;
	}
	fuse_reply_attr(req, &stbuf, 0.0);
}

static void hello_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct fuse_entry_param e = {0};

	if (parent == 1 && strcmp(name, hello_name) == 0) {
		e.ino = 2;
		e.attr.st_ino = 2;
		e.attr.st_mode = S_IFREG | 0444;
		e.attr.st_nlink = 1;
		e.attr.st_size = strlen(hello_str);
	} else if (parent == 1 && strcmp(name, bigfile_name) == 0) {
		e.ino = 3;
		e.attr.st_ino = 3;
		e.attr.st_mode = S_IFREG | 0444;
		e.attr.st_nlink = 1;
		e.attr.st_size = BIGFILE_SIZE;
	} else {
		fuse_reply_err(req, ENOENT);
		return;
	}

	e.attr_timeout = 0.0;
	e.entry_timeout = 0.0;

	fuse_reply_entry(req, &e);
}

static void hello_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			  off_t off, struct fuse_file_info *fi)
{
	(void)fi;
	if (ino == 2) {
		size_t len = strlen(hello_str);
		if (off >= (off_t)len) {
			fuse_reply_buf(req, NULL, 0);
			return;
		}
		size_t to_read = len - (size_t)off;
		if (to_read > size)
			to_read = size;
		fuse_reply_buf(req, hello_str + off, to_read);
	} else if (ino == 3) {
		if (off >= BIGFILE_SIZE) {
			fuse_reply_buf(req, NULL, 0);
			return;
		}
		size_t to_read = BIGFILE_SIZE - (size_t)off;
		if (to_read > size)
			to_read = size;
		
		/* We allocate a temporary buffer full of 'A's to simulate a real read */
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
		{".",       1, S_IFDIR},
		{"..",      1, S_IFDIR},
		{"hello",   2, S_IFREG},
		{"bigfile", 3, S_IFREG},
	};
	int nentries = 4;

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

static void hello_ll_open(fuse_req_t req, fuse_ino_t ino,
			  struct fuse_file_info *fi)
{
	if (ino != 2 && ino != 3) {
		fuse_reply_err(req, EISDIR);
		return;
	}
	/* Bypass page cache to measure true IPC FUSE overhead */
	fi->direct_io = 1;
	fuse_reply_open(req, fi);
}

static const struct fuse_lowlevel_ops hello_ll_oper = {
	.lookup		= hello_ll_lookup,
	.getattr	= hello_ll_getattr,
	.open       = hello_ll_open,
	.read		= hello_ll_read,
	.readdir	= hello_ll_readdir,
};

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_session *se;
	struct fuse_cmdline_opts opts;
	struct fuse_loop_config config;
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

	if(opts.mountpoint == NULL) {
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

	/* Block until ctrl+c or fusermount -u */
	config.clone_fd = opts.clone_fd;
	config.max_idle_threads = opts.max_idle_threads;
	ret = fuse_session_loop_mt(se, &config);

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
