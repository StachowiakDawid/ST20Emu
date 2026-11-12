/*
 * dvbtool.c - DVB-S user tools for uClinux/ST20 on the DSR9500 (Linux DVB API v3), one
 * binary with the applets (by argv[0] or as the first argument):
 *
 *   scan [-o FILE] [-l LOF] [-D] TRANSPONDERS... | -f FILE
 *        tune each transponder (FREQ_MHZ:POL:SR_KSPS[:SAT], or dvb-apps "S 11836000 H
 *        27500000 3/4" lines in FILE), read PAT, PMT and SDT, write channels.conf
 *        (default /etc/config/channels.conf) in szap format:
 *            name:freq_MHz:pol:sat:sr_ksps:vpid[+pcr]:apid:sid
 *   zap [-c FILE] [-l LOF] [-D] [-n COUNT] [-r] NAME|NUMBER
 *        tune, start video/audio/PCR on the decoders, print the frontend status once a
 *        second (COUNT times, default for ever); -r: the A/V PIDs to dvr0 instead
 *        (TS recording, as szap -r)
 *   tv [-c FILE] [-l LOF] [-D] [NUMBER]
 *        TV application: live video under an OSD channel list on /dev/fb0, driven by the
 *        remote (/dev/input/event0): up/down select, OK zaps, digits zap by number,
 *        P+/P- next/previous channel, Menu/Exit show/hide the list, Power quits
 *   femon   frontend status (status, signal, SNR, BER) once
 *
 * LNB: universal by default (LOF 9750/10600 MHz, switch at 11700 MHz: 22 kHz tone for the
 * high band), V/R = 13 V, H/L = 18 V; -l LOF for a single-LOF LNB (MHz), -l L1,L2,SW for
 * other dual ones.  DiSEqC 1.0 committed switch + tone burst for SAT 0-3 as szap does
 * (-D: no DiSEqC).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/video.h>
#include <linux/dvb/audio.h>

#define ADAPTER "/dev/dvb/adapter0/"
#define CONF "/etc/config/channels.conf"

static unsigned lof1 = 9750, lof2 = 10600, slof = 11700;	/* MHz */
static int use_diseqc = 1;

struct chan {
	char name[40];
	unsigned freq, sr;		/* MHz, ksym/s */
	char pol;
	int sat, vpid, apid, pcr, sid;
};

static void msleep(int ms)
{
	usleep(ms * 1000);
}

/* 1 if fd is readable within ms (select: uClibc's poll() emulation does not see evdev
   events here) */
static int readable(int fd, int ms)
{
	fd_set r;
	struct timeval tv;

	FD_ZERO(&r);
	FD_SET(fd, &r);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = ms % 1000 * 1000;
	return select(fd + 1, &r, 0, 0, &tv) > 0;
}

static void parse_lnb(const char *s)
{
	unsigned a = 0, b = 0, c = 0;
	int n = sscanf(s, "%u,%u,%u", &a, &b, &c);

	lof1 = a;
	lof2 = n >= 2 ? b : a;
	slof = n >= 3 ? c : n == 2 ? 11700 : 0;
}

/* ---- frontend ---- */

static int fe_fd = -1;

static int fe_open(void)
{
	if (fe_fd < 0 && (fe_fd = open(ADAPTER "frontend0", O_RDWR)) < 0)
		perror(ADAPTER "frontend0");
	return fe_fd;
}

