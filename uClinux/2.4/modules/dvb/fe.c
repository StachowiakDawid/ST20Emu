/*
 * fe.c - DVB-S frontend of the DSR9500: HM1221 QPSK demodulator + tuner PLL on I2C 0xaa
 * (SSC1), LNB supply/tone on the EMI latch 0x50400000, DiSEqC from the demodulator.
 *
 * Register sequences follow the DSR9500 main application (Decompilation/DSR9500:
 * a_7fe81640.c QPSK driver, a_7fe84150.c TUNER_taskProcess, a_7fe75df8.c STI2C glue,
 * a_7fe9c528.c board latches), with the double arithmetic done in integers.
 * A kernel thread does what TUNER_taskProcess does: tune, wait for lock with growing
 * timeouts, step the frequency +-1..3 MHz, then track the lock (AGC loop step, levels).
 */

#define FE_I2C_ADDR	0xaa

static struct semaphore i2c_sem;
static int i2c_errors;

/* ---- I2C master on SSC1, polled ---- */

static int ssc_frame(u32 frame, u32 *rx)
{
	int n;

	dev_wr(SSC1 + SSC_TBUF, frame);
	for (n = 0; n < 4000; n++) {
		if (dev_rd(SSC1 + SSC_STAT) & SSC_STAT_RIR) {
			*rx = dev_rd(SSC1 + SSC_RBUF) & 0x1ff;
			return 0;
		}
		udelay(1);
	}
	return -ETIMEDOUT;
}

static void ssc_stop(void)
{
	int n;

	dev_wr(SSC1 + SSC_I2C, 0x0d);			/* I2CM | STOPG | ACKG */
	for (n = 0; n < 1000 && (dev_rd(SSC1 + SSC_STAT) & SSC_STAT_BUSY); n++)
		udelay(1);
	dev_wr(SSC1 + SSC_I2C, 0x09);
}

/* one transaction: START, address, len bytes written or read, STOP */
static int i2c_xfer(int addr, u8 *buf, int len, int rd)
{
	u32 rx;
	int i, r;

	dev_wr(SSC1 + SSC_CON, 0x398);			/* soft reset */
	dev_wr(SSC1 + SSC_CON, 0x318);			/* enable, master */
	dev_wr(SSC1 + SSC_I2C, 0x0b);			/* I2CM | STRTG | ACKG */
	r = ssc_frame(((addr | rd) << 1) | 1, &rx);
	dev_wr(SSC1 + SSC_I2C, 0x09);
	if (!r && (rx & 1))
		r = -EREMOTEIO;				/* address not acknowledged */
	for (i = 0; !r && i < len; i++) {
		if (rd) {
			if (i == len - 1)
				dev_wr(SSC1 + SSC_I2C, 0x01);	/* last byte: no ACK */
			r = ssc_frame(0x1ff, &rx);
			buf[i] = rx >> 1;
		} else {
			r = ssc_frame((buf[i] << 1) | 1, &rx);
			if (!r && (rx & 1))
				r = -EREMOTEIO;
		}
	}
	ssc_stop();
	if (r)
		i2c_errors++;
	return r;
}

static void i2c_setup(void)
{
	/* PIO3 bits 0 and 2: outputs high, then alternate function bidirectional (open drain) */
	dev_wr(PIO3 + PIO_SET_OUT, 5);
	dev_wr(PIO3 + PIO_SET_C0, 5);
	dev_wr(PIO3 + PIO_SET_C1, 5);
	dev_wr(PIO3 + PIO_SET_C2, 5);
	dev_wr(SSC1 + SSC_I2C, 0);
	/* ~100 kHz: BRG = fcomms / (2 * baud), fcomms ~60 MHz */
	dev_wr(SSC1 + SSC_BRG, 300);
	dev_wr(SSC1 + SSC_CON, 0x318);
	dev_wr(SSC1 + SSC_IEN, 0);
	ssc_stop();
}

static int qpsk_wr(int reg, int val)
{
	u8 b[2];
	int r;

	b[0] = reg;
	b[1] = val;
	down(&i2c_sem);
	r = i2c_xfer(FE_I2C_ADDR, b, 2, 0);
	up(&i2c_sem);
	return r;
}

