/*
 * user/boot/bootinit.c - DSR9500 clock generator and SDRAM controller set-up, run from
 * flash by the ROM loader (tools/mkrom-dsr9500.py) before the kernel is copied to SDRAM.
 *
 * Samsung's dsr_ckg_init / dsr_sdram_init, called by the original romload right after its
 * poke loop (Decompilation/DSR9500 src/boot_hw.c, src/rl_romload.c).  Register names:
 * STi5512 Register Manual 7145562 B, chapters 8 and 9; byte registers at address 0.
 * Workspace: on-chip SRAM (the loader sets Wptr); no data, no globals.
 */
#define REG8(a)		(*(volatile unsigned char *) (a))
#define CFG_MCF		0x00	/* SDRAM refresh interval, 32-clock units */
#define CFG_CCF		0x01	/* chip configuration */
#define CKG_PLL		0x30
#define CKG_CFG		0x31
#define CFG_DRC		0x38	/* DRAM configuration */
#define CKG_PCM1	0xd0
#define CKG_PCM2	0xd1
#define CKG_PCM3	0xd2
#define CKG_PCM4	0xd3
#define CKG_MCK1	0xd8
#define CKG_MCK2	0xd9
#define CKG_MCK3	0xda
#define CKG_MCK4	0xdb
#define CKG_AUX1	0xdc
#define CKG_AUX2	0xdd
#define CKG_AUX3	0xde
#define CKG_AUX4	0xdf

static void spin(int n)
{
	volatile int i;

	for (i = 0; i < n; i++)
		;
}

static void ckg_init(void)
{
	REG8(CKG_PLL) = 0x80;		/* PDM: PLL power down */
	spin(1000);
	REG8(CKG_CFG) = 0x20;		/* PCMCLK pin is an output */
	REG8(CKG_PLL) = 0x59;		/* 27 MHz PIXCLK, /2, x16 */
	spin(100);
	REG8(CKG_MCK1) = 0x00;
	REG8(CKG_MCK2) = 0x00;
	REG8(CKG_MCK3) = 0x0f;
	REG8(CKG_MCK4) = 0xfe;
	REG8(CKG_AUX1) = 0x23;
	REG8(CKG_AUX2) = 0x00;
	REG8(CKG_AUX3) = 0x0f;
	REG8(CKG_AUX4) = 0xfe;
	REG8(CKG_PCM1) = 0x32;
	REG8(CKG_PCM2) = 0x00;
	REG8(CKG_PCM3) = 0x0f;
	REG8(CKG_PCM4) = 0xfe;
	REG8(CFG_CCF) = 0xff;		/* enable interfaces and clocks */
	spin(1000);
}

static void sdram_init(void)
{
	static const unsigned char drc[4] = { 0xa3, 0xa7, 0xab, 0xaf };
	volatile unsigned long *p = (volatile unsigned long *) 0xc0000000;
	int phase;

	REG8(CFG_MCF) = 0x00;
	REG8(CFG_DRC) = 0x00;
	REG8(CFG_DRC) = 0x01;		/* SDR */
	REG8(CFG_DRC) = 0x21;		/* MRS | SDR */
	REG8(CFG_DRC) = 0xa3;		/* MY64 | MRS | ERQ | SDR, P=0 */
	REG8(CFG_MCF) = 0x34;		/* refresh interval 52 */
	spin(1000);
	/* step CFG_DRC.P[1:0] until the SDRAM reads back correctly */
	for (phase = 0; phase < 4; phase++) {
		REG8(CFG_DRC) = drc[phase];
		*p = 0x12345678;
		if (*p == 0x12345678)
			break;
	}
}

void bootinit(void)
{
	ckg_init();
	sdram_init();
}
