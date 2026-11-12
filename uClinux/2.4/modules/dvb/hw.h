/*
 * hw.h - DSR9500 (STi5512) DVB hardware: device accesses, register addresses
 *
 * The PTI's registers and shared memories must be accessed with device instructions
 * (datasheet 18.x, "must be accessed using device access instructions"); the original
 * drivers use them for every peripheral (#pragma ST_device), so this driver does too.
 */
#ifndef DVB_HW_H
#define DVB_HW_H

static inline u32 dev_rd(unsigned long a)
{
	u32 v;
	__asm__ __volatile__ ("devlw" : "=a" (v) : "aP" (a) : "memory");
	return v;
}

static inline void dev_wr(unsigned long a, u32 v)
{
	__asm__ __volatile__ ("devsw" : : "bP" (v), "aP" (a) : "memory");
}

static inline u8 dev_rdb(unsigned long a)
{
	u32 v;
	__asm__ __volatile__ ("devlb" : "=a" (v) : "aP" (a) : "memory");
	return v;
}

static inline void dev_wrb(unsigned long a, u32 v)
{
	__asm__ __volatile__ ("devsb" : : "bP" (v), "aP" (a) : "memory");
}

/* SSC1 = tuner I2C bus ("back"), PIO3 bit 0 SDA, bit 2 SCL */
#define SSC1		0x2000a000UL
#define SSC_BRG		0x00
#define SSC_TBUF	0x04
#define SSC_RBUF	0x08
#define SSC_CON		0x0c
#define SSC_IEN		0x10
#define SSC_STAT	0x14
#define SSC_I2C		0x18
#define SSC_STAT_RIR	0x001
#define SSC_STAT_BUSY	0x200
#define PIO3		0x2000f000UL
#define PIO_SET_C0	0x24
#define PIO_SET_C1	0x34
#define PIO_SET_C2	0x44
#define PIO_SET_OUT	0x04

/* EMI output latch with the LNB and demodulator control lines (main app a_7fe9c528) */
#define LATCH_LNB	0x50400000UL
#define LNB_POWER	0x80		/* LNB supply on */
#define LNB_18V		0x20		/* 18 V (horizontal / right), clear = 13 V */
#define LNB_TONE_OFF	0x08		/* 22 kHz tone, active low */
#define LNB_DEMOD_RUN	0x04		/* demodulator reset, active low */

/* PTI (registers 0x20030000, data memory 0x20038000, instruction memory 0x2003c000) */
#define PTI		0x20030000UL
#define PTI_REG(n)	(PTI + 4 * (n))		/* as the PTI 3.4.1 driver's pti_regs[n] */
#define PTI_DATA	0x20038000UL
#define PTI_IRAM	0x2003c000UL
#define PTI_PIDS	0x20038000UL		/* 48 x 16-bit PIDs, 0xe000 = none */
#define PTI_SLOT(s, w)	(0x20038060UL + 16 * (s) + 4 * (w))
#define PTI_DMADESC(d)	(0x20038360UL + 20 * (d))	/* base, top, read, write, qwrite */
#define PTI_PCR_PID	0x20038a40UL
#define PTI_PCR_VAL	0x20038a64UL
#define PTI_FILT_EN	0x20038a6cUL
#define PTI_ALTOUT	0x20038a7cUL
#define PTI_DMACUR	0x20038a80UL
#define PTI_TC_CTRL	0x20027000UL
#define PTI_IRQ		14
#define PTI_SLOTS	48
#define PTI_PCR_SLOT	47
#define PTI_NO_PID	0xe000

#endif
