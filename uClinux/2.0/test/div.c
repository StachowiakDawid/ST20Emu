#define ASC(o) (*(volatile unsigned *)(0x20003000 + (o)))
static void putc_(int c) { ASC(4) = c; }
static void puthex(unsigned long v) { int i; for (i = 28; i >= 0; i -= 4) putc_("0123456789abcdef"[(v >> i) & 15]); putc_(' '); }
volatile unsigned long n = 49766400, b = 10;
volatile unsigned long big = 0x90000000UL;
void start(void)
{
	unsigned long x = n, y = b;
	ASC(0xc) = 0x89;
	puthex(x / y); puthex(x % y); puthex(x / 10); puthex(x % 10);
	puthex(big / y); puthex(big % y); putc_('\n');
	for (;;) ;
}
