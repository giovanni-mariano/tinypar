// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

// Worked example: driving a Monte Carlo simulation with tinypar.
//
// Each index in [0, history_count) is one independent neutron history, which
// maps directly onto the half-open ranges tinypar hands to the callback. The
// example estimates the uncollided transmission probability through a slab of
// thickness L with total macroscopic cross section sigma_t; the analytic
// answer is exp(-sigma_t * L), so you can see the estimate converge to it.
//
// Build (from the repository root):
//
//     make example
//     ./build/neutron_slab
//
// or link by hand against the installed/static library:
//
//     cc -std=c11 -Iinclude example/neutron_slab.c lib/libtinypar.a -pthread -lm

#include "tinypar.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// A small, well-mixed PRNG. Seeding it from the *global* history index gives
// every history an independent stream whose result does not depend on which
// worker or chunk happens to run it -- so the tally is reproducible no matter
// how tinypar partitions the work.
static uint64_t splitmix64(uint64_t* state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Uniform in (0, 1]; the 1 - u form keeps the sample strictly positive so the
// -log() sampling below never sees log(0).
static double rng_unit(uint64_t* state) {
    double u = (double)(splitmix64(state) >> 11) * (1.0 / 9007199254740992.0);
    return 1.0 - u;
}

typedef struct simulation {
    double sigma_t;         // total macroscopic cross section (1/cm)
    double thickness;       // slab thickness (cm)
    uint64_t base_seed;

    // One tally slot per worker. Each worker writes only to its own slot
    // (indexed by the stable worker index), so no locking is needed; the
    // slots are summed after tinypar_parallel_for() returns.
    uint64_t* transmitted;
    size_t worker_count;
} simulation_t;

// The callback: process the half-open range [begin, end) of histories. It may
// be called several times for the same worker as it claims more chunks, so it
// only ever accumulates -- never resets -- its worker's slot.
static int transport_histories(void* context, size_t worker_index,
                               size_t begin, size_t end) {
    simulation_t* sim = context;
    uint64_t* slot = &sim->transmitted[worker_index];
    for (size_t history = begin; history < end; history++) {
        uint64_t rng = sim->base_seed ^ (uint64_t)history;
        (void)splitmix64(&rng);  // decorrelate consecutive seeds
        double free_flight = -log(rng_unit(&rng)) / sim->sigma_t;
        if (free_flight >= sim->thickness) (*slot)++;  // crossed uncollided
    }
    return 0;  // return non-zero to cancel the run and report a failure
}

int main(void) {
    const size_t history_count = 20000000;

    // max_workers = 0 asks tinypar for the platform's logical processor count.
    tinypar_config_t config = {
        .item_count = history_count,
        .chunk_size = 8192,
        .max_workers = 0
    };

    // Ask how many workers this configuration will actually use so we can size
    // the per-worker tally array to match.
    size_t workers = tinypar_effective_workers(config.item_count,
                                               config.chunk_size,
                                               config.max_workers);
    uint64_t* transmitted = calloc(workers, sizeof(*transmitted));
    if (transmitted == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    simulation_t sim = {
        .sigma_t = 1.5,
        .thickness = 2.0,
        .base_seed = 0xC0FFEEu,
        .transmitted = transmitted,
        .worker_count = workers
    };

    printf("Simulating %zu histories across %zu worker(s)...\n",
           history_count, workers);

    tinypar_status_t status =
        tinypar_parallel_for(&config, transport_histories, &sim);
    if (status != TINYPAR_OK) {
        fprintf(stderr, "simulation failed: %s\n", tinypar_status_string(status));
        free(transmitted);
        return 1;
    }

    // Reduce the per-worker tallies into a single count.
    uint64_t total_transmitted = 0;
    for (size_t w = 0; w < workers; w++) total_transmitted += transmitted[w];

    double estimate = (double)total_transmitted / (double)history_count;
    double analytic = exp(-sim.sigma_t * sim.thickness);
    printf("transmission estimate: %.6f\n", estimate);
    printf("analytic  exp(-St*L): %.6f\n", analytic);
    printf("absolute error:        %.2e\n", fabs(estimate - analytic));

    free(transmitted);
    return 0;
}
