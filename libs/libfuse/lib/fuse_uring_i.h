/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Unikraft stub for io_uring support
 * io_uring is not supported in Unikraft
 */
#ifndef FUSE_URING_I_H
#define FUSE_URING_I_H

/* Stub - io_uring not supported */

#include <stdint.h>
#include <stddef.h>
struct fuse_session;
struct fuse_chan;
struct iovec;
struct fuse_bufvec;
typedef struct fuse_req *fuse_req_t;

uint32_t fuse_higher_32_bits(uint64_t n);
int send_reply_uring(fuse_req_t req, int error, const void *arg, size_t argsize);
int fuse_send_msg_uring(fuse_req_t req, struct iovec *iov, int count);
int fuse_reply_data_uring(fuse_req_t req, struct fuse_bufvec *bufv, enum fuse_buf_copy_flags flags);
int fuse_uring_start(struct fuse_session *se);
void fuse_uring_wake_ring_threads(struct fuse_session *se);

#endif /* FUSE_URING_I_H */
