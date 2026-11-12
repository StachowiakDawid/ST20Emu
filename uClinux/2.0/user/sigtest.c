/* user/sigtest.c - signal and error-trap checks for uClinux/ST20
 *   sigtest raise   handler via kill(getpid(), SIGUSR1), then returns normally
 *   sigtest loop    busy loop (no system calls) until SIGINT (Ctrl-C on the terminal)
 *   sigtest ill     illegal instruction -> killed by SIGILL
 *   sigtest div     division by zero -> killed by SIGFPE
 */
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static volatile int got;

static void handler(int sig)
{
	got = sig;
	printf("handler: got signal %d\n", sig);
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "raise";

	if (!strcmp(mode, "raise")) {
		int i, sum = 0;
		signal(SIGUSR1, handler);
		for (i = 0; i < 10; i++)
			sum += i;
		kill(getpid(), SIGUSR1);
		printf("back in main: got=%d, sum=%d (expect 45)\n", got, sum);
	} else if (!strcmp(mode, "loop")) {
		volatile unsigned long n = 0;
		signal(SIGINT, handler);
		printf("looping, press Ctrl-C\n");
		while (!got)
			n++;
		printf("loop ended after %lu iterations\n", n);
	} else if (!strcmp(mode, "ill")) {
		printf("executing an illegal instruction\n");
		__asm__ __volatile__ (".byte 0x2b, 0xf1");	/* opr 0xb1: no such instruction */
		printf("still alive?!\n");
	} else if (!strcmp(mode, "div")) {
		volatile int a = 7, z = 0;
		printf("dividing by zero\n");
		printf("7/0 = %d?!\n", a / z);
	}
	return 0;
}
