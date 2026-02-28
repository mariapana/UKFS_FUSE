/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Unikraft stubs for libfuse utilities
 */
#ifndef FUSE_UTIL_H
#define FUSE_UTIL_H

#include <stdlib.h>
#include <errno.h>

/* Simple string to long conversion */
static inline int libfuse_strtol(const char *str, long *out)
{
	char *endptr;
	long val;
	
	errno = 0;
	val = strtol(str, &endptr, 10);
	
	if (errno != 0 || *endptr != '\0')
		return -EINVAL;
	
	*out = val;
	return 0;
}

#endif /* FUSE_UTIL_H */
