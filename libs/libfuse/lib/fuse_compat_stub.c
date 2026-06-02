#include "fuse_i.h"
#include "fuse_common.h"
#include "fuse_lowlevel.h"
#include "fuse_opt.h"
#include <errno.h>

int fuse_kern_mount(const char *mountpoint, struct mount_opts *mo) { return -1; }
void fuse_kern_unmount(const char *mountpoint, int fd) {}
int fuse_mnt_parse_fuse_fd(const char *mountpoint) { return -1; }
void fuse_mount_version(void) {}
unsigned get_max_read(struct mount_opts *opts) { return 0; }
struct mount_opts *parse_mount_opts(struct fuse_args *args) { return NULL; }
void destroy_mount_opts(struct mount_opts *opts) {}

struct fuse *_fuse_new_31(struct fuse_args *args, const struct fuse_operations *op, size_t op_size, struct libfuse_version *version, void *private_data) { return NULL; }
int fuse_mount(struct fuse *f, const char *mountpoint) { return -1; }
void fuse_unmount(struct fuse *f) {}
void fuse_destroy(struct fuse *f) {}
int fuse_loop(struct fuse *f) { return -1; }
int fuse_loop_mt_312(struct fuse *f, struct fuse_loop_config *config) { return -1; }
struct fuse_session *fuse_get_session(struct fuse *f) { return NULL; }
void fuse_lib_help(struct fuse_args *args) {}

struct fuse_loop_config *fuse_loop_cfg_create(void) { return NULL; }
void fuse_loop_cfg_destroy(struct fuse_loop_config *config) {}
void fuse_loop_cfg_set_idle_threads(struct fuse_loop_config *config, unsigned int value) {}
void fuse_loop_cfg_set_max_threads(struct fuse_loop_config *config, unsigned int value) {}
void fuse_loop_cfg_set_clone_fd(struct fuse_loop_config *config, unsigned int value) {}

int fuse_set_signal_handlers(struct fuse_session *se) { (void)se; return 0; }
void fuse_remove_signal_handlers(struct fuse_session *se) { (void)se; }

int fuse_session_loop_mt_312(struct fuse_session *se, struct fuse_loop_config *config)
{
    (void)se; (void)config;
    return -1;  /* multi-threaded loop not supported; use -s (singlethread) */
}

const char *fuse_pkgversion(void) { return "3.12.0-ukfuse"; }

struct fuse_chan *fuse_chan_get(struct fuse_chan *ch) { return ch; }

void fuse_uring_wake_ring_threads(struct fuse_session *se) {}
