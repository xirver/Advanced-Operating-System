/*
 * Simple implementation of cprintf console output for the kernel, based on
 * printfmt() and the kernel console's cputchar().
 */

#include <types.h>
#include <cpu.h>
#include <spinlock.h>
#include <stdio.h>
#include <stdarg.h>

extern struct spinlock console_lock;

static void putch(int ch, int *cnt)
{
	cputchar(ch);
	*cnt++;
}

int vcprintf(const char *fmt, va_list ap)
{
	int cnt = 0;

#ifndef USE_BIG_KERNEL_LOCK
	int already_locked = 0;
	if (console_lock.cpu != this_cpu) {
		spin_lock(&console_lock);
	} else {
		already_locked = 1;
	}
#endif

	vprintfmt((void*)putch, &cnt, fmt, ap);

#ifndef USE_BIG_KERNEL_LOCK
	if (!already_locked)
		spin_unlock(&console_lock);
#endif

	return cnt;
}

int cprintf(const char *fmt, ...)
{
	va_list ap;
	int cnt;

	va_start(ap, fmt);

	cnt = vcprintf(fmt, ap);

	va_end(ap);

	return cnt;
}

