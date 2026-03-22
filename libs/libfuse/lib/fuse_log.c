#include "fuse_log.h"
#include <stdio.h>
#include <stdarg.h>

static fuse_log_func_t current_log_func = NULL;

void fuse_set_log_func(fuse_log_func_t func) {
    current_log_func = func;
}

void fuse_log(enum fuse_log_level level, const char *fmt, ...) {
    if (current_log_func) {
        va_list ap;
        va_start(ap, fmt);
        current_log_func(level, fmt, ap);
        va_end(ap);
    } else {
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
}
