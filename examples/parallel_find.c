// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

// Cooperative cancellation example: search a large array for an index whose
// value satisfies a predicate. When the callback returns non-zero, tinypar
// stops handing out unclaimed work and, after the running workers finish their
// current chunk, tinypar_parallel_for() returns TINYPAR_CALLBACK_FAILED. Here
// we use that "failure" deliberately as a found-signal to avoid scanning the
// rest of the array.
//
// Each worker records its own hit in its own slot (indexed by the stable worker
// index), so no locking or atomics are needed; after the join we reduce the
// slots to the smallest matching index that was seen.

#include "tinypar.h"

#include <stdio.h>
#include <stdlib.h>

#define NOT_FOUND ((size_t)-1)

typedef struct search {
    const int* data;
    int target;
    size_t* hit;  // one slot per worker; NOT_FOUND until that worker matches
} search_t;

static int scan_range(void* context, size_t worker_index,
                      size_t begin, size_t end) {
    search_t* search = context;
    for (size_t i = begin; i < end; i++) {
        if (search->data[i] == search->target) {
            search->hit[worker_index] = i;
            return 1;  // cancel the remaining work
        }
    }
    return 0;
}

int main(void) {
    const size_t n = 50000000;
    int* data = malloc(n * sizeof(*data));
    if (data == NULL) return 1;
    for (size_t i = 0; i < n; i++) data[i] = (int)(i % 1000);
    const size_t planted = 37500000;
    data[planted] = 4242;  // the needle

    tinypar_config_t config = {
        .item_count = n,
        .chunk_size = 8192,
        .max_workers = 0
    };

    size_t workers = tinypar_effective_workers(config.item_count,
                                               config.chunk_size,
                                               config.max_workers);
    size_t* hit = malloc(workers * sizeof(*hit));
    if (hit == NULL) {
        free(data);
        return 1;
    }
    for (size_t w = 0; w < workers; w++) hit[w] = NOT_FOUND;

    search_t search = { data, 4242, hit };
    tinypar_status_t status = tinypar_parallel_for(&config, scan_range, &search);

    // Reduce the per-worker hits to the smallest matching index.
    size_t found = NOT_FOUND;
    for (size_t w = 0; w < workers; w++) {
        if (hit[w] < found) found = hit[w];
    }

    // A search that finds nothing returns TINYPAR_OK; a hit surfaces as
    // TINYPAR_CALLBACK_FAILED, which is the cancellation we asked for.
    if (status == TINYPAR_CALLBACK_FAILED && found != NOT_FOUND) {
        printf("found target at index %zu (planted at %zu)\n", found, planted);
    } else if (status == TINYPAR_OK) {
        printf("target not present\n");
    } else {
        fprintf(stderr, "parallel_for error: %s\n",
                tinypar_status_string(status));
        free(hit);
        free(data);
        return 1;
    }

    free(hit);
    free(data);
    return 0;
}