static int qpsk_rd(int reg)
{
	u8 b;
	int r;

	b = reg;
	down(&i2c_sem);
	r = i2c_xfer(FE_I2C_ADDR, &b, 1, 0);
	if (!r)
		r = i2c_xfer(FE_I2C_ADDR, &b, 1, 1);
	up(&i2c_sem);
	return r ? 0 : b;
}

/* ---- board latch ---- */

static u8 lnb_latch = 0x10 | LNB_DEMOD_RUN | LNB_TONE_OFF;

static void latch_set(int bits, int on)
{
	unsigned long f;

	save_flags(f);
	cli();
	if (on)
		lnb_latch |= bits;
	else
		lnb_latch &= ~bits;
	dev_wrb(LATCH_LNB, lnb_latch);
	restore_flags(f);
}

static void msleep(int ms)
{
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((ms * HZ + 999) / 1000);
}

/* ---- HM1221 ---- */

/* 3-byte word into the PLL / loop register file through 0x6d/0x72 (f_7fe81640) */
static void pll_word(const u8 *w)
{
	int i, v = 0;

	for (i = 0; i < 20; i++) {
		v = qpsk_rd(0x6d);
		if ((v & 0xc0) == 0x40)
			break;
	}
	if (i == 20) {
		printk(KERN_DEBUG "dvb-fe: VCO time-out\n");
		return;
	}
	qpsk_wr(0x6d, v & 0xfc);
	for (i = 0; i < 3; i++)
		qpsk_wr(0x72, w[i]);
	qpsk_wr(0x6d, v | 2);
}

static void shift24(u8 *p, int bit)
{
	p[2] >>= 1;
	if (p[1] & 1)
		p[2] |= 0x80;
	p[1] >>= 1;
	if (p[0] & 1)
		p[1] |= 0x80;
	p[0] >>= 1;
	if (bit)
		p[0] |= 0x80;
}

/* loop filter words (f_7fe81724: tail 0,1,0; f_7fe817ac: tail 0,0,1) */
static void loop_word(unsigned a, unsigned b, int which)
{
	u8 w[3];
	unsigned i;

	w[0] = w[1] = w[2] = 0;
	for (i = 0; i < a; i++)
		shift24(w, 0);
	for (; i < 9; i++)
		shift24(w, 1);
	for (i = 0; i < b; i++)
		shift24(w, 0);
	for (; i < 9; i++)
		shift24(w, 1);
	shift24(w, 0);
	shift24(w, which == 0);
	shift24(w, which != 0);
	pll_word(w);
}

/* PLL step tables in 0.1 kHz (doubles 1011.2/505.6/252.8 and 505.6/252.8/126.4 kHz) */
static const u16 vco_hi[3] = { 10112, 5056, 2528 };
static const u16 vco_lo[3] = { 5056, 2528, 1264 };

static u32 vco_round(u32 f, u32 d)
{
	return (f * 10 + d / 2) / d * d;
}

static const struct { u32 lo, hi; u8 b1, b2, low; } pll_bands[] = {
	{ 927999, 1018000, 0x02, 0x00, 1 }, { 1017999, 1075000, 0x04, 0x00, 1 },
	{ 1074999, 1178000, 0x10, 0x08, 0 }, { 1177999, 1296000, 0x10, 0x10, 0 },
	{ 1295999, 1432000, 0x10, 0x20, 0 }, { 1431999, 1576000, 0x10, 0x40, 0 },
	{ 1575999, 1718000, 0x10, 0x80, 0 }, { 1717999, 1856000, 0x11, 0x00, 0 },
	{ 1855999, 2036000, 0x12, 0x00, 0 }, { 2035999, 2150001, 0x14, 0x00, 0 },
};

/* program the tuner for IF freq (kHz) with step 1000/500/250 kHz; returns the PLL
 * frequency in kHz (f_7fe81837) */
