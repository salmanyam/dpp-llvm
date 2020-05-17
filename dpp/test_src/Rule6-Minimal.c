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

void func(mystruct_t *ms) {
    gets(ms->buffer);
    ms->fp(ms->buffer);
}

int main(void) {
    mystruct_t ms = {"Hello World\n", &printf};
    func(&ms);
    return 0;
}
