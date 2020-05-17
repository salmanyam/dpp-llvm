#
# Makefile
# Hans Liljestrand, 2020-05-14 16:48
#


SOURCE_FILES = $(wildcard *.c)
LLVM_FILES = $(SOURCE_FILES:.c=.ll)

CC = ${HOME}/opt/llvm/llvmorg-9.0.1/bin/clang
CFLAGS = -Xclang -disable-lifetime-markers -fno-unroll-loops -O2 -emit-llvm -c -S

LL_FILES = $(SOURCE_FILES:.c=.ll)
TEST_FILES = $(SOURCE_FILES:.ll=.test)

all: $(LL_FILES)

$(LL_FILES): %.ll : %.c
	echo "; clang $(CFLAGS)" > $@
	echo >> $@
	sed 's/^/; /g' $^ >> $@
	$(CC) $(CFLAGS) $^ -o- >> $@


.PHONY: clean
clean:
	rm -rf $(LL_FILES)


# vim:ft=make