static u32 tuner_set_freq(u32 freq, int step)
{
	u8 buf[6];
	int idx, i;
	u32 d, div;

	idx = step == 250 ? 2 : step == 500 ? 1 : 0;
	buf[3] = 0xf8 - 0x10 * idx;
	for (i = 0; i < 2 && idx > 0; i++) {
		if (vco_round(freq, vco_hi[idx - 1]) == vco_round(freq, vco_hi[idx])) {
			idx--;
			buf[3] += 0x10;
		}
	}
	for (i = 0; i < sizeof(pll_bands) / sizeof(pll_bands[0]); i++)
		if (freq > pll_bands[i].lo && freq < pll_bands[i].hi)
			break;
	if (i == sizeof(pll_bands) / sizeof(pll_bands[0]))
		return freq;
	buf[1] = pll_bands[i].b1;
	buf[2] = pll_bands[i].b2;
	buf[3] |= 4;
	d = pll_bands[i].low ? vco_lo[idx] : vco_hi[idx];
	div = ((freq * 10 + d / 2) / d) & 0xffff;
	if (div > 0x1fff)
		buf[3] |= 1;
	if (div & 0x1f) {
		buf[4] = div >> 5;
		buf[5] = div << 3;
	} else {
		buf[4] = (div - 32) >> 5;
		buf[5] = (div - 32) << 3;
	}
	buf[0] = 0;
	pll_word(buf);
	pll_word(buf + 3);
	for (i = 0; i < 40; i++)
		if (qpsk_rd(0x66) & 0x20)
			break;
	if (i == 40)
		printk(KERN_DEBUG "dvb-fe: no VCO lock at %u kHz\n", freq);
	return div * d / 10;
}

static int agc1, agc2, agc3;		/* shadows of registers 0x28, 0x34, 0x3f */

/* restart acquisition (f_7fe81fbf) */
static void demod_restart(void)
{
	agc1 = 0x1a;
	qpsk_wr(0x28, agc1);
	agc2 = 0x35;
	qpsk_wr(0x34, agc2);
	agc3 = 0x10;
	qpsk_wr(0x4b, 0xe3);
	qpsk_wr(9, 0x14);
	qpsk_wr(9, 0);
	qpsk_wr(4, 5);
}

static const u8 demod_init_tab[] = {
	0x04, 0x14, 0x05, 0x13, 0x0b, 0x0a, 0x0c, 0x02, 0x10, 0x30, 0x10, 0x70, 0x15, 0x27,
	0x1e, 0xff, 0x24, 0x04, 0x29, 0x00, 0x36, 0x40, 0x38, 0x87, 0x3c, 0x0f, 0x41, 0x00,
	0x42, 0x00, 0x43, 0x00, 0x49, 0x22, 0x4a, 0x03, 0x4b, 0xe3, 0x51, 0x03, 0x56, 0x42,
	0x57, 0x01, 0x61, 0xd5, 0x62, 0x07, 0x63, 0x00, 0x64, 0x20, 0x67, 0x04, 0x6c, 0x01,
	0x6e, 0x00, 0x6f, 0x00, 0x70, 0x15, 0x76, 0x10,
};

static int fe_last_sr, fe_last_fec = -1;

/* f_7fe8200a */
static void demod_init(void)
{
	static const u8 w1[3] = { 0x5f, 0x0c, 0x00 }, w2[3] = { 0x80, 0x71, 0xf8 };
	int i;

	for (i = 0; i < sizeof(demod_init_tab); i += 2)
		qpsk_wr(demod_init_tab[i], demod_init_tab[i + 1]);
	pll_word(w1);
	pll_word(w2);
	demod_restart();
	fe_last_sr = 0;
	fe_last_fec = -1;
}

/* reset pulse and id check (f_7fe852b0, f_7fe81f3f): 0 if an HM1221 answered */
static int demod_probe(void)
{
	latch_set(LNB_DEMOD_RUN, 1);
	msleep(1);
	latch_set(LNB_DEMOD_RUN, 0);
	msleep(7);
	latch_set(LNB_DEMOD_RUN, 1);
	msleep(1);
	if (qpsk_wr(9, 0x37) || qpsk_wr(9, 0))
		return -ENODEV;
	return qpsk_rd(0) == 0x5a ? 0 : -ENODEV;
}

static const struct { u32 min; u16 ki, kf; u8 r6, r7; u32 c; } sr_tab[] = {
	{ 40400, 368, 7341, 0x78, 3, 90999 },	/* 368.734074 */
	{ 30300, 414, 8258, 0x87, 2, 80888 },	/* 414.825833 */
	{ 22700, 553, 1011, 0xa5, 1, 60666 },	/* 553.101111 */
	{ 0, 737, 4681, 0x78, 0, 45499 },	/* 737.468148 */
};
static const u8 fec_tab[8][3] = {
	{ 0xae, 5, 6 }, { 2, 1, 1 }, { 4, 2, 3 }, { 8, 5, 6 }, { 0x10, 8, 0xf },
	{ 0x20, 0x15, 0x1a }, { 0x40, 0x25, 0x3a }, { 0x80, 0x45, 0x7a },
};

