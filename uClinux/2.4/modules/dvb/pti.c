/*
 * pti.c - STi5512 PTI (transport demultiplexer) programmed like ST's PTI 3.4.1 driver
 * (Decompilation/DSR9500 m_6be4/m_713c/m_7d38/m_81ac/m_8df4: the transport controller's
 * data structures are defined by its microcode, so they are used as that driver does).
 *
 * Streams used here:
 *   slot 0   video ES/PES (stream type 3) into an 8 KB SDRAM buffer, PTI DMA channel 1
 *            moves it to the video decoder's compressed-data FIFO (as the DSR9500 main
 *            application, PTI_Init: pti_set_buffer(0, video, 8 KB))
 *   slot 1   audio (type 4), 8 KB buffer, DMA channel 2
 *   slots 3-46  raw transport packets (type 6) of one PID each into a circular buffer;
 *            dmx.c filters sections / PES / the TS tap in software
 *   slot 47  PCR (0x20038a40, value at 0x20038a64)
 * The raw buffers are polled every tick and on the PTI interrupt (14).
 */

/* pti_tc_code[729] and DVB_HAVE_PTI_TC, or nothing: written by tools/mkptitc.py (mkimage.sh) */
#include "../../build/mod/pti_tc.h"

#define PTI_RAW_PKTS	48		/* packets per raw buffer (9 KB) */
#define PTI_RAW_SIZE	(PTI_RAW_PKTS * 188)
#define PTI_AV_SIZE	0x2000
/* DMA descriptors and PTI_DMAnTop hold the last 16-byte unit: top = base + size - 16
   (PTI 3.4.1: (int *) (buf + size) - 4) */

struct pti_raw {
	int pid;			/* -1 = free */
	int dma;			/* DMA descriptor index */
	u8 *buf, *mem;			/* 16-byte aligned circular buffer, kmalloc block */
	u32 rp;				/* read pointer (absolute) */
	unsigned long packets, overflows;
	void (*feed)(int pid, const u8 *pkt);
};

static struct pti_raw pti_raw[PTI_SLOTS];
static u32 pti_dma_free = 0xffffffff;	/* DMA descriptors 0-31 */
static u32 pti_enable[3];		/* shadow of IntEnable1-3 (slot interrupts) */
static u32 pti_int0;			/* shadow of IntEnable0 */
static int pti_tc_loaded, pti_running;
static unsigned long pti_irqs, pti_polls;
static struct timer_list pti_timer;
static struct tasklet_struct pti_tasklet;
static int pti_active;			/* raw slots in use */
static u8 *pti_av_mem[2], *pti_av_buf[2];
static int pti_av_dma[2] = { -1, -1 }, pti_av_pid[2] = { -1, -1 };
static int pti_pcr_pid = -1;

/* busy-wait 75 ticks of the high-priority timer (pti_wait) */
static void pti_wait(void)
{
	udelay(80);
}

static void pti_set_slot_pid(int slot, int pid)
{
	unsigned long a = PTI_PIDS + 4 * (slot >> 1);
	u32 v = dev_rd(a);

	if (slot & 1)
		v = (v & 0xffff) | (u32) pid << 16;
	else
		v = (v & 0xffff0000) | pid;
	dev_wr(a, v);
}

static int pti_dma_alloc(void)
{
	int i;

	for (i = 31; i >= 0; i--)
		if (pti_dma_free & (1UL << i)) {
			pti_dma_free &= ~(1UL << i);
			return i;
		}
	return -1;
}

static void pti_dma_desc(int d, u32 start, u32 end)
{
	dev_wr(PTI_DMADESC(d) + 0, start);
	dev_wr(PTI_DMADESC(d) + 4, end);
	dev_wr(PTI_DMADESC(d) + 8, start);
	dev_wr(PTI_DMADESC(d) + 12, start);
	dev_wr(PTI_DMADESC(d) + 16, start);
}

static void pti_slot_reset(int s)
{
	dev_wr(PTI_SLOT(s, 0), 0x20);
	dev_wr(PTI_SLOT(s, 1), 0xf0000000);
	dev_wr(PTI_SLOT(s, 2), 0xf000f000);
	dev_wr(PTI_SLOT(s, 3), 0);
}

