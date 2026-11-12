/*
 * modtool: insmod / rmmod / lsmod for uClinux/ST20 .mod images (arch/st20/kernel/module.c)
 *   insmod FILE.mod     rmmod NAME     lsmod
 * (also as "modtool insmod ..."; the romfs has insmod/rmmod/lsmod links to it)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

typedef long (*vec_t)(long, long, long, long, long, long);
#define SYSCALL(nr, a, b) ((*(vec_t *) 0xc0000000)((nr), (long) (a), (long) (b), 0, 0, 0))

static int sys(long r, const char *what)
{
	if (r < 0 && r > -4096) {
		errno = -r;
		perror(what);
		return 1;
	}
	return 0;
}

static int insmod(const char *path)
{
	struct stat st;
	char *buf;
	int fd = open(path, O_RDONLY), n, got = 0;

	if (fd < 0 || fstat(fd, &st) < 0) {
		perror(path);
		return 1;
	}
	buf = malloc(st.st_size);
	if (!buf) {
		perror("malloc");
		return 1;
	}
	while (got < st.st_size && (n = read(fd, buf + got, st.st_size - got)) > 0)
		got += n;
	close(fd);
	if (got != st.st_size) {
		fprintf(stderr, "%s: short read\n", path);
		return 1;
	}
	return sys(SYSCALL(128, buf, got), path);
}

static int lsmod(void)
{
	char line[128];
	FILE *f = fopen("/proc/modules", "r");

	if (!f) {
		perror("/proc/modules");
		return 1;
	}
	printf("Module                   Size  Address\n");
	while (fgets(line, sizeof(line), f))
		fputs(line, stdout);
	fclose(f);
	return 0;
}

int main(int argc, char **argv)
{
	const char *cmd = strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0];

	if (!strcmp(cmd, "modtool") && argc > 1) {
		argv++;
		argc--;
		cmd = argv[0];
	}
	if (!strcmp(cmd, "insmod") && argc == 2)
		return insmod(argv[1]);
	if (!strcmp(cmd, "rmmod") && argc == 2)
		return sys(SYSCALL(129, argv[1], 0), argv[1]);
	if (!strcmp(cmd, "lsmod"))
		return lsmod();
	fprintf(stderr, "usage: insmod FILE.mod | rmmod NAME | lsmod\n");
	return 2;
}