/* symbol rate (ksym/s) and code rate (0 = auto, 1-7 = FEC_1_2 .. FEC_7_8) (f_7fe821b0) */
static void demod_set_rate(int fec, u32 sr)
{
	int i, y;
	u32 x;

	if (sr != fe_last_sr) {
		for (i = 0; sr <= sr_tab[i].min; i++)
			;
		x = sr * sr_tab[i].ki + (sr * sr_tab[i].kf + 5000) / 10000;
		qpsk_wr(1, (x >> 16) & 0xff);
		qpsk_wr(2, (x >> 8) & 0xff);
		qpsk_wr(3, x & 0xff);
		qpsk_wr(6, sr_tab[i].r6);
		qpsk_wr(7, sr_tab[i].r7);
		y = (int) (sr / 1000) * 2324 / 100 - 362;
		if (y > 511)
			y = 511;
		else if (y < -512)
			y = -512;
		if (((y >> 2) & 0xf0) == 0xb0)
			y = 0x300;
		qpsk_wr(0x57, y & 3);
		qpsk_wr(0x56, (y >> 2) & 0xff);
		if (sr > (sr_tab[i].c >> 3)) {
			qpsk_wr(0x29, 3);
			qpsk_wr(0x34, 0x35);
		} else if (sr > (sr_tab[i].c >> 5)) {
			qpsk_wr(0x29, 2);
			qpsk_wr(0x34, 0x25);
		} else {
			qpsk_wr(0x29, 1);
			qpsk_wr(0x34, 0x15);
		}
		if (sr < 5000) {
			loop_word(3, 3, 0);
			loop_word(3, 3, 1);
		} else if (sr < 15000) {
			loop_word(5, 7, 0);
			loop_word(5, 3, 1);
		} else {
			loop_word(7, 8, 0);
			loop_word(6, 3, 1);
		}
	}
	if (fec < 0 || fec > 7)
		fec = 0;
	if (fec != fe_last_fec) {
		qpsk_wr(0x18, fec_tab[fec][0]);
		qpsk_wr(0x1a, fec_tab[fec][1]);
		qpsk_wr(0x1b, fec_tab[fec][2]);
	}
	fe_last_sr = sr;
	fe_last_fec = fec;
}

static int demod_locked(void)
{
	return (qpsk_rd(8) & 0x60) == 0x60;
}

/* AGC loop step while acquiring (f_7fe8253c) */
static void demod_track(void)
{
	int s, t;

	s = qpsk_rd(0x55);
	if ((s & 0x18) != 0x10)
		return;
	if (s & 1) {
		t = qpsk_rd(0x32);
		if (t < 0x80) {
			if (agc2 & 0x70)
				qpsk_wr(0x34, agc2 = (agc2 - 0x10) & 0xff);
		} else if ((t & 0x70) < 0x70)
			qpsk_wr(0x34, agc2 = (agc2 + 0x10) & 0xff);
	}
	if (s & 2) {
		t = qpsk_rd(0x27);
		if (t & 0x80) {
			if (agc1 < 0x1f)
				qpsk_wr(0x28, ++agc1);
		} else if (agc1 > 3)
			qpsk_wr(0x28, --agc1);
	}
	if (s & 4) {
		/* (t & 0x3f) < 0x80 is always true in the firmware */
		if (agc3 & 3)
			qpsk_wr(0x3f, --agc3);
	}
}

/* carrier offset in kHz (f_7fe826ca) */
static int demod_offset(u32 sr)
{
	int n = (qpsk_rd(0x47) << 4) | (qpsk_rd(0x48) & 0xf);

	if (n & 0x800)
		n -= 0x1000;
	/* n * sr * 0.06103515625 / 1000 = n * sr / 16384 */
	return n * (int) sr / 16384;
}

/* ---- DiSEqC (f_7fe82770) ---- */

static int diseqc_wait(void)
{
	int n;

	for (n = 0; n < 50; n++) {
		if (qpsk_rd(0x76) & 0x40)
			return 0;
		msleep(10);
	}
	return -ETIMEDOUT;
}

