// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#if !defined(_WIN32)

#include "tinypar_platform.h"

#include <unistd.h>

size_t tinypar_platform_hardware_threads(void) {
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? (size_t)count : 1;
}

int tinypar_thread_start(tinypar_thread_t* thread, tinypar_thread_entry_t entry,
                         void* argument) {
    return pthread_create(thread, NULL, entry, argument) == 0;
}

int tinypar_thread_join(tinypar_thread_t* thread) {
    return pthread_join(*thread, NULL) == 0;
}

#endif
