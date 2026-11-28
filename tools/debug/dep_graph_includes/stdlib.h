#ifndef _FAKE_STDLIB_H
#define _FAKE_STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void free(void *p);
int abs(int x);
#endif
