#
# Author: Hans Liljestrand <hans@liljestrand.dev>
#
# Distributed under Apache License v2.0 with LLVM Exceptions.

SOURCE_FILES = $(wildcard *.c)
LLVM_FILES = $(SOURCE_FILES:.c=.ll)

CC = ${HOME}/opt/llvm/llvmorg-9.0.1/bin/clang
CFLAGS = -Wall -Xclang -disable-lifetime-markers -fno-unroll-loops -O2

LL_FILES = $(SOURCE_FILES:.c=.ll)
TEST_FILES = $(SOURCE_FILES:.ll=.test)
OUT_FILES = $(SOURCE_FILES:.c=.out)
OBJ_FILES = $(SOURCE_FILES:.c=.o)

all: $(LL_FILES) $(OBJ_FILES)

$(LL_FILES): %.ll : %.c
	echo "; clang $(CFLAGS)" > $@
	echo >> $@
	sed 's/^/; /g' $^ >> $@
	$(CC) $(CFLAGS) -emit-llvm -c -S $^ -o- >> $@

$(OUT_FILES): %.out : %.c
	$(CC) $(CFLAGS) $^ -o $@

$(OBJ_FILES): %.o : %.c
	$(CC) $(CFLAGS) -c $^ -o $@

.PHONY: clean
clean:
	rm -rf $(LL_FILES) $(OUT_FILES)


# vim:ft=make
