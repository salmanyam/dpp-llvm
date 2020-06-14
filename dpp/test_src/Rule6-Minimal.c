/*
 * Author: Hans Liljestrand <hans@liljestrand.dev>
 * Copyright (C) 2020 Hans Liljestrand <hans@liljestrand.dev>
 *
 * Distributed under terms of the MIT license.
 */

#include <stdio.h>

typedef struct mystruct_s {
    char buffer[64];
    int (*fp)(const char*, ...);
} mystruct_t;

typedef struct mystruct_safer_s {
  int (*fp)(const char*, ...);
  char buffer[64];
} mystruct_safer_t;

mystruct_t g_ms = {"Hello World\n", &printf};
mystruct_safer_t g_ms_safer = {&printf, "Hello World\n"};

__attribute__((noinline))
void func(mystruct_t *ms) {
    scanf("%s", ms->buffer);
    ms->fp(ms->buffer);
}

__attribute__((noinline))
void func_safer(mystruct_safer_t *ms) {
  scanf("%s", ms->buffer);
  ms->fp(ms->buffer);
}

int main(void) {
    mystruct_t ms = {"Hello World\n", &printf};
    mystruct_safer_t ms_safer = {&printf, "Hello World\n"};
    func(&ms);
    func_safer(&ms_safer);
    func(&g_ms);
    func_safer(&g_ms_safer);
    return 0;
}