/* tune and wait up to timeout ms for a lock; returns 1 if locked */
static int tune(unsigned freq, char pol, unsigned sr, int sat, int timeout)
{
	struct dvb_frontend_parameters p;
	fe_status_t st = 0;
	int hi = slof && freq >= slof, h = pol == 'h' || pol == 'H' || pol == 'l' || pol == 'L';
	int t;

	if (fe_open() < 0)
		return 0;
	ioctl(fe_fd, FE_SET_TONE, SEC_TONE_OFF);
	ioctl(fe_fd, FE_SET_VOLTAGE, h ? SEC_VOLTAGE_18 : SEC_VOLTAGE_13);
	if (use_diseqc && sat >= 0) {
		struct dvb_diseqc_master_cmd c = { { 0xe0, 0x10, 0x38, 0xf0 }, 4 };

		c.msg[3] = 0xf0 | ((sat * 4) & 0x0f) | (hi ? 1 : 0) | (h ? 2 : 0);
		msleep(15);
		ioctl(fe_fd, FE_DISEQC_SEND_MASTER_CMD, &c);
		msleep(15);
		ioctl(fe_fd, FE_DISEQC_SEND_BURST, (sat & 1) ? SEC_MINI_B : SEC_MINI_A);
		msleep(15);
	}
	ioctl(fe_fd, FE_SET_TONE, hi ? SEC_TONE_ON : SEC_TONE_OFF);
	memset(&p, 0, sizeof(p));
	p.frequency = (freq > (hi ? lof2 : lof1) ? freq - (hi ? lof2 : lof1) :
		       (hi ? lof2 : lof1) - freq) * 1000;
	p.inversion = INVERSION_AUTO;
	p.u.qpsk.symbol_rate = sr * 1000;
	p.u.qpsk.fec_inner = FEC_AUTO;
	if (ioctl(fe_fd, FE_SET_FRONTEND, &p) < 0) {
		perror("FE_SET_FRONTEND");
		return 0;
	}
	for (t = 0; t < timeout; t += 100) {
		msleep(100);
		if (ioctl(fe_fd, FE_READ_STATUS, &st) == 0 && (st & FE_HAS_LOCK))
			return 1;
	}
	return 0;
}

static void fe_print(void)
{
	fe_status_t st = 0;
	unsigned sig = 0, snr = 0;	/* the ioctls write 16 bits (little endian); not
				   unsigned short: logs/gcc-2.7.2-t800-short-vararg.c */
	unsigned ber = 0, unc = 0;

	ioctl(fe_fd, FE_READ_STATUS, &st);
	ioctl(fe_fd, FE_READ_SIGNAL_STRENGTH, &sig);
	ioctl(fe_fd, FE_READ_SNR, &snr);
	ioctl(fe_fd, FE_READ_BER, &ber);
	ioctl(fe_fd, FE_READ_UNCORRECTED_BLOCKS, &unc);
	printf("status %02x | signal %04x | snr %04x | ber %08x | unc %08x | %s\n", st,
	       sig, snr, ber, unc, st & FE_HAS_LOCK ? "FE_HAS_LOCK" : "");
	fflush(stdout);
}

/* ---- sections ---- */

/* one section of table tid (ext: table id extension or -1) on pid; returns its length */
static int get_section(int pid, int tid, int ext, int secnum, unsigned char *buf, int timeout)
{
	struct dmx_sct_filter_params f;
	int fd, n = -1;

	if ((fd = open(ADAPTER "demux0", O_RDWR)) < 0) {
		perror(ADAPTER "demux0");
		return -1;
	}
	memset(&f, 0, sizeof(f));
	f.pid = pid;
	f.filter.filter[0] = tid;
	f.filter.mask[0] = 0xff;
	if (ext >= 0) {
		f.filter.filter[1] = ext >> 8;
		f.filter.filter[2] = ext;
		f.filter.mask[1] = f.filter.mask[2] = 0xff;
	}
	if (secnum >= 0) {
		f.filter.filter[4] = secnum;
		f.filter.mask[4] = 0xff;
	}
	f.timeout = timeout;
	f.flags = DMX_CHECK_CRC | DMX_IMMEDIATE_START | DMX_ONESHOT;
	if (ioctl(fd, DMX_SET_FILTER, &f) == 0) {
		if (readable(fd, timeout + 500))
			n = read(fd, buf, 4096);
	}
	close(fd);
	return n;
}

static int sec_len(const unsigned char *s)
{
	return 3 + ((s[1] & 0x0f) << 8 | s[2]);
}

/* text of a DVB string (drop the character table byte and control codes) */
static void dvb_text(char *out, int max, const unsigned char *s, int n)
{
	int i, k = 0;

	if (n > 0 && s[0] < 0x20) {
		i = s[0] == 0x10 ? 3 : 1;
		s += i;
		n -= i;
	}
	for (i = 0; i < n && k < max - 1; i++)
		if (s[i] >= 0x20 && s[i] != ':' && (s[i] < 0x80 || s[i] >= 0xa0))
			out[k++] = s[i];
	out[k] = 0;
}

