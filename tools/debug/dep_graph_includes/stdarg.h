#ifndef _FAKE_STDARG_H
#define _FAKE_STDARG_H
typedef void *va_list;
#define va_start(ap, last) ((void)0)
#define va_end(ap) ((void)0)
#define va_arg(ap, t) (*(t *)0)
#endif
