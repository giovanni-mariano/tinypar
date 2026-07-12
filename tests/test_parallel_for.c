// SPDX-License-Identifier: MPL-2.0

#include "tinypar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct visit_context {
    unsigned char* visits;
} visit_context_t;

static int mark_range(void* opaque, size_t worker_index,
                      size_t begin, size_t end) {
    visit_context_t* context = opaque;
    (void)worker_index;
    for (size_t i = begin; i < end; i++) context->visits[i]++;
    return 0;
}

typedef struct failure_context {
    size_t fail_item;
} failure_context_t;

static int fail_on_range(void* opaque, size_t worker_index,
                         size_t begin, size_t end) {
    failure_context_t* context = opaque;
    (void)worker_index;
    return begin <= context->fail_item && context->fail_item < end ? -1 : 0;
}

static int zero_call_count = 0;

static int count_zero_call(void* opaque, size_t worker_index,
                           size_t begin, size_t end) {
    (void)opaque;
    (void)worker_index;
    (void)begin;
    (void)end;
    zero_call_count++;
    return 0;
}

static int test_exact_once(size_t worker_count) {
    const size_t item_count = 100003;
    unsigned char* visits = calloc(item_count, sizeof(*visits));
    CHECK(visits != NULL);

    visit_context_t context = { visits };
    tinypar_config_t config = { item_count, 7, worker_count };
    CHECK(tinypar_parallel_for(&config, mark_range, &context) == TINYPAR_OK);
    for (size_t i = 0; i < item_count; i++) CHECK(visits[i] == 1);

    free(visits);
    return 0;
}

static int test_callback_failure(void) {
    failure_context_t context = { 91 };
    tinypar_config_t config = { 1000, 5, 4 };
    CHECK(tinypar_parallel_for(&config, fail_on_range, &context) ==
          TINYPAR_CALLBACK_FAILED);
    return 0;
}

static int test_empty_and_invalid(void) {
    tinypar_config_t empty = { 0, 0, 4 };
    zero_call_count = 0;
    CHECK(tinypar_parallel_for(&empty, count_zero_call, NULL) == TINYPAR_OK);
    CHECK(zero_call_count == 0);

    tinypar_config_t invalid = { 1, 0, 1 };
    CHECK(tinypar_parallel_for(&invalid, count_zero_call, NULL) ==
          TINYPAR_INVALID_ARGUMENT);
    CHECK(tinypar_parallel_for(NULL, count_zero_call, NULL) ==
          TINYPAR_INVALID_ARGUMENT);
    CHECK(tinypar_parallel_for(&invalid, NULL, NULL) ==
          TINYPAR_INVALID_ARGUMENT);
    return 0;
}

static int test_worker_limits(void) {
    CHECK(tinypar_hardware_threads() >= 1);
    CHECK(tinypar_effective_workers(0, 0, 0) == 1);
    CHECK(tinypar_effective_workers(10, 0, 1) == 0);
    CHECK(tinypar_effective_workers(10, 3, 100) == 4);
    CHECK(tinypar_effective_workers(10, 3, 1) == 1);
    CHECK(strcmp(tinypar_status_string(TINYPAR_OK), "success") == 0);
    return 0;
}

typedef struct nested_context {
    unsigned char* visits;
    tinypar_status_t inner_status;
} nested_context_t;

static int nested_inner(void* opaque, size_t worker_index,
                        size_t begin, size_t end) {
    visit_context_t* context = opaque;
    (void)worker_index;
    for (size_t i = begin; i < end; i++) context->visits[i]++;
    return 0;
}

static int nested_outer(void* opaque, size_t worker_index,
                        size_t begin, size_t end) {
    nested_context_t* context = opaque;
    (void)worker_index;
    (void)end;
    if (begin != 0) return 0;

    visit_context_t inner = { context->visits };
    tinypar_config_t config = { 257, 3, 2 };
    context->inner_status = tinypar_parallel_for(&config, nested_inner, &inner);
    return context->inner_status == TINYPAR_OK ? 0 : -1;
}

static int test_nested_invocation(void) {
    unsigned char* visits = calloc(257, sizeof(*visits));
    CHECK(visits != NULL);

    nested_context_t context = { visits, TINYPAR_OK };
    tinypar_config_t config = { 32, 4, 4 };
    CHECK(tinypar_parallel_for(&config, nested_outer, &context) == TINYPAR_OK);
    CHECK(context.inner_status == TINYPAR_OK);
    for (size_t i = 0; i < 257; i++) CHECK(visits[i] == 1);

    free(visits);
    return 0;
}

int main(void) {
    CHECK(test_exact_once(1) == 0);
    CHECK(test_exact_once(4) == 0);
    CHECK(test_callback_failure() == 0);
    CHECK(test_empty_and_invalid() == 0);
    CHECK(test_worker_limits() == 0);
    CHECK(test_nested_invocation() == 0);
    puts("tinypar tests: passed");
    return 0;
}
