// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

// Uneven-workload example: render the Mandelbrot set to ASCII, one image row
// per index. Rows cost wildly different amounts of work -- points inside the
// set run the full iteration budget while points outside bail out early. This
// is exactly where tinypar's dynamic chunk scheduling helps: fast rows do not
// have to wait for a worker that got stuck on a slow row, because idle workers
// keep claiming new chunks until the range is exhausted.

#include "tinypar.h"

#include <stdio.h>
#include <stdlib.h>

#define WIDTH 96
#define HEIGHT 40
#define MAX_ITER 1000

typedef struct render {
    char* pixels;  // HEIGHT * WIDTH grid, one char per pixel
} render_t;

static char shade(int iterations) {
    static const char ramp[] = " .:-=+*#%@";
    if (iterations >= MAX_ITER) return ' ';  // inside the set
    int levels = (int)(sizeof(ramp) - 2);
    return ramp[iterations % levels + 1];
}

static int render_rows(void* context, size_t worker_index,
                       size_t begin, size_t end) {
    render_t* image = context;
    (void)worker_index;
    for (size_t row = begin; row < end; row++) {
        double ci = (double)row / HEIGHT * 2.0 - 1.0;      // [-1, 1]
        for (size_t col = 0; col < WIDTH; col++) {
            double cr = (double)col / WIDTH * 3.0 - 2.0;   // [-2, 1]
            double zr = 0.0, zi = 0.0;
            int iter = 0;
            while (iter < MAX_ITER && zr * zr + zi * zi <= 4.0) {
                double next = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = next;
                iter++;
            }
            image->pixels[row * WIDTH + col] = shade(iter);
        }
    }
    return 0;
}

int main(void) {
    char* pixels = malloc((size_t)HEIGHT * WIDTH);
    if (pixels == NULL) return 1;
    render_t image = { pixels };

    tinypar_config_t config = {
        .item_count = HEIGHT,
        .chunk_size = 2,  // small chunks let scheduling balance uneven rows
        .max_workers = 0
    };

    tinypar_status_t status = tinypar_parallel_for(&config, render_rows, &image);
    if (status != TINYPAR_OK) {
        fprintf(stderr, "render failed: %s\n", tinypar_status_string(status));
        free(pixels);
        return 1;
    }

    for (size_t row = 0; row < HEIGHT; row++) {
        fwrite(&pixels[row * WIDTH], 1, WIDTH, stdout);
        putchar('\n');
    }
    free(pixels);
    return 0;
}
