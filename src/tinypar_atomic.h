// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#ifndef TINYPAR_ATOMIC_H
#define TINYPAR_ATOMIC_H

#include <stddef.h>

#if defined(_MSC_VER) && !defined(__clang__) && \
    (defined(__STDC_NO_ATOMICS__) || !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L)

#include <intrin.h>

typedef volatile size_t tinypar_atomic_size_t;
typedef volatile long tinypar_atomic_int_t;

static inline void tinypar_atomic_size_init(tinypar_atomic_size_t* value,
                                            size_t initial) {
    *value = initial;
}

static inline size_t tinypar_atomic_size_fetch_add(tinypar_atomic_size_t* value,
                                                    size_t increment) {
    if (sizeof(size_t) == 8) {
        return (size_t)_InterlockedExchangeAdd64((volatile __int64*)value,
                                                 (__int64)increment);
    }
    return (size_t)_InterlockedExchangeAdd((volatile long*)value,
                                           (long)increment);
}

static inline void tinypar_atomic_int_init(tinypar_atomic_int_t* value,
                                           int initial) {
    *value = (long)initial;
}

static inline int tinypar_atomic_int_load(const tinypar_atomic_int_t* value) {
    return (int)_InterlockedCompareExchange((volatile long*)value, 0, 0);
}

static inline void tinypar_atomic_int_store(tinypar_atomic_int_t* value,
                                            int desired) {
    (void)_InterlockedExchange(value, (long)desired);
}

static inline int tinypar_atomic_int_compare_exchange(tinypar_atomic_int_t* value,
                                                       int* expected,
                                                       int desired) {
    long observed = _InterlockedCompareExchange(value, (long)desired,
                                                (long)*expected);
    if (observed == (long)*expected) return 1;
    *expected = (int)observed;
    return 0;
}

#else

#include <stdatomic.h>

typedef _Atomic size_t tinypar_atomic_size_t;
typedef _Atomic int tinypar_atomic_int_t;

static inline void tinypar_atomic_size_init(tinypar_atomic_size_t* value,
                                            size_t initial) {
    atomic_init(value, initial);
}

static inline size_t tinypar_atomic_size_fetch_add(tinypar_atomic_size_t* value,
                                                    size_t increment) {
    return atomic_fetch_add_explicit(value, increment, memory_order_relaxed);
}

static inline void tinypar_atomic_int_init(tinypar_atomic_int_t* value,
                                           int initial) {
    atomic_init(value, initial);
}

static inline int tinypar_atomic_int_load(const tinypar_atomic_int_t* value) {
    return atomic_load_explicit(value, memory_order_acquire);
}

static inline void tinypar_atomic_int_store(tinypar_atomic_int_t* value,
                                            int desired) {
    atomic_store_explicit(value, desired, memory_order_release);
}

static inline int tinypar_atomic_int_compare_exchange(tinypar_atomic_int_t* value,
                                                       int* expected,
                                                       int desired) {
    return atomic_compare_exchange_strong_explicit(
        value, expected, desired, memory_order_acq_rel, memory_order_acquire);
}

#endif

#endif /* TINYPAR_ATOMIC_H */