static int diseqc_send(const u8 *msg, int len, int burst)
{
	int i, r;

	if (len < 3 || len > 6)
		return -EINVAL;
	if ((r = diseqc_wait()))
		return r;
	qpsk_wr(0x77, 0xb0);
	for (i = 0; i < len; i++)
		qpsk_wr(0x79 + i, msg[i]);
	qpsk_wr(0x76, 0x40 | (len - 3) | (burst ? 8 : 0));
	r = diseqc_wait();
	qpsk_wr(0x76, 0x10);
	return r;
}

static int diseqc_burst(int b)
{
	int r;

	if ((r = diseqc_wait()))
		return r;
	qpsk_wr(0x77, 0xb4);
	qpsk_wr(0x76, b ? 0x48 : 0x40);
	r = diseqc_wait();
	qpsk_wr(0x76, 0x10);
	return r;
}

/* ---- tuning thread (TUNER_taskProcess) ---- */

static struct {
	struct semaphore lock;		/* parameters, register sequences of the thread */
	wait_queue_head_t wake;		/* the thread waits here */
	wait_queue_head_t events;	/* FE_GET_EVENT / poll */
	int present;			/* demodulator answered the probe */
	int request;			/* new parameters for the thread */
	int stop;
	struct dvb_frontend_parameters p;	/* last FE_SET_FRONTEND */
	u32 pll;			/* PLL frequency set (kHz) */
	int offset;			/* carrier offset at lock (kHz) */
	fe_status_t status;
	int level, quality;		/* f_7fe84be6 0..250, f_7fe84bd3 */
	u32 errors, ber;
	struct dvb_frontend_event ev[4];
	int ev_head, ev_count;
	int voltage, tone;
	pid_t pid;
} fe;

static void fe_event(fe_status_t st)
{
	struct dvb_frontend_event *e;

	if (fe.ev_count == 4) {
		fe.ev_head = (fe.ev_head + 1) & 3;
		fe.ev_count--;
	}
	e = &fe.ev[(fe.ev_head + fe.ev_count) & 3];
	e->status = st;
	e->parameters = fe.p;
	if (st & FE_HAS_LOCK)
		e->parameters.frequency = fe.pll + fe.offset;
	fe.ev_count++;
	wake_up_interruptible(&fe.events);
}

static void fe_set_status(fe_status_t st)
{
	if (st != fe.status) {
		fe.status = st;
		fe_event(st);
	}
}

/* signal level and quality while locked (f_7fe84be6, f_7fe84bd3, error counter
 * f_7fe84b94) */
static void fe_measure(void)
{
	int n, v;

	v = qpsk_rd(0x27);
	fe.quality = 40 - (signed char) v;
	qpsk_wr(0x6a, 0x84);
	for (n = 0; n < 200 && (qpsk_rd(0x6a) & 0x80); n++)
		;
	if (n < 200) {
		v = qpsk_rd(0x68) << 8 | qpsk_rd(0x69);
		fe.level = 250 - (v & 0xffff) / 100;
	}
	if (qpsk_rd(0x10) & 0x40) {
		fe.errors = qpsk_rd(0x13) << 8 | qpsk_rd(0x12);
		fe.ber += fe.errors;
		qpsk_wr(0x10, 0x30);
		qpsk_wr(0x10, 0x70);
	}
}

