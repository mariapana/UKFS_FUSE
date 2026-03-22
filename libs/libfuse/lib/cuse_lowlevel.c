#define FUSE_USE_VERSION 31
#include "fuse_lowlevel.h"
#include <errno.h>

void cuse_lowlevel_init(fuse_req_t req, fuse_ino_t nodeid, const void *inarg) {
    fuse_reply_err(req, ENOSYS);
}
void _cuse_lowlevel_init(fuse_req_t req, fuse_ino_t nodeid, const void *inarg) {
    fuse_reply_err(req, ENOSYS);
}
