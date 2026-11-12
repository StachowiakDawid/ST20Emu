#define ASC0 0x20003000
#define REG(o) (*(volatile unsigned *)(ASC0 + (o)))
static const char msg[] = "Hello from gcc-t450 on ST20\n";
int sum(int a, int b) { return a * b + 7; }
static void putc_(int c) { REG(4) = c; }
void start(void)
{
    const char *p;
    REG(0xc) = 0x80 | 0x9;          /* run, 8N1 */
    for (p = msg; *p; p++) putc_(*p);
    putc_('0' + sum(2, 3) % 10);   /* 2*3+7 = 13 -> '3' */
    putc_('\n');
    for (;;) ;
}