static int fe_thread(void *arg)
{
	int state = 2, k = 0, dir = -2, step = 1000, cnt = 0;
	int timeout = 0, base = 0, limit = 0;	/* ms */
	u32 freq = 0, sr = 0;
	int fec = 0;

	daemonize();
	strcpy(current->comm, "kdvb-fe");
	down(&fe.lock);
	demod_init();
	up(&fe.lock);
	while (!fe.stop) {
		if (state == 2 || state == 0 || state == 1) {
			int ms = state == 2 ? -1 : state == 1 ? 100 : timeout;

			if (ms < 0)
				wait_event_interruptible(fe.wake, fe.request || fe.stop);
			else
				interruptible_sleep_on_timeout(&fe.wake, (ms * HZ + 999) / 1000 ?: 1);
			if (signal_pending(current)) {
				spin_lock_irq(&current->sigmask_lock);
				flush_signals(current);
				spin_unlock_irq(&current->sigmask_lock);
			}
		}
		if (fe.stop)
			break;
		down(&fe.lock);
		if (fe.request) {
			fe.request = 0;
			freq = fe.p.frequency;
			sr = fe.p.u.qpsk.symbol_rate / 1000;
			fec = fe.p.u.qpsk.fec_inner;
			if (fec > FEC_7_8)
				fec = 0;
			dir = sr > 9000 ? -2 : -1;
			step = 1000;
			fe.pll = tuner_set_freq(freq, step);
			demod_set_rate(fec, sr);
			demod_restart();
			fe.offset = 0;
			fe.level = 0;
			fe.quality = 5;
			k = 0;
			if (sr < 1000)
				sr = 1000;
			/* firmware: 5500000 / sr and 330000000 / sr clock ticks of 64 us */
			timeout = base = 352000 / sr;
			limit = 21120000 / sr;
			state = 0;
			fe_set_status(0);
		} else if (state == 0) {
			demod_track();
			if (demod_locked()) {
				fe.offset = -demod_offset(sr);
				qpsk_wr(4, 0xc);
				qpsk_wr(0x10, 0x30);
				qpsk_wr(0x10, 0x70);
				state = 1;
				cnt = 0;
				fe_measure();
				fe_set_status(FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI |
					      FE_HAS_SYNC | FE_HAS_LOCK);
			} else {
				if (k < 1) {
					if (dir > k) {
						k = 0;
						if ((timeout *= 4) > limit) {
							timeout = base;
							fe_set_status(FE_TIMEDOUT);
						}
					} else
						k = 1 - k;
				} else
					k = -k;
				fe.pll = tuner_set_freq(freq + k * step, step);
				if (k == 0)
					demod_restart();
			}
		} else if (state == 1) {
			if (demod_locked()) {
				cnt = 0;
				fe_measure();
			} else if (++cnt > 2) {
				state = 0;
				demod_restart();
				fe.level = 0;
				fe.quality = 5;
				fe_set_status(0);
			}
		}
		up(&fe.lock);
	}
	fe.pid = 0;
	return 0;
}

/* ---- /dev/dvb/adapter0/frontend0 ---- */

