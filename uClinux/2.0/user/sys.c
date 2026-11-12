/* user/sys.c - minimal system call stubs for uClinux/ST20 programs (no libc yet) */
#include "sys.h"

typedef long (*st20_syscall_t)(long, long, long, long, long, long);

/* the kernel stores its entry point at 0xc0000000 (asm-st20/unistd.h) */
long syscall3(long nr, long a, long b, long c)
{
	return (*(st20_syscall_t *) 0xc0000000)(nr, a, b, c, 0, 0);
}

int write(int fd, const void *buf, int n) { return syscall3(4, fd, (long) buf, n); }
int read(int fd, void *buf, int n) { return syscall3(3, fd, (long) buf, n); }
int getpid(void) { return syscall3(20, 0, 0, 0); }
int pause(void) { return syscall3(29, 0, 0, 0); }
long time(long *t) { return syscall3(13, (long) t, 0, 0); }
void _exit(int code) { syscall3(1, code, 0, 0); for (;;) ; }

int strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
void puts_(const char *s) { write(1, s, strlen(s)); }

/* decimal without libc */
void putnum(long v)
{
	char b[12];
	int i = sizeof(b);
	int neg = v < 0;

	if (neg)
		v = -v;
	b[--i] = 0;
	do {
		b[--i] = '0' + v % 10;
		v /= 10;
	} while (v);
	if (neg)
		b[--i] = '-';
	puts_(b + i);
}