/* ---- channels.conf ---- */

static struct chan *chans;
static int nchans;

static int load_conf(const char *file)
{
	FILE *f = fopen(file, "r");
	char line[200], *p, *q;
	struct chan c;

	if (!f) {
		perror(file);
		return -1;
	}
	nchans = 0;
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || !(p = strchr(line, ':')))
			continue;
		*p++ = 0;
		memset(&c, 0, sizeof(c));
		strncpy(c.name, line, sizeof(c.name) - 1);
		c.freq = strtoul(p, &p, 10);
		if (*p++ != ':')
			continue;
		c.pol = *p;
		if (!(p = strchr(p, ':')))
			continue;
		c.sat = strtol(p + 1, &p, 10);
		c.sr = strtoul(p + 1, &p, 10);
		c.vpid = strtol(p + 1, &p, 10);
		c.pcr = *p == '+' ? strtol(p + 1, &p, 10) : c.vpid;
		c.apid = strtol(p + 1, &q, 10);
		c.sid = *q == ':' ? strtol(q + 1, 0, 10) : 0;
		if (!c.freq || !c.sr)
			continue;
		chans = realloc(chans, (nchans + 1) * sizeof(c));
		chans[nchans++] = c;
	}
	fclose(f);
	return nchans;
}

/* ---- scan ---- */

static int scan_tp(unsigned freq, char pol, unsigned sr, int sat, FILE *out)
{
	static unsigned char pat[4096], sec[4096];
	int progs[64], pmts[64], np = 0, i, j, n, found = 0, last, s;
	struct chan c[64];

	printf("tuning %u %c %u: ", freq, pol, sr);
	fflush(stdout);
	if (!tune(freq, pol, sr, sat, 3000)) {
		printf("no lock\n");
		return 0;
	}
	printf("locked\n");
	if ((n = get_section(0, 0x00, -1, -1, pat, 3000)) < 12) {
		printf("  no PAT\n");
		return 0;
	}
	for (i = 8; i + 4 <= sec_len(pat) - 4 && np < 64; i += 4) {
		int prog = pat[i] << 8 | pat[i + 1], pid = (pat[i + 2] & 0x1f) << 8 | pat[i + 3];

		if (prog) {
			progs[np] = prog;
			pmts[np++] = pid;
		}
	}
	memset(c, 0, sizeof(c));
	for (i = 0; i < np; i++) {
		int pil, es;

		c[i].freq = freq;
		c[i].pol = tolower(pol);
		c[i].sat = sat < 0 ? 0 : sat;
		c[i].sr = sr;
		c[i].sid = progs[i];
		sprintf(c[i].name, "service %d", progs[i]);
		if ((n = get_section(pmts[i], 0x02, progs[i], -1, sec, 2000)) < 16)
			continue;
		c[i].pcr = (sec[8] & 0x1f) << 8 | sec[9];
		pil = (sec[10] & 0x0f) << 8 | sec[11];
		for (j = 12 + pil; j + 5 <= sec_len(sec) - 4; j += 5 + es) {
			int type = sec[j], pid = (sec[j + 1] & 0x1f) << 8 | sec[j + 2];

			es = (sec[j + 3] & 0x0f) << 8 | sec[j + 4];
			if ((type == 1 || type == 2) && !c[i].vpid)
				c[i].vpid = pid;
			else if ((type == 3 || type == 4) && !c[i].apid)
				c[i].apid = pid;
		}
	}
	/* SDT actual: service names */
	last = 0;
	for (s = 0; s <= last; s++) {
		if ((n = get_section(0x11, 0x42, -1, s, sec, 3000)) < 11)
			break;
		last = sec[7];
		for (j = 11; j + 5 <= sec_len(sec) - 4;) {
			int sid = sec[j] << 8 | sec[j + 1], dl = (sec[j + 3] & 0x0f) << 8 | sec[j + 4];
			int d = j + 5;

			while (d + 2 <= j + 5 + dl) {
				if (sec[d] == 0x48) {
					int pl = sec[d + 3], nl = sec[d + 4 + pl];

					for (i = 0; i < np; i++)
						if (c[i].sid == sid)
							dvb_text(c[i].name, sizeof(c[i].name),
								 sec + d + 5 + pl, nl);
				}
				d += 2 + sec[d + 1];
			}
			j += 5 + dl;
		}
	}
	for (i = 0; i < np; i++) {
		if (!c[i].vpid && !c[i].apid)
			continue;
		found++;
		if (c[i].pcr && c[i].pcr != c[i].vpid)
			sprintf((char *) sec, "%d+%d", c[i].vpid, c[i].pcr);
		else
			sprintf((char *) sec, "%d", c[i].vpid);
		printf("  %s:%u:%c:%d:%u:%s:%d:%d\n", c[i].name, c[i].freq, c[i].pol, c[i].sat,
		       c[i].sr, sec, c[i].apid, c[i].sid);
		if (out)
			fprintf(out, "%s:%u:%c:%d:%u:%s:%d:%d\n", c[i].name, c[i].freq, c[i].pol,
				c[i].sat, c[i].sr, sec, c[i].apid, c[i].sid);
	}
	return found;
}

