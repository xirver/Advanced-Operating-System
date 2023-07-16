#pragma once

#include <stdarg.h>

#ifndef NULL
#define NULL ((void *) 0)
#endif /* !NULL */

#define GLOBAL_DEBUG 1

/* lib/stdio.c */
void cputchar(int c);
void putchar(int c);
int getchar(void);
int iscons(int fd);

/* lib/printfmt.c */
void printfmt(void (*putch)(int, void*), void *putdat, const char *fmt, ...);
void vprintfmt(void (*putch)(int, void*), void *putdat, const char *fmt,
	va_list);
int snprintf(char *str, int size, const char *fmt, ...);
int vsnprintf(char *str, int size, const char *fmt, va_list);

/* lib/printf.c */
int cprintf(const char *fmt, ...);
int vcprintf(const char *fmt, va_list);
//#define debug_print(s) __debug_print(s, __FILE__, __LINE__)
#define debug_print(fmt, ...) \
        do { if (GLOBAL_DEBUG && DEBUG) cprintf("[%-20s():%4d]: " fmt, __func__, \
                                __LINE__, __VA_ARGS__); } while (0)
#define debug_print_basic(fmt, ...) \
        do { if (GLOBAL_DEBUG && DEBUG) cprintf("[%s():%d]: " fmt, __func__, \
                                __LINE__); } while (0)

/* lib/fprintf.c */
int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list);
int fprintf(int fd, const char *fmt, ...);
int vfprintf(int fd, const char *fmt, va_list);

/* lib/readline.c */
char *readline(const char *prompt);



