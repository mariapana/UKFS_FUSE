#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

void perror(const char *s) {
    printf("%s: error %d\n", s, errno);
}

pid_t fork(void) {
    errno = ENOSYS;
    return -1;
}

void _exit(int status) {
    exit(status);
}

char *realpath(const char *path, char *resolved_path) {
    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    if (resolved_path) {
        strcpy(resolved_path, path);
        return resolved_path;
    }
    return strdup(path);
}

void *aligned_alloc(size_t alignment, size_t size) {
    void *ptr = NULL;
    int ret = posix_memalign(&ptr, alignment, size);
    if (ret != 0) return NULL;
    return ptr;
}

char *getenv(const char *name) {
    return NULL;
}
