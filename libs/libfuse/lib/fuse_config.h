/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Unikraft-specific configuration for libfuse
 * Replaces fuse_config.h from autotools
 */
#ifndef FUSE_CONFIG_H
#define FUSE_CONFIG_H

/* Unikraft doesn't have these */
#undef HAVE_SPLICE
#undef HAVE_VMSPLICE
#undef HAVE_PIPE2
#undef HAVE_UTIMENSAT
#undef HAVE_POSIX_FALLOCATE
#undef HAVE_SETXATTR
#undef HAVE_LIBICONV

/* We don't use io_uring in Unikraft */
#undef HAVE_LIBURING

/* Disable USDT tracing */
#undef USDT_ENABLED

/* Define what we do have */
#define HAVE_STRUCT_STAT_ST_ATIM 1
#define HAVE_STRUCT_STAT_ST_MTIM 1
#define HAVE_STRUCT_STAT_ST_CTIM 1

#endif /* FUSE_CONFIG_H */
