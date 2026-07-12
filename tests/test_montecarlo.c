// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

// Exercises tinypar as the parallel driver of a Monte Carlo neutron transport
// code. Every index in [0, item_count) is one independent neutron history, so
// the histories map directly onto tinypar's dynamically claimed ranges.
//
// The two properties an MC code needs from its parallel driver are checked
// here: statistical correctness (estimates converge to the analytic answer)
// and reproducibility (per-history seeding makes the tally independent of how
// tinypar happens to partition the work across workers and chunks).

#include "tinypar.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

// splitmix64: a small, well-mixed PRNG. Seeding it from the global history
// index gives every history an independent, reproducible stream that does not
// depend on which worker or chunk executes the history.
static uint64_t splitmix64(uint64_t* state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Uniform in (0, 1]. The 1 - u form keeps the sample strictly positive so the
// -log() sampling below never sees log(0).
static double rng_unit(uint64_t* state) {
    double u = (double)(splitmix64(state) >> 11) * (1.0 / 9007199254740992.0);
    return 1.0 - u;
}

static uint64_t history_seed(uint64_t base_seed, size_t history_index) {
    // Mix the index into the base seed so consecutive histories start far
    // apart in the stream.
    return splitmix64(&(uint64_t){ base_seed ^ (uint64_t)history_index });
}

// Per-worker tally slots. Each worker touches only its own slot, so the shared
// accumulator array needs no locking; results are reduced after the join.
// Counts are unsigned integers, so the reduction is exact and order
// independent, which is what makes the parallel tally bit-for-bit reproducible.
typedef struct tally {
    uint64_t transmitted;   // histories that crossed the slab uncollided
    uint64_t collisions;    // total collisions over all histories
    uint64_t histories;     // histories processed (exactly-once check)
} tally_t;

typedef struct sim_context {
    uint64_t base_seed;
    double sigma_t;         // total macroscopic cross section (1/cm)
    double sigma_a;         // absorption cross section (1/cm)
    double thickness;       // slab thickness (cm)
    tally_t* worker_tally;  // one slot per worker
} sim_context_t;

// Uncollided transmission through a purely attenuating slab. A history that
// samples a free-flight distance past the slab boundary is transmitted; the
// expected transmitted fraction is exp(-sigma_t * thickness).
static int run_transmission(void* opaque, size_t worker_index,
                            size_t begin, size_t end) {
    sim_context_t* sim = opaque;
    tally_t* slot = &sim->worker_tally[worker_index];
    for (size_t i = begin; i < end; i++) {
        uint64_t rng = history_seed(sim->base_seed, i);
        double flight = -log(rng_unit(&rng)) / sim->sigma_t;
        if (flight >= sim->thickness) slot->transmitted++;
        slot->histories++;
    }
    return 0;
}

// Infinite-medium random walk: at each collision the neutron is absorbed with
// probability sigma_a / sigma_t, otherwise it scatters and collides again. The
// mean number of collisions per history is sigma_t / sigma_a.
static int run_random_walk(void* opaque, size_t worker_index,
                           size_t begin, size_t end) {
    sim_context_t* sim = opaque;
    tally_t* slot = &sim->worker_tally[worker_index];
    double p_absorb = sim->sigma_a / sim->sigma_t;
    for (size_t i = begin; i < end; i++) {
        uint64_t rng = history_seed(sim->base_seed, i);
        for (;;) {
            slot->collisions++;
            if (rng_unit(&rng) <= p_absorb) break;  // absorbed
        }
        slot->histories++;
    }
    return 0;
}

// Models a corrupt cross-section lookup mid-run: the history at bad_index
// reports failure, which must cancel the remaining histories and surface as
// TINYPAR_CALLBACK_FAILED.
typedef struct abort_context {
    size_t bad_index;
} abort_context_t;

static int run_with_bad_history(void* opaque, size_t worker_index,
                                size_t begin, size_t end) {
    abort_context_t* ctx = opaque;
    (void)worker_index;
    return begin <= ctx->bad_index && ctx->bad_index < end ? -1 : 0;
}

static tally_t reduce_tally(const tally_t* slots, size_t workers) {
    tally_t total = { 0, 0, 0 };
    for (size_t w = 0; w < workers; w++) {
        total.transmitted += slots[w].transmitted;
        total.collisions += slots[w].collisions;
        total.histories += slots[w].histories;
    }
    return total;
}

// The transmitted fraction must converge to exp(-sigma_t * thickness). With
// N histories the standard error of the estimate is sqrt(p(1-p)/N); a five
// sigma band keeps the test overwhelmingly unlikely to fail by chance while
// still catching a broken sampler.
static int test_transmission_converges(void) {
    const size_t histories = 4000000;
    const size_t workers = 4;
    tally_t slots[4] = {{ 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }};

    sim_context_t sim = { 0xC0FFEEu, 1.5, 0.0, 2.0, slots };
    tinypar_config_t config = { histories, 4096, workers };
    CHECK(tinypar_parallel_for(&config, run_transmission, &sim) == TINYPAR_OK);

    tally_t total = reduce_tally(slots, workers);
    CHECK(total.histories == histories);

    double estimate = (double)total.transmitted / (double)histories;
    double expected = exp(-sim.sigma_t * sim.thickness);
    double stderr_ = sqrt(expected * (1.0 - expected) / (double)histories);
    CHECK(fabs(estimate - expected) < 5.0 * stderr_);
    return 0;
}

// The mean collision count per history must converge to sigma_t / sigma_a.
static int test_mean_collisions_converges(void) {
    const size_t histories = 2000000;
    const size_t workers = 4;
    tally_t slots[4] = {{ 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }};

    sim_context_t sim = { 0x5EEDu, 2.0, 0.5, 0.0, slots };
    tinypar_config_t config = { histories, 2048, workers };
    CHECK(tinypar_parallel_for(&config, run_random_walk, &sim) == TINYPAR_OK);

    tally_t total = reduce_tally(slots, workers);
    CHECK(total.histories == histories);

    double mean = (double)total.collisions / (double)histories;
    double expected = sim.sigma_t / sim.sigma_a;  // = 4 collisions
    // Collision count is geometric; variance = (1 - p) / p^2 with p = 1/expected.
    double p = 1.0 / expected;
    double stderr_ = sqrt((1.0 - p) / (p * p) / (double)histories);
    CHECK(fabs(mean - expected) < 5.0 * stderr_);
    return 0;
}

// The defining property for a parallel MC code: because each history is seeded
// from its global index, the summed tally must be identical no matter how many
// workers run or how the range is chunked. Run one reference configuration and
// several alternatives and require exact equality of the integer tallies.
static int run_transmission_tally(size_t histories, size_t chunk_size,
                                  size_t workers, tally_t* out) {
    tally_t* slots = calloc(workers, sizeof(*slots));
    CHECK(slots != NULL);

    sim_context_t sim = { 0xABCDEF01u, 1.5, 0.0, 2.0, slots };
    tinypar_config_t config = { histories, chunk_size, workers };
    CHECK(tinypar_parallel_for(&config, run_transmission, &sim) == TINYPAR_OK);

    *out = reduce_tally(slots, workers);
    free(slots);
    return 0;
}

static int test_reproducible_across_workers(void) {
    const size_t histories = 500009;  // prime: never evenly chunked
    tally_t reference;
    CHECK(run_transmission_tally(histories, 1, 1, &reference) == 0);
    CHECK(reference.histories == histories);
    CHECK(reference.transmitted > 0);

    const size_t chunk_sizes[] = { 1, 3, 64, 997, 500009 };
    const size_t worker_counts[] = { 1, 2, 4, 8 };
    for (size_t c = 0; c < sizeof(chunk_sizes) / sizeof(*chunk_sizes); c++) {
        for (size_t w = 0; w < sizeof(worker_counts) / sizeof(*worker_counts); w++) {
            tally_t got;
            CHECK(run_transmission_tally(histories, chunk_sizes[c],
                                         worker_counts[w], &got) == 0);
            CHECK(got.histories == reference.histories);
            CHECK(got.transmitted == reference.transmitted);
        }
    }
    return 0;
}

static int test_abort_propagates(void) {
    abort_context_t ctx = { 733 };
    tinypar_config_t config = { 100000, 32, 4 };
    CHECK(tinypar_parallel_for(&config, run_with_bad_history, &ctx) ==
          TINYPAR_CALLBACK_FAILED);
    return 0;
}

int main(void) {
    CHECK(test_transmission_converges() == 0);
    CHECK(test_mean_collisions_converges() == 0);
    CHECK(test_reproducible_across_workers() == 0);
    CHECK(test_abort_propagates() == 0);
    puts("tinypar montecarlo tests: passed");
    return 0;
}
