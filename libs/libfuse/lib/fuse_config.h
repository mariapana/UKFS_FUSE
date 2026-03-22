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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>

/* Unikraft specifics */
#include <uk/essentials.h>
#include <uk/fuse.h>
#define PACKAGE_VERSION "3.18.0-unikraft"
#define SESSION_DEF_URING_ENABLE 0
#define SESSION_DEF_URING_Q_DEPTH 0
#define container_of(ptr, type, member) __containerof(ptr, type, member)
#define ROUND_UP(v, d) (((v) + (d) - 1) & ~((d) - 1))
#undef HAVE_FLOCK_L_PID

static inline uint32_t fuse_lower_32_bits(uint64_t n) { return (uint32_t)n; }
static inline int getpagesize(void) { return 4096; }

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>

/* Missing prototypes in nolibc */
void perror(const char *s);
pid_t fork(void);
void _exit(int status);
int pipe(int pipefd[2]);
pid_t setsid(void);
void *aligned_alloc(size_t alignment, size_t size);
char *getenv(const char *name);
uid_t getuid(void);
int fuse_mnt_parse_fuse_fd(const char *mountpoint);
char *realpath(const char *path, char *resolved_path);

#endif /* FUSE_CONFIG_H */
