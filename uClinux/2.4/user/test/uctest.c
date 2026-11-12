/* uctest: uClibc smoke test for uClinux/ST20 (stdio, printf floats, malloc, setjmp,
   signals, vfork + exec + wait, pipes, dirs, time, strtod, qsort, regex, alloca) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <regex.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <math.h>
#include <alloca.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static jmp_buf jb;
static sigjmp_buf sjb;
static volatile int got;

static void deep(int n) { if (n == 0) longjmp(jb, 42); deep(n - 1); }
static void onsig(int s) { got = s; }
static void onalrm(int s) { siglongjmp(sjb, s); }
/* alloca with calls while the block is live (gcc-t800 used to save Wptr in its first word) */
static int al(int n)
{
	char *p = alloca(n), *q;
	int i, s = 0;
	for (i = 0; i < n; i++) p[i] = i;
	write(1, "", 0);
	q = alloca(16); strcpy(q, "alloca");
	sprintf(q + 6, "%d", n);
	for (i = 0; i < n; i++) s += p[i];
	return strcmp(q, "alloca100") == 0 ? s : -1;
}
static int cmp(const void *a, const void *b) { return *(const int *) a - *(const int *) b; }

int main(int argc, char **argv)
{
	char buf[256];
	int i, st, p[2];
	pid_t pid;

	printf("uctest: argc=%d argv[0]=%s\n", argc, argv[0]);
	/* uctest vforkfail: a failing vfork must return -1 and set errno (here EAGAIN:
	   RLIMIT_NPROC 0 for a non-root uid) */
	if (argc > 1 && !strcmp(argv[1], "vforkfail")) {
		struct rlimit rl = { 0, 0 };
		if (setuid(1000) < 0 || setrlimit(RLIMIT_NPROC, &rl) < 0) {
			printf("vforkfail: setup failed\n");
			return 2;
		}
		errno = 0;
		pid = vfork();
		if (pid == 0)
			_exit(0);
		printf("vforkfail: vfork=%d errno=%d %s\n", (int) pid, errno,
		       pid == -1 && errno == EAGAIN ? "ok" : "BAD");
		return pid == -1 && errno == EAGAIN ? 0 : 1;
	}
	/* printf */
	sprintf(buf, "%d %u %x %o %5s|%-5s|%c %ld %lld", -12, 34u, 0xbeef, 8, "ab", "cd", 'z',
		-123456789L, -1234567890123LL);
	printf("%s\n", buf);
	CHECK(!strcmp(buf, "-12 34 beef 10    ab|cd   |z -123456789 -1234567890123"));
	sprintf(buf, "%f %.3f %e %g %g %g", 3.5, -2.0 / 3, 12345.678, 0.0001, 1e20, 100.0);
	printf("%s\n", buf);
	CHECK(!strcmp(buf, "3.500000 -0.667 1.234568e+04 0.0001 1e+20 100"));
	CHECK(strtod("2.5e3", NULL) == 2500.0);
	CHECK(atoi("-77") == -77 && strtol("0x7f", NULL, 0) == 127);
	sprintf(buf, "%.4f %.4f %.4f %.4f", sqrt(2.0), sin(1.0), exp(1.0), atan2(1.0, 1.0));
	printf("math: %s\n", buf);
	CHECK(!strcmp(buf, "1.4142 0.8415 2.7183 0.7854"));
	sprintf(buf, "%.3f %.3f %.3f", pow(2.0, 10.0), log(10.0), fmod(7.5, 2.0));
	printf("math: %s\n", buf);
	CHECK(!strcmp(buf, "1024.000 2.303 1.500"));
	/* malloc */
	{
		char *a[64];
		for (i = 0; i < 64; i++) { a[i] = malloc(100 + i * 37); CHECK(a[i]); memset(a[i], i, 100 + i * 37); }
		for (i = 0; i < 64; i += 2) free(a[i]);
		for (i = 1; i < 64; i += 2) { CHECK(a[i][99] == i); free(a[i]); }
		a[0] = realloc(NULL, 10); strcpy(a[0], "123456789"); a[0] = realloc(a[0], 5000);
		CHECK(!strcmp(a[0], "123456789")); free(a[0]);
	}
	/* qsort */
	{
		int v[8] = { 5, 3, 9, 1, 7, 2, 8, 0 };
		qsort(v, 8, sizeof(int), cmp);
		for (i = 0; i < 7; i++) CHECK(v[i] <= v[i + 1]);
	}
	/* setjmp */
	if ((i = setjmp(jb)) == 0) deep(10);
	CHECK(i == 42);
	/* signals */
	signal(SIGUSR1, onsig);
	kill(getpid(), SIGUSR1);
	CHECK(got == SIGUSR1);
	signal(SIGALRM, onalrm);
	if ((i = sigsetjmp(sjb, 1)) == 0) { alarm(1); for (;;) ; }
	CHECK(i == SIGALRM);
	{
		sigset_t s;
		sigprocmask(SIG_BLOCK, NULL, &s);
		CHECK(!sigismember(&s, SIGALRM));	/* mask restored by siglongjmp */
	}
	/* vfork + exec + wait */
	pid = vfork();
	if (pid == 0) { execl("/bin/echo", "echo", "child", "echo", "ok", NULL); _exit(99); }
	CHECK(pid > 0);
	CHECK(waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0);
	pid = vfork();
	if (pid == 0) _exit(7);
	CHECK(waitpid(pid, &st, 0) == pid && WEXITSTATUS(st) == 7);
	/* pipe */
	CHECK(pipe(p) == 0);
	pid = vfork();
	if (pid == 0) { dup2(p[1], 1); close(p[0]); execl("/bin/echo", "echo", "piped", NULL); _exit(99); }
	close(p[1]);
	i = read(p[0], buf, sizeof buf - 1); buf[i > 0 ? i : 0] = 0;
	close(p[0]); waitpid(pid, &st, 0);
	CHECK(!strcmp(buf, "piped\n"));
	/* files */
	{
		FILE *f = fopen("/tmp/uctest.txt", "w");
		struct stat sb;
		DIR *d; struct dirent *de; int found = 0;
		CHECK(f != NULL);
		if (f) { fprintf(f, "line1\nline2 %d\n", 2); fclose(f); }
		f = fopen("/tmp/uctest.txt", "r");
		CHECK(f && fgets(buf, sizeof buf, f) && !strcmp(buf, "line1\n"));
		CHECK(f && fscanf(f, "line2 %d", &i) == 1 && i == 2);
		if (f) fclose(f);
		CHECK(stat("/tmp/uctest.txt", &sb) == 0 && sb.st_size == 14 && S_ISREG(sb.st_mode));
		d = opendir("/tmp");
		while (d && (de = readdir(d))) if (!strcmp(de->d_name, "uctest.txt")) found = 1;
		if (d) closedir(d);
		CHECK(found);
		CHECK(unlink("/tmp/uctest.txt") == 0);
		CHECK(open("/nonexistent", O_RDONLY) < 0 && errno == ENOENT);
		printf("strerror: %s\n", strerror(ENOENT));
	}
	/* regex */
	{
		regex_t re; regmatch_t m[3];
		CHECK(regcomp(&re, "([a-z]+)=([0-9]+)", REG_EXTENDED) == 0);
		CHECK(regexec(&re, "xx abc=123 yy", 3, m, 0) == 0 && m[1].rm_so == 3 && m[2].rm_eo == 10);
		regfree(&re);
	}
	/* time, uname */
	{
		struct utsname u; struct timeval tv; time_t t = 86400 * 365;
		CHECK(uname(&u) == 0);
		printf("uname: %s %s %s\n", u.sysname, u.release, u.machine);
		CHECK(gettimeofday(&tv, NULL) == 0);
		strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", gmtime(&t));
		CHECK(!strcmp(buf, "1971-01-01 00:00:00"));
	}
	/* __builtin_alloca with calls (incl. inlined syscalls) while blocks are live */
	CHECK(al(100) == 4950);
	printf("uctest: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
	return fails != 0;
}