static int scan_main(int argc, char **argv)
{
	const char *out = CONF, *list = 0;
	FILE *o;
	int i, total = 0;
	unsigned f, sr;
	char pol, line[200];
	int sat;

	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "-o") && i + 1 < argc)
			out = argv[++i];
		else if (!strcmp(argv[i], "-f") && i + 1 < argc)
			list = argv[++i];
		else if (!strcmp(argv[i], "-l") && i + 1 < argc)
			parse_lnb(argv[++i]);
		else if (!strcmp(argv[i], "-D"))
			use_diseqc = 0;
		else
			break;
	}
	if (i == argc && !list) {
		fprintf(stderr, "usage: scan [-o FILE] [-l LOF] [-D] FREQ:POL:SR[:SAT]... | -f FILE\n");
		return 2;
	}
	if (!(o = fopen(out, "w")))
		perror(out);
	for (; i < argc; i++) {
		sat = -1;
		pol = 'h';
		if (sscanf(argv[i], "%u:%c:%u:%d", &f, &pol, &sr, &sat) >= 3)
			total += scan_tp(f, pol, sr, sat, o);
	}
	if (list) {
		FILE *l = fopen(list, "r");

		while (l && fgets(line, sizeof(line), l))
			if (sscanf(line, "S %u %c %u", &f, &pol, &sr) == 3)
				total += scan_tp(f / 1000, pol, sr / 1000, -1, o);
		if (l)
			fclose(l);
	}
	if (o)
		fclose(o);
	printf("scan: %d services, written to %s\n", total, out);
	return total ? 0 : 1;
}

/* ---- zapping ---- */

static int vfd = -1, afd = -1, dmx_v = -1, dmx_a = -1, dmx_p = -1, dvr_v = -1, dvr_a = -1;

static int pes_filter(int pid, dmx_output_t out, dmx_pes_type_t type)
{
	struct dmx_pes_filter_params f;
	int fd;

	if (pid <= 0 || pid >= 0x1fff || (fd = open(ADAPTER "demux0", O_RDWR)) < 0)
		return -1;
	f.pid = pid;
	f.input = DMX_IN_FRONTEND;
	f.output = out;
	f.pes_type = type;
	f.flags = DMX_IMMEDIATE_START;
	if (ioctl(fd, DMX_SET_PES_FILTER, &f) < 0) {
		perror("DMX_SET_PES_FILTER");
		close(fd);
		return -1;
	}
	return fd;
}

static void av_stop(void)
{
	int *fds[7] = { &dmx_v, &dmx_a, &dmx_p, &dvr_v, &dvr_a, 0, 0 };
	int i;

	if (vfd >= 0)
		ioctl(vfd, VIDEO_STOP, 0);
	if (afd >= 0)
		ioctl(afd, AUDIO_STOP, 0);
	for (i = 0; fds[i]; i++)
		if (*fds[i] >= 0) {
			close(*fds[i]);
			*fds[i] = -1;
		}
}

