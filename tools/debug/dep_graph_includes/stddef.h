#ifndef _FAKE_STDDEF_H
#define _FAKE_STDDEF_H
#ifndef NULL
#define NULL ((void *)0)
#endif
#define offsetof(t, m) ((size_t)&(((t *)0)->m))
typedef unsigned int size_t;
typedef int ptrdiff_t;
typedef unsigned short wchar_t;
#endif
