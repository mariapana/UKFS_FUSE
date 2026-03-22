#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#include <uk/semaphore.h>

typedef struct uk_semaphore sem_t;

static inline int sem_init(sem_t *sem, int pshared, unsigned int value) {
    uk_semaphore_init(sem, value);
    return 0;
}

static inline int sem_destroy(sem_t *sem) {
    return 0;
}

static inline int sem_wait(sem_t *sem) {
    uk_semaphore_down(sem);
    return 0;
}

static inline int sem_post(sem_t *sem) {
    uk_semaphore_up(sem);
    return 0;
}

#endif /* _SEMAPHORE_H */