static int zap_to(struct chan *c, int dvr)
{
	int locked;

	av_stop();
	locked = tune(c->freq, c->pol, c->sr, c->sat, 5000);
	if (vfd < 0)
		vfd = open(ADAPTER "video0", O_RDWR);
	if (afd < 0)
		afd = open(ADAPTER "audio0", O_RDWR);
	if (dvr) {
		/* as szap -r: the A/V PIDs to dvr0 instead of the decoders (a PID goes to one
		   PTI slot: the decoder's or a software one) */
		dvr_v = pes_filter(c->vpid, DMX_OUT_TS_TAP, DMX_PES_OTHER);
		dvr_a = pes_filter(c->apid, DMX_OUT_TS_TAP, DMX_PES_OTHER);
		return locked;
	}
	if (c->pcr)
		dmx_p = pes_filter(c->pcr, DMX_OUT_DECODER, DMX_PES_PCR);
	dmx_v = pes_filter(c->vpid, DMX_OUT_DECODER, DMX_PES_VIDEO);
	dmx_a = pes_filter(c->apid, DMX_OUT_DECODER, DMX_PES_AUDIO);
	if (vfd >= 0 && c->vpid) {
		ioctl(vfd, VIDEO_SET_BLANK, 1);
		ioctl(vfd, VIDEO_PLAY, 0);
	}
	if (afd >= 0 && c->apid)
		ioctl(afd, AUDIO_PLAY, 0);
	return locked;
}

static struct chan *find_chan(const char *s)
{
	char *e;
	int i, n = strtol(s, &e, 10);

	if (!*e)
		return n >= 1 && n <= nchans ? &chans[n - 1] : 0;
	for (i = 0; i < nchans; i++)
		if (!strcasecmp(chans[i].name, s))
			return &chans[i];
	for (i = 0; i < nchans; i++)
		if (strstr(chans[i].name, s))
			return &chans[i];
	return 0;
}

static int common_opt(int argc, char **argv, int *i, const char **conf)
{
	if (!strcmp(argv[*i], "-c") && *i + 1 < argc)
		*conf = argv[++*i];
	else if (!strcmp(argv[*i], "-l") && *i + 1 < argc)
		parse_lnb(argv[++*i]);
	else if (!strcmp(argv[*i], "-D"))
		use_diseqc = 0;
	else
		return 0;
	return 1;
}

static int zap_main(int argc, char **argv)
{
	const char *conf = CONF;
	int i, count = -1, dvr = 0;
	struct chan *c;

	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
		if (common_opt(argc, argv, &i, &conf))
			continue;
		if (!strcmp(argv[i], "-n") && i + 1 < argc)
			count = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-r"))
			dvr = 1;
		else
			break;
	}
	if (i != argc - 1) {
		fprintf(stderr, "usage: zap [-c FILE] [-l LOF] [-D] [-n COUNT] [-r] NAME|NUMBER\n");
		return 2;
	}
	if (load_conf(conf) <= 0 || !(c = find_chan(argv[i]))) {
		fprintf(stderr, "zap: no channel \"%s\" in %s\n", argv[i], conf);
		return 1;
	}
	printf("zapping to %d \"%s\": %u %c %u, video %d, audio %d, pcr %d, sid %d\n",
	       (int) (c - chans) + 1, c->name, c->freq, c->pol, c->sr, c->vpid, c->apid, c->pcr,
	       c->sid);
	fflush(stdout);
	zap_to(c, dvr);
	while (count < 0 || count-- > 0) {
		fe_print();
		sleep(1);
	}
	av_stop();
	return 0;
}

static int femon_main(int argc, char **argv)
{
	if (fe_open() < 0)
		return 1;
	fe_print();
	return 0;
}

/* ---- tv: OSD channel list over live video ---- */

#include <linux/fb.h>
#include <linux/input.h>
#include <asm/font8x16.h>

#define W 640
#define H 480
#define ROWS 12
enum { C_CLEAR, C_BOX, C_TEXT, C_SEL, C_SELTEXT, C_TITLE, C_DIM };

static unsigned char *fb;
static int fb_pitch, fbfd = -1;