static void pti_slot_irq(int s, int on)
{
	unsigned long f;

	save_flags(f);
	cli();
	dev_wr(PTI_REG(9 + (s >> 4)), 1UL << (s & 15));		/* IntAck */
	if (on)
		pti_enable[s >> 4] |= 1UL << (s & 15);
	else
		pti_enable[s >> 4] &= ~(1UL << (s & 15));
	dev_wr(PTI_REG(5 + (s >> 4)), pti_enable[s >> 4]);	/* IntEnable */
	restore_flags(f);
}

/* point a slot at DMA descriptor d (slot word 2, high half = offset from 0x20030000) */
static void pti_slot_dma(int s, int d)
{
	dev_wr(PTI_SLOT(s, 2), ((PTI_DMADESC(d) - PTI) << 16) | (dev_rd(PTI_SLOT(s, 2)) & 0xffff));
}

/* PTI DMA channel n (1 = video, 2 = audio) over [start, end] (pti_start_list) */
static void pti_av_channel(int n, u32 start, u32 end)
{
	unsigned long f;

	save_flags(f);
	cli();
	dev_wr(PTI_REG(0x414), 15 - (1 << n));
	dev_wr(PTI_REG(0x400 + n), start);
	dev_wr(PTI_REG(0x404 + n), end);
	dev_wr(PTI_REG(0x408 + n), start);
	dev_wr(PTI_REG(0x40c + n), start);
	dev_wr(PTI_REG(0x414), 15);
	restore_flags(f);
}

/* ---- raw streams ---- */

static int pti_raw_open(int pid, void (*feed)(int, const u8 *))
{
	struct pti_raw *r;
	int s, d;
	u32 start;

	for (s = 3; s < PTI_PCR_SLOT; s++)
		if (pti_raw[s].pid < 0)
			break;
	if (s == PTI_PCR_SLOT || (d = pti_dma_alloc()) < 0)
		return -EBUSY;
	r = &pti_raw[s];
	if (!(r->mem = kmalloc(PTI_RAW_SIZE + 16, GFP_KERNEL))) {
		pti_dma_free |= 1UL << d;
		return -ENOMEM;
	}
	r->buf = (u8 *) (((unsigned long) r->mem + 15) & ~15UL);
	r->dma = d;
	r->feed = feed;
	r->packets = r->overflows = 0;
	start = (u32) r->buf;
	r->rp = start;
	pti_slot_reset(s);
	pti_dma_desc(d, start, start + PTI_RAW_SIZE - 16);
	pti_slot_dma(s, d);
	dev_wr(PTI_SLOT(s, 3), 0x1010 | 0x40);	/* raw stream, alternate output default */
	r->pid = pid;
	pti_slot_irq(s, 1);
	pti_set_slot_pid(s, pid);
	if (!pti_active++)
		mod_timer(&pti_timer, jiffies + 1);
	return s;
}

static void pti_raw_close(int s)
{
	struct pti_raw *r = &pti_raw[s];

	if (s < 3 || s >= PTI_PCR_SLOT || r->pid < 0)
		return;
	pti_set_slot_pid(s, PTI_NO_PID);
	pti_wait();
	pti_slot_irq(s, 0);
	tasklet_disable(&pti_tasklet);
	r->pid = -1;
	tasklet_enable(&pti_tasklet);
	pti_dma_free |= 1UL << r->dma;
	kfree(r->mem);
	r->mem = r->buf = 0;
	pti_active--;
}

/* hand the packets between read and write pointer to the slot's feed */
static void pti_raw_drain(int s)
{
	struct pti_raw *r = &pti_raw[s];
	u32 start = (u32) r->buf, end = start + PTI_RAW_SIZE;
	u32 wp = dev_rd(PTI_DMADESC(r->dma) + 12);
	int n = 0;

	if (wp < start || wp >= end)
		return;
	while (r->rp != wp && n++ < PTI_RAW_PKTS) {
		const u8 *p = (const u8 *) r->rp;

		r->packets++;
		if (p[0] == 0x47)
			r->feed(r->pid, p);
		r->rp += 188;
		if (r->rp >= end)
			r->rp = start;
	}
	dev_wr(PTI_DMADESC(r->dma) + 8, r->rp);
}

static void pti_do_tasklet(unsigned long x)
{
	int s;

	for (s = 3; s < PTI_PCR_SLOT; s++)
		if (pti_raw[s].pid >= 0)
			pti_raw_drain(s);
}

static void pti_tick(unsigned long x)
{
	pti_polls++;
	tasklet_schedule(&pti_tasklet);
	if (pti_active)
		mod_timer(&pti_timer, jiffies + 1);
}

