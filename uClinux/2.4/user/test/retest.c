/* retest PATTERN STRING [flags: e=REG_EXTENDED n=REG_NOSUB i=REG_ICASE]: uClibc regex check
   retest -g PATTERN: glob() check */
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <glob.h>

int main(int argc, char **argv)
{
	regex_t re;
	regmatch_t m[2];
	int fl = 0, r;
	const char *f = argc > 3 ? argv[3] : "";

	if (argc == 3 && !strcmp(argv[1], "-g")) {	/* retest -g PATTERN: glob */
		glob_t g;
		size_t i;
		printf("main at %p\n", (void *) main); fflush(stdout);
		r = glob(argv[2], 0, NULL, &g);
		printf("glob = %d:", r);
		for (i = 0; !r && i < g.gl_pathc; i++)
			printf(" %s", g.gl_pathv[i]);
		printf("\n");
		return r != 0;
	}
	if (argc < 3)
		return 2;
	if (strchr(f, 'e')) fl |= REG_EXTENDED;
	if (strchr(f, 'n')) fl |= REG_NOSUB;
	if (strchr(f, 'i')) fl |= REG_ICASE;
	printf("regcomp(%s, %#x)\n", argv[1], fl); fflush(stdout);
	r = regcomp(&re, argv[1], fl);
	printf(" = %d\n", r); fflush(stdout);
	if (r)
		return 2;
	r = regexec(&re, argv[2], (fl & REG_NOSUB) ? 0 : 2, (fl & REG_NOSUB) ? NULL : m, 0);
	printf("regexec(%s) = %d", argv[2], r);
	if (!r && !(fl & REG_NOSUB))
		printf(" [%d,%d)", (int) m[0].rm_so, (int) m[0].rm_eo);
	printf("\n");
	regfree(&re);
	return r != 0;
}