static void fill(int x, int y, int w, int h, int c)
{
	int i;

	for (i = 0; i < h; i++)
		memset(fb + (y + i) * fb_pitch + x, c, w);
}

static void text(int x, int y, const char *s, int fg, int bg, int maxc)
{
	int i, r, b;

	for (; *s && maxc-- > 0; s++, x += 8)
		for (r = 0; r < 16; r++) {
			unsigned char bits = st20_font8x16[(unsigned char) *s * 16 + r];
			unsigned char *p = fb + (y + r) * fb_pitch + x;

			for (b = 0; b < 8; b++)
				p[b] = bits & (0x80 >> b) ? fg : bg;
		}
}

static int fb_setup(void)
{
	/* clear (transparent: the video shows), box, text, selection, selected text, title,
	   dim */
	static unsigned short r[7] = { 0, 0x1000, 0xffff, 0xe000, 0x0000, 0xffff, 0x8000 };
	static unsigned short g[7] = { 0, 0x1800, 0xffff, 0xa000, 0x0000, 0xe000, 0x8000 };
	static unsigned short b[7] = { 0, 0x5000, 0xffff, 0x0000, 0x0000, 0x4000, 0x8000 };
	static unsigned short t[7] = { 0xffff, 0, 0, 0, 0, 0, 0 };
	struct fb_cmap cm;
	struct fb_fix_screeninfo fix;

	if ((fbfd = open("/dev/fb0", O_RDWR)) < 0) {
		perror("/dev/fb0");
		return -1;
	}
	ioctl(fbfd, FBIOGET_FSCREENINFO, &fix);
	fb_pitch = fix.line_length;
	fb = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
	if (fb == MAP_FAILED) {
		perror("mmap /dev/fb0");
		return -1;
	}
	cm.start = 0;
	cm.len = 7;
	cm.red = r;
	cm.green = g;
	cm.blue = b;
	cm.transp = t;
	ioctl(fbfd, FBIOPUTCMAP, &cm);
	fill(0, 0, W, H, C_CLEAR);
	return 0;
}

static void draw_list(int show, int sel, int cur, const char *digits, const char *msg)
{
	int i, top, y;
	char buf[64];

	fill(0, 0, W, H, C_CLEAR);
	/* banner: current channel */
	fill(40, 400, 560, 40, C_BOX);
	if (cur >= 0)
		sprintf(buf, "%3d  %-34.34s", cur + 1, chans[cur].name);
	else
		strcpy(buf, "no channel");
	text(56, 412, buf, C_TITLE, C_BOX, 40);
	text(56 + 41 * 8, 412, digits[0] ? digits : msg, C_TEXT, C_BOX, 24);
	if (!show)
		return;
	fill(40, 40, 360, 32 + ROWS * 24, C_BOX);
	text(56, 48, "Channels", C_TITLE, C_BOX, 40);
	top = sel - ROWS / 2;
	if (top > nchans - ROWS)
		top = nchans - ROWS;
	if (top < 0)
		top = 0;
	for (i = top, y = 72; i < nchans && i < top + ROWS; i++, y += 24) {
		sprintf(buf, "%3d %-36.36s", i + 1, chans[i].name);
		if (i == sel)
			fill(48, y - 4, 344, 24, C_SEL);
		text(56, y, buf, i == sel ? C_SELTEXT : C_TEXT, i == sel ? C_SEL : C_BOX, 42);
	}
}