static void pti_interrupt(int irq, void *dev, struct pt_regs *regs)
{
	u32 st1 = dev_rd(PTI_REG(1)) & pti_enable[0];
	u32 st2 = dev_rd(PTI_REG(2)) & pti_enable[1];
	u32 st3 = dev_rd(PTI_REG(3)) & pti_enable[2];
	u32 st0 = dev_rd(PTI_REG(0)) & pti_int0;

	pti_irqs++;
	if (st1)
		dev_wr(PTI_REG(9), st1);
	if (st2)
		dev_wr(PTI_REG(10), st2);
	if (st3)
		dev_wr(PTI_REG(11), st3);
	if (st0)
		dev_wr(PTI_REG(8), st0);
	tasklet_schedule(&pti_tasklet);
}

/* ---- audio / video / PCR ---- */

/* start (pid >= 0) or stop (pid < 0) the decoder input slot 0 (video) or 1 (audio) */
static void pti_av(int s, int pid)
{
	u32 start = (u32) pti_av_buf[s], end = start + PTI_AV_SIZE - 16;
	u32 v;

	if (pti_av_pid[s] >= 0) {
		/* pti_list_enable(0): discard, then reset the buffer and the DMA channel */
		pti_set_slot_pid(s, PTI_NO_PID);
		dev_wr(PTI_SLOT(s, 3), dev_rd(PTI_SLOT(s, 3)) | 0x80);
		pti_wait();
		pti_dma_desc(pti_av_dma[s], start, end);
		pti_av_channel(s + 1, start, end);
		pti_av_pid[s] = -1;
	}
	if (pid < 0)
		return;
	pti_av_pid[s] = pid;
	dev_wr(PTI_SLOT(s, 0), (dev_rd(PTI_SLOT(s, 0)) & 0xffff0000) | 0x20);
	v = dev_rd(PTI_SLOT(s, 3)) & ~0x80;
	dev_wr(PTI_SLOT(s, 3), v);
	pti_set_slot_pid(s, pid);
}

static void pti_av_setup(int s)
{
	u32 start, end;
	int d = pti_dma_alloc();

	pti_av_dma[s] = d;
	start = (u32) pti_av_buf[s];
	end = start + PTI_AV_SIZE - 16;
	pti_slot_reset(s);
	pti_av_channel(s + 1, start, end);
	pti_dma_desc(d, start, end);
	pti_slot_dma(s, d);
	/* video: 0x400 | 4 (buffered), audio: 8 (buffered); plus alternate output default,
	 * disabled (0x80) until a PID is set */
	dev_wr(PTI_SLOT(s, 3), (s ? 0x008 : 0x404) | 0x40 | 0x80);
}

static void pti_set_pcr(int pid)
{
	pti_pcr_pid = pid;
	dev_wr(PTI_PCR_PID, (dev_rd(PTI_PCR_PID) & 0xffff0000) | (pid < 0 ? PTI_NO_PID : pid));
	pti_set_slot_pid(PTI_PCR_SLOT, pid < 0 ? PTI_NO_PID : pid);
}

/* last PCR in 27 MHz units / 300 = 90 kHz base (pti_get_pcr) */
static u32 pti_get_pcr_base(void)
{
	u32 w = dev_rd(PTI_PCR_VAL), x = dev_rd(PTI_PCR_VAL + 4);

	return w << 17 | (w >> 16) << 1 | ((x >> 15) & 1);
}

/* ---- set-up (pti_tc_init, pti_init_state) ---- */

