# SPDX-License-Identifier: GPL-3.0-or-later
CC ?= cc
CFLAGS ?= -O2
BASE = -std=c11 -Wall -Wextra -Werror -I Libraries/include
SRC = native/filesystems/f2fs/f2fs.c
.PHONY: all check clean
all: build/F2FS.so build/freestanding/F2FS.so
build/F2FS.so: $(SRC) Makefile
	mkdir -p build
	$(CC) $(BASE) $(CFLAGS) -fPIC -fvisibility=hidden -shared -Wl,-z,defs -o $@ $(SRC)
build/freestanding/F2FS.so: $(SRC) Makefile
	mkdir -p build/freestanding
	$(CC) $(BASE) -O2 -march=x86-64 -DIFS_FREESTANDING -ffreestanding -fno-builtin -fno-stack-protector -fno-tree-loop-distribute-patterns -fPIC -fvisibility=hidden -shared -nostdlib -Wl,-z,defs,-Bsymbolic -o $@ $(SRC)
build/test_f2fs: tests/test_f2fs.c
	mkdir -p build
	$(CC) $(BASE) $(CFLAGS) -o $@ $< -ldl
check: all build/test_f2fs
	test "$$(nm -D --defined-only build/F2FS.so | awk '{print $$3}')" = infiltrator_filesystem_api_v2
	test "$$(nm -D --defined-only build/freestanding/F2FS.so | awk '{print $$3}')" = infiltrator_filesystem_api_v2
	test -z "$$(nm -D --undefined-only build/freestanding/F2FS.so)"
	! readelf -d build/freestanding/F2FS.so | grep -q NEEDED
	build/test_f2fs ./build/F2FS.so
	build/test_f2fs ./build/freestanding/F2FS.so
clean:
	rm -rf build
