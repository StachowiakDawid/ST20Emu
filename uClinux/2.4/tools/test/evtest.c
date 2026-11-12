/*
 * evtest [DEVICE [COUNT]] - prints input events (default /dev/input/event0, 20 events)
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

struct input_event {
	long sec, usec;
	unsigned short type, code;
	long value;
};
#define EVIOCGNAME(len) (0x80004506 | ((len) << 16))	/* _IOC(_IOC_READ, 'E', 0x06, len) */

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/input/event0";
	int n = argc > 2 ? atoi(argv[2]) : 20, fd;
	char name[64] = "?";
	struct input_event ev;

	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		perror(dev);
		return 1;
	}
	ioctl(fd, EVIOCGNAME(sizeof(name)), name);
	printf("%s: %s\n", dev, name);
	fflush(stdout);
	while (n-- > 0 && read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
		printf("event %ld.%06ld type %d code %d value %ld\n", ev.sec, ev.usec, ev.type,
		       ev.code, ev.value);
		fflush(stdout);
	}
	return 0;
}
