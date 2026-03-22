#include "fuse_i.h"
#include <errno.h>

uint32_t fuse_higher_32_bits(uint64_t n) {
    return (uint32_t)(n >> 32);
}

int send_reply_uring(fuse_req_t req, int error, const void *arg, size_t argsize) { return -ENOSYS; }
int fuse_send_msg_uring(fuse_req_t req, struct iovec *iov, int count) { return -ENOSYS; }
int fuse_reply_data_uring(fuse_req_t req, struct fuse_bufvec *bufv, enum fuse_buf_copy_flags flags) { return -ENOSYS; }
int fuse_uring_start(struct fuse_session *se) { return -ENOSYS; }
