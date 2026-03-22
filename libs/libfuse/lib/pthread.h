#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <uk/mutex.h>
#include <uk/thread.h>

typedef struct uk_mutex pthread_mutex_t;
typedef struct uk_thread *pthread_t;
typedef int pthread_key_t;

#define PTHREAD_MUTEX_INITIALIZER UK_MUTEX_INITIALIZER(0)

static inline int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr) {
    uk_mutex_init(mutex);
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *mutex) {
    uk_mutex_lock(mutex);
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    uk_mutex_unlock(mutex);
    return 0;
}

static inline int pthread_create(pthread_t *thread, const void *attr, void *(*start_routine) (void *), void *arg) {
    return -1;
}

static inline int pthread_join(pthread_t thread, void **retval) {
    return -1;
}

static inline int pthread_cancel(pthread_t thread) {
    return -1;
}

static inline int pthread_key_create(pthread_key_t *key, void (*destructor)(void*)) {
    return -1;
}

static inline int pthread_key_delete(pthread_key_t key) {
    return -1;
}

static inline int pthread_setspecific(pthread_key_t key, const void *value) {
    return -1;
}

static inline void *pthread_getspecific(pthread_key_t key) {
    return NULL;
}

#endif /* _PTHREAD_H */
