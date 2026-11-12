/* user/sash-ps-stub.c - sash's ps (ps.c) uses floating point, which uClinux/ST20 lacks */
#include <stdio.h>

void do_ps(int argc, char **argv)
{
	printf("ps: not available on uClinux/ST20 yet (see /proc)\n");
}
