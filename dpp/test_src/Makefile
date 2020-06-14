#
# Makefile
# Hans Liljestrand, 2020-05-14 16:48
#


SOURCE_FILES = $(wildcard *.c)
LLVM_FILES = $(SOURCE_FILES:.c=.ll)

CC = ${HOME}/opt/llvm/llvmorg-9.0.1/bin/clang
CFLAGS = -Xclang -disable-lifetime-markers -fno-unroll-loops -O2

LL_FILES = $(SOURCE_FILES:.c=.ll)
TEST_FILES = $(SOURCE_FILES:.ll=.test)
OUT_FILES = $(SOURCE_FILES:.c=.out)

all: $(LL_FILES) $(OUT_FILES)

$(LL_FILES): %.ll : %.c
	echo "; clang $(CFLAGS)" > $@
	echo >> $@
	sed 's/^/; /g' $^ >> $@
	$(CC) $(CFLAGS) -emit-llvm -c -S $^ -o- >> $@

$(OUT_FILES): %.out : %.c
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean
clean:
	rm -rf $(LL_FILES) $(OUT_FILES)


# vim:ft=make
