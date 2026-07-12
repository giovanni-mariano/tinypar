// SPDX-License-Identifier: MPL-2.0

#ifndef TINYPAR_H
#define TINYPAR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TINYPAR_VERSION_MAJOR 0
#define TINYPAR_VERSION_MINOR 1
#define TINYPAR_VERSION_PATCH 0

typedef enum tinypar_status {
    TINYPAR_OK = 0,
    TINYPAR_INVALID_ARGUMENT,
    TINYPAR_ALLOCATION_FAILED,
    TINYPAR_THREAD_CREATE_FAILED,
    TINYPAR_THREAD_JOIN_FAILED,
    TINYPAR_CALLBACK_FAILED
} tinypar_status_t;

/**
 * Process one half-open range [begin, end). A callback may be called multiple
 * times for a worker. Returning non-zero cancels unclaimed work and makes
 * tinypar_parallel_for() return TINYPAR_CALLBACK_FAILED.
 */
typedef int (*tinypar_range_fn)(void* context, size_t worker_index,
                                size_t begin, size_t end);

typedef struct tinypar_config {
    size_t item_count;
    size_t chunk_size;
    /* Zero selects the platform's logical processor count. */
    size_t max_workers;
} tinypar_config_t;

/** Return the platform-reported logical processor count, never less than one. */
size_t tinypar_hardware_threads(void);

/**
 * Return the number of workers that tinypar_parallel_for() would use.
 * Returns zero when chunk_size is zero for non-empty work.
 */
size_t tinypar_effective_workers(size_t item_count, size_t chunk_size,
                                 size_t max_workers);

/**
 * Run callback over [0, config->item_count) using dynamically claimed ranges.
 * The calling thread participates as worker zero. This function is safe to
 * invoke concurrently and recursively; every invocation owns its own state.
 */
tinypar_status_t tinypar_parallel_for(const tinypar_config_t* config,
                                      tinypar_range_fn callback,
                                      void* context);

/** Return a stable textual description of a tinypar status code. */
const char* tinypar_status_string(tinypar_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* TINYPAR_H */