static void pti_tc_init(void)
{
	int i;
#ifdef DVB_HAVE_PTI_TC
	u32 a = 0, b = 0;
#endif

	dev_wr(PTI_TC_CTRL, 10);			/* pti_tc_stop */
	dev_wr(0x200320e0, 1);				/* not ICAM mode */
	dev_wr(PTI_REG(0xc), 0);
	for (i = 8; i < 12; i++)
		dev_wr(PTI_REG(i), 0xffff);
	for (i = 4; i < 8; i++)
		dev_wr(PTI_REG(i), 0);
	dev_wr(PTI_REG(0x15), 0);
	dev_wr(PTI_REG(0x14), 0);
	dev_wr(PTI_REG(0x414), 0);
	dev_wr(PTI_REG(0x418), 0x4000);
	for (i = 0x400; i < 0x410; i++)
		dev_wr(PTI_REG(i), 0);
	dev_wr(PTI_REG(0x410), 0);
	dev_wr(PTI_REG(0x415), 1);
	dev_wr(PTI_REG(0x411), 1);
	dev_wr(PTI_REG(0x416), 0x1f);
	dev_wr(PTI_REG(0x412), 1);
	dev_wr(PTI_REG(0x417), 0);
	dev_wr(PTI_REG(0x413), 1);
	dev_wr(PTI_REG(0x414), 0xf);
	dev_wr(PTI_REG(0x802), 0);
	dev_wr(PTI_REG(0x804), 0x5e);
	dev_wr(PTI_REG(0x805), 0);
	dev_wr(PTI_REG(0x806), 0);
	for (i = 0; i < 0x200; i++)
		dev_wr(PTI_REG(0x1000 + i), 0);
	for (i = 0; i < 0x300; i++)
		dev_wr(PTI_DATA + 4 * i, 0);
	for (i = 0; i < 0x400; i++)
		dev_wr(PTI_IRAM + 4 * i, 0);
	for (i = 0; i < 24; i++)
		dev_wr(PTI_PIDS + 4 * i, 0xe000e000);
	for (i = 0; i < 4; i++)
		pti_slot_reset(i);
	dev_wr(0x20038350, 0);
	dev_wr(0x20038354, 0xf0000000);
	dev_wr(0x20038358, 0xf000f000);
	dev_wr(0x2003835c, 0x80 | 0x40);		/* alternate output action 1 */
	dev_wr(PTI_PCR_PID, (dev_rd(PTI_PCR_PID) & 0xffff0000) | PTI_NO_PID);
	dev_wr(PTI_FILT_EN, 0);
	dev_wr(PTI_ALTOUT, 0x1080 | 0x40);
#ifdef DVB_HAVE_PTI_TC
	for (i = 0; i < 729; i++) {
		dev_wr(PTI_IRAM + 4 * i, pti_tc_code[i]);
		a ^= pti_tc_code[i];
		b ^= dev_rd(PTI_IRAM + 4 * i);
	}
	pti_tc_loaded = a == b;
	if (!pti_tc_loaded)
		printk(KERN_ERR "dvb: TC code loading failed\n");
#endif
	udelay(1000);
	dev_wr(PTI_REG(0x802), 1);			/* input FIFO on */
	dev_wr(PTI_REG(0xc), 2);			/* TC reset */
	dev_wr(PTI_REG(0xc), 1);			/* TC run */
}

static int pti_proc(char *buf)
{
	int s, n;

	n = sprintf(buf, "pti: %s, irqs %lu, polls %lu, video pid %d, audio pid %d, pcr pid %d"
		    " (base %u)\n", pti_tc_loaded ? "TC loaded" : "no TC code", pti_irqs,
		    pti_polls, pti_av_pid[0], pti_av_pid[1], pti_pcr_pid, pti_get_pcr_base());
	for (s = 3; s < PTI_PCR_SLOT; s++)
		if (pti_raw[s].pid >= 0)
			n += sprintf(buf + n, "  slot %d pid 0x%04x: %lu packets\n", s,
				     pti_raw[s].pid, pti_raw[s].packets);
	return n;
}

static int pti_init(void)
{
	int s;

	for (s = 0; s < PTI_SLOTS; s++)
		pti_raw[s].pid = -1;
	init_timer(&pti_timer);
	pti_timer.function = pti_tick;
	tasklet_init(&pti_tasklet, pti_do_tasklet, 0);
	for (s = 0; s < 2; s++) {
		if (!(pti_av_mem[s] = kmalloc(PTI_AV_SIZE + 16, GFP_KERNEL)))
			return -ENOMEM;
		pti_av_buf[s] = (u8 *) (((unsigned long) pti_av_mem[s] + 15) & ~15UL);
	}
	pti_tc_init();
	pti_av_setup(0);
	pti_av_setup(1);
	pti_set_pcr(-1);
	if (request_irq(PTI_IRQ, pti_interrupt, 0, "pti", &pti_raw) == 0)
		pti_running = 1;
	return 0;
}

static void pti_exit(void)
{
	int s;

	for (s = 3; s < PTI_PCR_SLOT; s++)
		pti_raw_close(s);
	pti_av(0, -1);
	pti_av(1, -1);
	pti_set_pcr(-1);
	del_timer(&pti_timer);
	tasklet_kill(&pti_tasklet);
	if (pti_running)
		free_irq(PTI_IRQ, &pti_raw);
	dev_wr(PTI_REG(0xc), 0);
	dev_wr(PTI_TC_CTRL, 10);
	for (s = 0; s < 2; s++)
		kfree(pti_av_mem[s]);
}
