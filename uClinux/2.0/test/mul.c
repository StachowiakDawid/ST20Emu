#define ASC(o) (*(volatile unsigned *)(0x20003000 + (o)))
#define CFG (*(volatile unsigned long *) 0x20000600UL)
unsigned long clk;
static void putc_(int c) { ASC(4) = c; }
static void puthex(unsigned long v) { int i; for (i = 28; i >= 0; i -= 4) putc_("0123456789abcdef"[(v >> i) & 15]); putc_('\n'); }
unsigned long f(unsigned long reload) { return 16 * reload * 115200; }
void start(void)
{
	unsigned long reload;
	ASC(0xc) = 0x89;
	CFG = 0;
	reload = (CFG & 0x3f) == 1 ? 0x20 : 0x1b;
	clk = 16 * reload * 115200;
	puthex(clk); puthex(f(27)); puthex(16UL * 27 * 115200);
	for (;;) ;
}
