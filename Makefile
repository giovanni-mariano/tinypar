# SPDX-FileCopyrightText: 2026 Giovanni MARIANO
#
# SPDX-License-Identifier: MPL-2.0

CC ?= cc
AR ?= ar
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
CPPFLAGS ?= -Iinclude -Isrc
PREFIX ?= /usr/local
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig

BUILD_DIR := build
LIB_DIR := lib
LIBRARY := $(LIB_DIR)/libtinypar.a
TEST := $(BUILD_DIR)/test_parallel_for
MC_TEST := $(BUILD_DIR)/test_montecarlo

EXAMPLE_SRC := $(wildcard examples/*.c)
EXAMPLES := $(EXAMPLE_SRC:examples/%.c=$(BUILD_DIR)/%)

UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
  PLATFORM_SRC := src/tinypar_win32.c
  THREAD_FLAGS :=
else ifneq (,$(findstring MINGW,$(UNAME_S)))
  PLATFORM_SRC := src/tinypar_win32.c
  THREAD_FLAGS :=
else
  PLATFORM_SRC := src/tinypar_posix.c
  THREAD_FLAGS := -pthread
endif

SOURCES := src/tinypar.c $(PLATFORM_SRC)
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)

all: $(LIBRARY)

$(BUILD_DIR) $(LIB_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) -c $< -o $@

$(LIBRARY): $(OBJECTS) | $(LIB_DIR)
	$(AR) rcs $@ $^

$(TEST): tests/test_parallel_for.c $(LIBRARY) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) $< $(LIBRARY) -o $@

$(MC_TEST): tests/test_montecarlo.c $(LIBRARY) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) $< $(LIBRARY) -lm -o $@

$(BUILD_DIR)/%: examples/%.c $(LIBRARY) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) $< $(LIBRARY) -lm -o $@

examples: $(EXAMPLES)

test: $(TEST) $(MC_TEST)
	$(TEST)
	$(MC_TEST)

install: $(LIBRARY)
	mkdir -p $(DESTDIR)$(LIBDIR) $(DESTDIR)$(INCLUDEDIR) $(DESTDIR)$(PKGCONFIGDIR)
	install -m 644 $(LIBRARY) $(DESTDIR)$(LIBDIR)/libtinypar.a
	install -m 644 include/tinypar.h $(DESTDIR)$(INCLUDEDIR)/tinypar.h
	sed -e 's|@PREFIX@|$(PREFIX)|g' -e 's|@LIBDIR@|$(LIBDIR)|g' \
		-e 's|@INCLUDEDIR@|$(INCLUDEDIR)|g' tinypar.pc.in > \
		$(DESTDIR)$(PKGCONFIGDIR)/tinypar.pc

clean:
	rm -rf $(BUILD_DIR) $(LIB_DIR)

.PHONY: all test examples install clean
