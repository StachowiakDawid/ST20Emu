#define ASC(o) (*(volatile unsigned *)(0x20003000 + (o)))
static void putc_(int c) { ASC(4) = c; }
#define do_div(n,base) ({ int __res; __res = ((unsigned long) n) % (unsigned) base; n = ((unsigned long) n) / (unsigned) base; __res; })
static char *digits = "0123456789abcdef";
void num(long n, int base) { char tmp[40]; int i = 0; if (n == 0) tmp[i++] = '0'; else while (n != 0) tmp[i++] = digits[do_div(n, base)]; while (i-- > 0) putc_(tmp[i]); putc_(' '); }
volatile long vals[] = { 7, 1234, 49766400, 49766407, 123456789, 2000000000 };
void start(void)
{
	int i;
	ASC(0xc) = 0x89;
	for (i = 0; i < 6; i++) num(vals[i], 10);
	putc_('\n');
	for (;;) ;
}