static int fe_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		    unsigned long arg)
{
	int r = 0;

	switch (cmd) {
	case FE_GET_INFO: {
		static struct dvb_frontend_info info;

		memset(&info, 0, sizeof(info));
		strcpy(info.name, "HM1221 QPSK (DSR9500)");
		info.type = FE_QPSK;
		info.frequency_min = 950000;
		info.frequency_max = 2150000;
		info.frequency_stepsize = 1000;
		info.frequency_tolerance = 3000;
		info.symbol_rate_min = 1000000;
		info.symbol_rate_max = 45000000;
		info.symbol_rate_tolerance = 500;
		info.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK;
		return copy_to_user((void *) arg, &info, sizeof(info)) ? -EFAULT : 0;
	}
	case FE_READ_STATUS:
		return put_user(fe.status, (fe_status_t *) arg);
	case FE_READ_BER:
		return put_user(fe.ber, (u32 *) arg);
	case FE_READ_UNCORRECTED_BLOCKS:
		return put_user(fe.errors, (u32 *) arg);
	case FE_READ_SIGNAL_STRENGTH: {
		int v = fe.status & FE_HAS_LOCK ? fe.level : 0;

		v = v < 0 ? 0 : v > 250 ? 0xffff : v * 0xffff / 250;
		return put_user((u16) v, (u16 *) arg);
	}
	case FE_READ_SNR: {
		int v = fe.status & FE_HAS_LOCK ? fe.quality : 0;

		v = v < 0 ? 0 : v > 150 ? 0xffff : v * 0xffff / 150;
		return put_user((u16) v, (u16 *) arg);
	}
	case FE_SET_FRONTEND: {
		struct dvb_frontend_parameters p;

		if (copy_from_user(&p, (void *) arg, sizeof(p)))
			return -EFAULT;
		if (p.frequency < 950000 || p.frequency > 2150000 ||
		    p.u.qpsk.symbol_rate < 1000000 || p.u.qpsk.symbol_rate > 45000000)
			return -EINVAL;
		down(&fe.lock);
		fe.p = p;
		fe.request = 1;
		fe.ber = 0;
		fe.errors = 0;
		fe.status = 0;
		fe.ev_count = 0;
		up(&fe.lock);
		wake_up_interruptible(&fe.wake);
		return 0;
	}
	case FE_GET_FRONTEND: {
		struct dvb_frontend_parameters p = fe.p;

		if (fe.status & FE_HAS_LOCK) {
			p.frequency = fe.pll + fe.offset;
			p.u.qpsk.fec_inner = qpsk_rd(0x22) & 7;
		}
		p.inversion = INVERSION_AUTO;
		return copy_to_user((void *) arg, &p, sizeof(p)) ? -EFAULT : 0;
	}
	case FE_GET_EVENT: {
		struct dvb_frontend_event e;

		if (!fe.ev_count) {
			if (file->f_flags & O_NONBLOCK)
				return -EWOULDBLOCK;
			if (wait_event_interruptible(fe.events, fe.ev_count))
				return -ERESTARTSYS;
		}
		cli();
		e = fe.ev[fe.ev_head];
		fe.ev_head = (fe.ev_head + 1) & 3;
		fe.ev_count--;
		sti();
		return copy_to_user((void *) arg, &e, sizeof(e)) ? -EFAULT : 0;
	}
	case FE_SET_VOLTAGE:
		if (arg > SEC_VOLTAGE_OFF)
			return -EINVAL;
		fe.voltage = arg;
		latch_set(LNB_POWER, arg != SEC_VOLTAGE_OFF);
		latch_set(LNB_18V, arg == SEC_VOLTAGE_18);
		return 0;
	case FE_ENABLE_HIGH_LNB_VOLTAGE:
		return 0;
	case FE_SET_TONE:
		if (arg > SEC_TONE_OFF)
			return -EINVAL;
		fe.tone = arg;
		latch_set(LNB_TONE_OFF, arg == SEC_TONE_OFF);
		return 0;
	case FE_DISEQC_SEND_MASTER_CMD: {
		struct dvb_diseqc_master_cmd c;

		if (copy_from_user(&c, (void *) arg, sizeof(c)))
			return -EFAULT;
		down(&fe.lock);
		r = diseqc_send(c.msg, c.msg_len, 0);
		up(&fe.lock);
		return r;
	}
	case FE_DISEQC_SEND_BURST:
		if (arg > SEC_MINI_B)
			return -EINVAL;
		down(&fe.lock);
		r = diseqc_burst(arg == SEC_MINI_B);
		up(&fe.lock);
		return r;
	case FE_DISEQC_RESET_OVERLOAD:
		return 0;
	case FE_DISEQC_RECV_SLAVE_REPLY:
		return -EOPNOTSUPP;
	}
	return -ENOTTY;
}

static unsigned int fe_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &fe.events, wait);
	return fe.ev_count ? POLLIN | POLLRDNORM | POLLPRI : 0;
}

static int fe_open(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations fe_fops = {
	ioctl: fe_ioctl,
	poll: fe_poll,
	open: fe_open,
};

static int fe_proc(char *buf)
{
	return sprintf(buf, "frontend: %s, status 0x%02x, IF %u kHz (PLL %u, offset %d), "
		       "SR %u, level %d, quality %d, errors %u, i2c errors %d, LNB latch 0x%02x\n",
		       fe.present ? "HM1221" : "no demodulator", fe.status, fe.p.frequency,
		       fe.pll, fe.offset, fe.p.u.qpsk.symbol_rate, fe.level, fe.quality,
		       fe.ber, i2c_errors, lnb_latch);
}

static int fe_init(void)
{
	sema_init(&i2c_sem, 1);
	sema_init(&fe.lock, 1);
	init_waitqueue_head(&fe.wake);
	init_waitqueue_head(&fe.events);
	fe.voltage = SEC_VOLTAGE_OFF;
	fe.tone = SEC_TONE_OFF;
	dev_wrb(LATCH_LNB, lnb_latch);
	i2c_setup();
	fe.present = demod_probe() == 0;
	if (!fe.present)
		printk(KERN_WARNING "dvb: no HM1221 at I2C 0x%02x (continuing)\n", FE_I2C_ADDR);
	fe.pid = kernel_thread(fe_thread, 0, CLONE_FS | CLONE_FILES | CLONE_SIGNAL);
	return fe.pid < 0 ? fe.pid : 0;
}

static void fe_exit(void)
{
	fe.stop = 1;
	wake_up_interruptible(&fe.wake);
	while (fe.pid)
		msleep(20);
	latch_set(LNB_POWER, 0);
}
