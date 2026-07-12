# tinypar

tinypar is a small C11 library for running an independent index range in
parallel. It provides dynamic chunk scheduling, caller participation,
cooperative callback failure, and a serial fallback. It is not a task graph,
event loop, or persistent worker pool.

On POSIX platforms tinypar uses pthreads. On Windows it uses `_beginthreadex`
and native Windows thread handles. The public API does not expose either
backend's types.

## Build and test

```sh
make
make test
```

With an MSVC developer prompt:

```text
nmake /f Makefile.msvc
nmake /f Makefile.msvc test
```

POSIX consumers must compile and link with `-pthread`:

```sh
cc -std=c11 -Iinclude example.c lib/libtinypar.a -pthread
```

## API model

`tinypar_parallel_for()` partitions `[0, item_count)` into dynamically claimed
half-open ranges. The callback receives a stable worker index and may run more
than once for the same worker. A non-zero callback result cancels unclaimed
work and returns `TINYPAR_CALLBACK_FAILED` after all started workers join.

Each invocation owns its queue and cancellation state, so calls may run
concurrently or recursively. tinypar does not use a global worker pool.

```c
static int process(void* context, size_t worker, size_t begin, size_t end) {
    (void)worker;
    for (size_t i = begin; i < end; i++) {
        /* Process item i. */
    }
    return 0;
}

tinypar_config_t config = {
    .item_count = count,
    .chunk_size = 16,
    .max_workers = 0  /* platform logical processor count */
};
tinypar_status_t status = tinypar_parallel_for(&config, process, context);
```

## License

tinypar is licensed under the Mozilla Public License, version 2.0. See
`LICENSE`.
