// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

// Minimal example: apply a pure function to every element of an array in
// parallel. This is the "hello world" of tinypar -- each index is one
// independent element, and the callback processes a contiguous slice.

#include "tinypar.h"

#include <stdio.h>
#include <stdlib.h>

// The callback processes the half-open range [begin, end). Because elements
// are independent and each is written exactly once, no synchronization is
// needed even though several workers run concurrently.
static int square_range(void* context, size_t worker_index,
                        size_t begin, size_t end) {
    double* data = context;
    (void)worker_index;
    for (size_t i = begin; i < end; i++) data[i] = data[i] * data[i];
    return 0;
}

int main(void) {
    const size_t n = 1000000;
    double* data = malloc(n * sizeof(*data));
    if (data == NULL) return 1;
    for (size_t i = 0; i < n; i++) data[i] = (double)i;

    tinypar_config_t config = {
        .item_count = n,
        .chunk_size = 4096,
        .max_workers = 0  // 0 = platform logical processor count
    };

    tinypar_status_t status = tinypar_parallel_for(&config, square_range, data);
    if (status != TINYPAR_OK) {
        fprintf(stderr, "parallel_for failed: %s\n",
                tinypar_status_string(status));
        free(data);
        return 1;
    }

    printf("data[0]=%.0f data[2]=%.0f data[%zu]=%.0f\n",
           data[0], data[2], n - 1, data[n - 1]);
    free(data);
    return 0;
}
