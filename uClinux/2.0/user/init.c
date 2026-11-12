/* user/init.c - first user-space program of uClinux/ST20 (step 5: no libc, no shell) */
#include "sys.h"

static const char *greeting[] = {	/* pointers in data: exercises bFLT relocations */
	"Hello from user space on the ST20!\n",
	"This is /sbin/init, a flat binary loaded from romfs.\n",
	0
};

int main(int argc, char **argv)
{
	int i;
	char c;

	for (i = 0; greeting[i]; i++)
		puts_(greeting[i]);
	puts_("argv[0] = ");
	puts_(argv[0]);
	puts_(", pid = ");
	putnum(getpid());
	if (argc > 1) {		/* run from the shell with an argument: say it and exit */
		puts_(", argv[1] = ");
		puts_(argv[1]);
		puts_("\n");
		return 7;
	}
	puts_("\nType characters, they are echoed back:\n");
	for (;;) {
		if (read(0, &c, 1) == 1) {
			puts_("got '");
			write(1, &c, 1);
			puts_("'\n");
		}
	}
	return 0;
}