static int tv_main(int argc, char **argv)
{
	const char *conf = CONF;
	int i, n, evfd, cur = 0, sel = 0, show = 1, nd = 0, dirty = 0, etype, ecode, evalue;
	unsigned char raw[16];
	char digits[8] = "", msg[32] = "";
	struct timeval last = { 0, 0 }, now;

	for (i = 1; i < argc && argv[i][0] == '-'; i++)
		if (!common_opt(argc, argv, &i, &conf))
			break;
	if (load_conf(conf) <= 0) {
		fprintf(stderr, "tv: no channels in %s (run scan)\n", conf);
		return 1;
	}
	if (i < argc)
		cur = sel = atoi(argv[i]) > 0 && atoi(argv[i]) <= nchans ? atoi(argv[i]) - 1 : 0;
	if ((evfd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK)) < 0) {
		perror("/dev/input/event0");
		return 1;
	}
	if (fb_setup())
		return 1;
	draw_list(show, sel, cur, digits, "tuning");
	strcpy(msg, zap_to(&chans[cur], 0) ? "" : "no signal");
	printf("tv: %d \"%s\"%s\n", cur + 1, chans[cur].name, msg[0] ? " (no signal)" : "");
	fflush(stdout);
	draw_list(show, sel, cur, digits, msg);
	for (;;) {
		int zap = -1;

		/* the remote, polled non-blocking; the 16-bit fields are taken from the bytes
		   (gcc-t800 compared ev.type as a whole word: logs/gcc-2.7.2-t800-short-vararg.c) */
		n = read(evfd, raw, sizeof(raw));
		if (n == sizeof(raw)) {
			etype = raw[8] | raw[9] << 8;
			ecode = raw[10] | raw[11] << 8;
			evalue = raw[12] | raw[13] << 8 | raw[14] << 16 | raw[15] << 24;
			dirty = 1;
			if (getenv("TVDEBUG"))
				fprintf(stderr, "tv: key %d %d\n", ecode, evalue);
			if (etype != EV_KEY || evalue == 0)
				continue;
			switch (ecode) {
			case KEY_UP:
				sel = sel > 0 ? sel - 1 : nchans - 1;
				show = 1;
				break;
			case KEY_DOWN:
				sel = sel < nchans - 1 ? sel + 1 : 0;
				show = 1;
				break;
			case KEY_ENTER:
				if (nd)
					zap = atoi(digits) - 1;
				else if (show)
					zap = sel;
				else
					show = 1;
				break;
			case KEY_PAGEUP:
				zap = cur < nchans - 1 ? cur + 1 : 0;
				break;
			case KEY_PAGEDOWN:
				zap = cur > 0 ? cur - 1 : nchans - 1;
				break;
			case KEY_MENU:
				show = !show;
				break;
			case KEY_EXIT:
			case KEY_ESC:
				show = 0;
				break;
			case KEY_POWER:
				goto quit;
			default:
				if (ecode >= KEY_1 && ecode <= KEY_0 && nd < 3) {
					digits[nd++] = ecode == KEY_0 ? '0' : '1' + ecode - KEY_1;
					digits[nd] = 0;
					gettimeofday(&last, 0);
				}
			}
		} else {
			readable(evfd, 50);		/* or 50 ms */
			gettimeofday(&now, 0);
			if (nd && now.tv_sec - last.tv_sec >= 2)
				zap = atoi(digits) - 1;
		}
		if (zap < 0 && nd && atoi(digits) * 10 > nchans)
			zap = atoi(digits) - 1;		/* no longer number possible */
		if (zap >= 0 && nd) {
			nd = 0;
			digits[0] = 0;
		}
		if (zap >= 0 && zap < nchans) {
			cur = sel = zap;
			draw_list(show, sel, cur, digits, "tuning");
			strcpy(msg, zap_to(&chans[cur], 0) ? "" : "no signal");
			printf("tv: %d \"%s\"%s\n", cur + 1, chans[cur].name,
			       msg[0] ? " (no signal)" : "");
			fflush(stdout);
			dirty = 1;
		}
		if (dirty)
			draw_list(show, sel, cur, digits, msg);
		dirty = 0;
	}
quit:
	av_stop();
	fill(0, 0, W, H, C_CLEAR);
	close(fbfd);
	return 0;
}

int main(int argc, char **argv)
{
	const char *n = strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0];

	if (!strcmp(n, "dvbtool") && argc > 1) {
		argv++;
		argc--;
		n = argv[0];
	}
	if (!strcmp(n, "scan"))
		return scan_main(argc, argv);
	if (!strcmp(n, "zap"))
		return zap_main(argc, argv);
	if (!strcmp(n, "tv"))
		return tv_main(argc, argv);
	if (!strcmp(n, "femon"))
		return femon_main(argc, argv);
	fprintf(stderr, "dvbtool: scan, zap, tv or femon\n");
	return 2;
}
