#ifndef _STDATOMIC_H
#define _STDATOMIC_H

#define atomic_exchange(obj, desired) __atomic_exchange_n(obj, desired, __ATOMIC_SEQ_CST)
#define atomic_store_explicit(obj, desired, order) __atomic_store_n(obj, desired, order)
#define atomic_load_explicit(obj, order) __atomic_load_n(obj, order)
#define memory_order_acquire __ATOMIC_ACQUIRE
#define memory_order_release __ATOMIC_RELEASE
#define memory_order_relaxed __ATOMIC_RELAXED

#endif /* _STDATOMIC_H */
