/* stattest PATH: raw struct stat bytes from the kernel, and the fields as libc sees them */
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

static void hex(unsigned long v, int n)
{
	char b[9];
	int i;

	for (i = n - 1; i >= 0; i--, v >>= 4)
		b[i] = "0123456789abcdef"[v & 15];
	b[n] = ' ';
	write(1, b, n + 1);
}

static void field(const char *name, unsigned long v)
{
	write(1, name, strlen(name));
	hex(v, 8);
}

int main(int argc, char **argv)
{
	struct stat st;
	unsigned char *p = (unsigned char *) &st;
	int i, r;

	memset(&st, 0xee, sizeof(st));
	r = stat(argc > 1 ? argv[1] : "/", &st);
	field("ret", r);
	field("size", sizeof(st));
	write(1, "\n", 1);
	for (i = 0; i < (int) sizeof(st); i++)
		hex(p[i], 2);
	write(1, "\n", 1);
	field("mode", st.st_mode);
	field("nlink", st.st_nlink);
	field("uid", st.st_uid);
	field("size", st.st_size);
	field("off_mode", (char *) &st.st_mode - (char *) &st);
	field("off_size", (char *) &st.st_size - (char *) &st);
	write(1, "\n", 1);
	return 0;
}
