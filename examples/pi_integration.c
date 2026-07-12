// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

// Parallel reduction example: estimate pi by numerically integrating
//
//     integral_0^1 4 / (1 + x^2) dx = pi
//
// with the midpoint rule. This shows the standard reduction pattern: give each
// worker its own partial-sum slot (indexed by the stable worker index) so the
// hot loop needs no locking, then combine the slots after the parallel region.
// The result is deterministic -- integer-partitioned work, no randomness.

#include "tinypar.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct integration {
    size_t steps;      // number of subintervals
    double* partial;   // one accumulator per worker
} integration_t;

static int accumulate_area(void* context, size_t worker_index,
                           size_t begin, size_t end) {
    integration_t* job = context;
    double width = 1.0 / (double)job->steps;
    double sum = 0.0;
    for (size_t i = begin; i < end; i++) {
        double x = ((double)i + 0.5) * width;  // midpoint of subinterval i
        sum += 4.0 / (1.0 + x * x);
    }
    // A worker may be called several times as it claims more chunks, so add to
    // the slot rather than overwriting it.
    job->partial[worker_index] += sum * width;
    return 0;
}

int main(void) {
    const size_t steps = 100000000;

    tinypar_config_t config = {
        .item_count = steps,
        .chunk_size = 65536,
        .max_workers = 0
    };

    // Size the accumulator array to the number of workers tinypar will use.
    size_t workers = tinypar_effective_workers(config.item_count,
                                               config.chunk_size,
                                               config.max_workers);
    double* partial = calloc(workers, sizeof(*partial));
    if (partial == NULL) return 1;

    integration_t job = { steps, partial };
    tinypar_status_t status =
        tinypar_parallel_for(&config, accumulate_area, &job);
    if (status != TINYPAR_OK) {
        fprintf(stderr, "parallel_for failed: %s\n",
                tinypar_status_string(status));
        free(partial);
        return 1;
    }

    double pi = 0.0;
    for (size_t w = 0; w < workers; w++) pi += partial[w];

    printf("workers used: %zu\n", workers);
    printf("pi estimate:  %.12f\n", pi);
    printf("reference:    3.141592653590\n");
    free(partial);
    return 0;
}
