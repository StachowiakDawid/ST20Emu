/*
 * av.c - /dev/dvb/adapter0/video0 and audio0 (DVB API v3) on the STi5512 MPEG decoders.
 * The decoders take their compressed data from the PTI's decoder slots (pti.c), whose
 * PIDs are set with DMX_SET_PES_FILTER (output DMX_OUT_DECODER) on demux0.
 */

static struct {
	video_play_state_t vstate;
	audio_play_state_t astate;
	int blank, mute, av_sync, bypass;
	audio_channel_select_t channel;
	audio_mixer_t mixer;
	video_format_t format;
	video_displayformat_t dformat;
	int vusers, ausers;
} av;

/* ---- STi5512 MPEG decoders (datasheet 7145560 ch. 19, register manual ch. 4/28) ---- */

extern unsigned long st20_avmem_start, st20_avmem_size;	/* arch/st20/kernel/setup.c */

#define SDRAM_BASE	0xc0000000UL
#define VBB_SIZE	0x34000		/* video bit buffer (208 KB; MP@ML VBV is 224 KB) */
#define ABB_SIZE	0x4000		/* audio bit buffer */
#define FRAME_SIZE	(45 * 36 * 384)	/* 720x576 4:2:0 */
#define LOW4MB		(SDRAM_BASE + 0x400000)

/*
 * Decoder memory: video bit buffer, three frames and the audio bit buffer (2,095,616
 * bytes) in one 2 MB page block in the low 4 MB of SDRAM (14-bit pointers in 256-byte
 * units).  Taken at the first VIDEO_PLAY / AUDIO_PLAY and kept (a 2 MB block is only
 * available while memory is not fragmented, i.e. soon after boot); or the kernel's boot
 * reservation "avmem=2048" (arch/st20/kernel/setup.c) when given.
 */
static unsigned long av_vmem, av_amem;

static unsigned long low_pages(int order)
{
	unsigned long p, bad[4];
	int n = 0;

	while ((p = __get_free_pages(GFP_KERNEL, order)) && p + (PAGE_SIZE << order) > LOW4MB &&
	       n < 4)
		bad[n++] = p;
	if (p && p + (PAGE_SIZE << order) > LOW4MB) {
		free_pages(p, order);
		p = 0;
	}
	while (n--)
		free_pages(bad[n], order);
	return p;
}

static int av_mem(void)
{
	if (av_vmem)
		return 0;
	if (st20_avmem_size >= VBB_SIZE + 3 * FRAME_SIZE + ABB_SIZE)
		av_vmem = st20_avmem_start;
	else
		av_vmem = low_pages(9);
	if (!av_vmem) {
		printk(KERN_WARNING "dvb: no decoder memory (a 2 MB block in the low 4 MB)\n");
		return -ENOMEM;
	}
	av_amem = av_vmem + VBB_SIZE + 3 * FRAME_SIZE;
	return 0;
}
#define VID_IRQ		21

#define vr(o)		dev_rdb(o)
#define vw(o, v)	dev_wrb(o, v)

static void vw16(int o, unsigned v)
{
	vw(o, (v >> 8) & 0xff);
	vw(o + 1, v & 0xff);
}

static const u8 intra_q[64] = {		/* default intra matrix, zigzag order */
	8, 16, 16, 19, 16, 19, 22, 22, 22, 22, 22, 22, 26, 24, 26, 27, 27, 27, 26, 26, 26, 26,
	27, 27, 27, 29, 29, 29, 34, 34, 34, 29, 29, 29, 27, 27, 29, 29, 32, 32, 34, 34, 37, 38,
	37, 35, 35, 34, 35, 38, 38, 40, 40, 40, 48, 48, 46, 46, 56, 56, 58, 69, 69, 83,
};

static struct {
	int running, irq;
	unsigned frame[3];		/* luma pointers (256-byte units) */
	unsigned chroma_off;		/* chroma after luma (units) */
	int mpeg2, mbw, mbh, w, h, aspect;
	int seq_seen, skip_done;	/* sequence header seen, first (skip) task issued */
	int ready;			/* headers of the next picture complete */
	int pct, pst, dcp, tff, fpfd, cmv, qst, ivf, azz, fh, fv, tref;
	int oldref, newref;		/* frame indexes, -1 none */
	int show;			/* frame to display at the next VSYNC, -1 none */
	int shown;
	unsigned long pictures, vsyncs, errors;
} vd = { 0 };

/* header FIFO: next byte, -1 if empty (VID_STA.HFE) */
static int hdf_n;
static int hdf(void)
{
	if (vr(0x65) & 0x04)
		return -1;
	hdf_n++;
	return vr(0x66);
}

static void load_matrix(int intra, const u8 *m, int from_fifo)
{
	int i, v;

	vw(0x69, intra ? 2 : 0);
	for (i = 0; i < 64; i++) {
		v = from_fifo ? hdf() : m ? m[i] : 16;
		vw(0x76, v < 0 ? 16 : v);
	}
	vw(0x69, 0);
}

static void vid_frame_size(void)
{
	int mbs = vd.mbw * vd.mbh;

	vw(0x25, vd.mbw);
	vw(0x24, (mbs >> 8) & 0x3f);		/* VID_DFS: serial, high byte first */
	vw(0x24, mbs & 0xff);
	vw(0x28, vd.mbw);			/* VID_XFW */
}

/* the header after a start code hit (code = its last byte) */
static void vid_header(int code)
{
	int b[8], i;

	switch (code) {
	case 0xb3:				/* sequence header */
		for (i = 0; i < 7; i++)
			b[i] = hdf();
		vd.w = b[0] << 4 | b[1] >> 4;
		vd.h = (b[1] & 15) << 8 | b[2];
		vd.aspect = b[3] >> 4;
		if (vd.w > 0 && vd.w <= 720 && vd.h > 0 && vd.h <= 576) {
			vd.mbw = (vd.w + 15) / 16;
			vd.mbh = (vd.h + 15) / 16;
			vid_frame_size();
		}
		/* b[6] bit 1: load_intra_quantiser_matrix (its 64 bytes are shifted by 1 bit:
		   take the defaults then, a stream's own matrices are rare in broadcasts) */
		load_matrix(1, intra_q, 0);
		load_matrix(0, 0, 0);
		vd.mpeg2 = 0;
		vd.seq_seen = 1;
		break;
	case 0xb5:				/* extension */
		b[0] = hdf();
		switch (b[0] >> 4) {
		case 1:				/* sequence extension */
			vd.mpeg2 = 1;
			break;
		case 8:				/* picture coding extension */
			for (i = 1; i < 5; i++)
				b[i] = hdf();
			vd.fh = (b[1] & 15) << 4 | (b[0] & 15);	/* BFH, FFH */
			vd.fv = (b[2] & 0xf0) | b[1] >> 4;		/* BFV, FFV */
			vd.dcp = (b[2] >> 2) & 3;
			vd.pst = b[2] & 3;
			vd.tff = b[3] >> 7;
			vd.fpfd = (b[3] >> 6) & 1;
			vd.cmv = (b[3] >> 5) & 1;
			vd.qst = (b[3] >> 4) & 1;
			vd.ivf = (b[3] >> 3) & 1;
			vd.azz = (b[3] >> 2) & 1;
			break;
		}
		break;
	case 0x00:				/* picture header */
		for (i = 0; i < 4; i++)
			b[i] = hdf();
		vd.tref = b[0] << 2 | b[1] >> 6;
		vd.pct = (b[1] >> 3) & 7;
		/* MPEG-1: full_pel + f_code after vbv_delay (bit 2 of b[3] on) */
		vd.fh = vd.fv = 0;
		if (vd.pct >= 2) {
			int fwd = (b[3] & 7) << 1, bwd = 0, x = hdf();

			fwd |= x >> 7;
			if (vd.pct == 3)
				bwd = (x >> 3) & 15;
			vd.fh = fwd | bwd << 4;
		}
		vd.pst = 3;
		vd.dcp = vd.tff = vd.cmv = vd.qst = vd.ivf = vd.azz = 0;
		vd.fpfd = 1;
		vd.ready = 0;
		break;
	case 0x01:				/* first slice: the picture's headers are complete */
		if (vd.pct >= 1 && vd.pct <= 3)
			vd.ready = 1;
		return;
	}
}

static void vid_search(void)
{
	if (hdf_n & 1)
		hdf();				/* even number of reads before a restart */
	hdf_n = 0;
	vw(0x69, 0x05);				/* HDS | SOS: stop on the first slice too */
}

static void vid_sch(void)
{
	int c;

	hdf_n = 0;
	c = hdf();
	if (!(vr(0x69) & 0x08) && c == 0x01)	/* SCM clear: 01, then the code */
		c = hdf();
	if (c < 0)
		return;
	vid_header(c);
	if (!vd.ready)
		vid_search();
	else if (hdf_n & 1)
		hdf();
}

static void vid_set_ptr(int o, int frame)
{
	unsigned l = vd.frame[frame];

	vw16(o, l);
	vw16(0x58 + (o - 0x0c), l + vd.chroma_off);	/* DFC/RFC/FFC/BFC follow the luma ones */
}

/* launch the next picture's decoding at the coming VSYNC */
static void vid_task(void)
{
	int r, tis = 0x01;			/* EXE */

	if (vd.pct == 3 && (vd.oldref < 0 || vd.newref < 0)) {
		/* B without two references (open GOP at the start): decode it, never show it */
		int ref = vd.newref >= 0 ? vd.newref : 0;

		r = ref == 0 ? 1 : 0;
		vid_set_ptr(0x10, ref);
		vid_set_ptr(0x12, ref);
		vd.show = -1;
		goto launch;
	}
	if (vd.pct == 3) {
		r = 3 - vd.oldref - vd.newref;
		vid_set_ptr(0x10, vd.oldref);	/* FFP */
		vid_set_ptr(0x12, vd.newref);	/* BFP */
		vd.show = r;
	} else {
		for (r = 0; r == vd.oldref || r == vd.newref; r++)
			;
		if (r > 2)
			r = vd.oldref >= 0 ? vd.oldref : 0;
		if (vd.newref >= 0)
			vid_set_ptr(0x10, vd.newref);
		vd.show = vd.newref;
		vd.oldref = vd.newref;
		vd.newref = r;
	}
launch:
	vid_set_ptr(0x0e, r);			/* RFP */
	if (r == vd.shown)
		tis |= 0x08;			/* OVW: decoding into the displayed frame */
	vw(0x04, vd.fh);
	vw(0x05, vd.fv);
	vw(0x06, (vd.pct & 3) << 4 | vd.dcp << 2 | (vd.mpeg2 ? vd.pst : 0));
	vw(0x07, vd.mpeg2 ? 0x40 | vd.tff << 5 | vd.fpfd << 4 | vd.cmv << 3 | vd.qst << 2 |
	   vd.ivf << 1 | vd.azz : 0);
	vw(0x03, tis);
	vd.pictures++;
	vd.ready = 0;
}

static void vid_vsync(int top)
{
	vd.vsyncs++;
	if (!top)
		return;
	if (vd.show >= 0) {
		vid_set_ptr(0x0c, vd.show);	/* DFP, DFC: from the next field */
		vd.shown = vd.show;
		vd.show = -1;
		vw(0x75, vr(0x75) | 0x20);	/* EVD */
	}
	if (!(vr(0x64) & 0x01))			/* pipeline busy (VID_STA.PII clear) */
		return;
	if (vd.seq_seen && !vd.skip_done) {
		vw(0x03, 0x01);			/* first task after a reset: skip to the sequence */
		vd.skip_done = 1;
	} else if (vd.skip_done && vd.ready && av.vstate == VIDEO_PLAYING)
		vid_task();
}

static void av_interrupt(int irq, void *dev, struct pt_regs *regs)
{
	int its0 = vr(0x3d), its2 = vr(0x62), its3 = vr(0x63);

	if (its0 & 0x40)
		vd.errors++;
	if (its2 & 0xc0)
		vd.errors++;			/* PDE / SER: decoding errors */
	if (!vd.running)
		return;
	if (its3 & 0x01)
		vid_sch();			/* SCH */
	if (its3 & 0x40)
		vid_vsync(1);			/* VST */
	if (its3 & 0x20)
		vid_vsync(0);			/* VSB */
}

static void vid_hw_reset(void)
{
	vw(0x39, 1);				/* VID_SRV */
	udelay(2);
	vw(0x39, 0);
	vw(0x02, 0x04);				/* pipeline reset */
	udelay(2);
	vw(0x02, 0x00);
}

static void vid_start(void)
{
	unsigned base;
	int i;

	if (av_mem())
		return;
	base = (av_vmem - SDRAM_BASE) >> 8;
	vd.running = 0;
	vw(0x75, vr(0x75) & ~0x20);
	vw(0x02, 0);
	vid_hw_reset();
	vw16(0x14, base);			/* VID_VBG */
	vw16(0x18, base + VBB_SIZE / 256 - 1);	/* VID_VBS: last 256-byte unit */
	vw16(0x1a, VBB_SIZE / 256 * 3 / 4);	/* VID_VBT */
	vd.mbw = 45;
	vd.mbh = 36;
	vd.chroma_off = 45 * 36;
	for (i = 0; i < 3; i++)
		vd.frame[i] = base + (VBB_SIZE + i * FRAME_SIZE) / 256;
	vid_frame_size();
	vd.seq_seen = vd.skip_done = vd.ready = 0;
	vd.oldref = vd.newref = vd.show = vd.shown = -1;
	vw(0x74, vr(0x74) & ~0x01);		/* FRZ off */
	vw(0x9b, 0);				/* VID_MWV: video under the OSD */
	vw(0x02, 0x01);				/* EDC */
	(void) vr(0x3d);
	vw(0x60, 0);				/* VID_ITM: PDE.. PII */
	vw(0x61, 0x61);				/* SCH, VST, VSB */
	vd.running = 1;
	vw(0x69, 0x05);				/* first search: a sequence header */
}

/* VIDEO_SET_BLANK semantics: blank (EVD off) when stopped, else keep the last picture */
static void vid_stop(void)
{
	vd.running = 0;
	vw(0x61, 0);
	vw(0x02, 0);
	if (av.blank)
		vw(0x75, vr(0x75) & ~0x20);
}

static void vid_freeze(int on)
{
	vw(0x74, on ? vr(0x74) | 1 : vr(0x74) & ~1);
}

static void vid_clear(void)
{
	if (vd.running)
		vid_start();
}

static void vid_blank(int on)
{
	if (on || vd.shown < 0)
		vw(0x75, vr(0x75) & ~0x20);
	else
		vw(0x75, vr(0x75) | 0x20);
}

static void aud_volume(int l, int r)
{
	/* attenuation in 2 dB steps: 255 -> 0 dB, 0 -> 126 dB */
	if (av.channel == AUDIO_MONO_LEFT)
		r = l;
	else if (av.channel == AUDIO_MONO_RIGHT)
		l = r;
	vw(0x81e, (255 - (l & 255)) / 4);
	vw(0x820, (255 - (r & 255)) / 4);
}

static void aud_mute(int on)
{
	vw(0x830, on ? 1 : 0);
}

static void aud_start(void)
{
	unsigned base;

	if (av_mem())
		return;
	base = (av_amem - SDRAM_BASE) >> 8;
	vw(0x82e, 0);				/* AUD_PLY off */
	vw(0x840, 1);				/* AUD_RES */
	udelay(2);
	vw(0x840, 0);
	vw16(0x1c, base);			/* VID_ABG */
	vw16(0x20, base + ABB_SIZE / 256 - 1);	/* VID_ABS */
	vw16(0x22, ABB_SIZE / 256 / 2);		/* VID_ABT */
	vw(0x823, 0);				/* AUD_SYN: PES start codes */
	vw(0x824, 0);				/* AUD_IDE: any stream id */
	vw(0x82a, 0);				/* AUD_CRC: no concealment */
	vw(0x870, 1);				/* AUD_BBE: through the bit buffer */
	aud_volume(av.mixer.volume_left, av.mixer.volume_right);
	aud_mute(av.mute);
	vw(0x82e, 1);				/* AUD_PLY */
}

static void aud_stop(void)
{
	vw(0x82e, 0);
	vw(0x830, 1);
}

static void aud_clear(void)
{
	if (av.astate != AUDIO_STOPPED)
		aud_start();
}

static int video_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		       unsigned long arg)
{
	switch (cmd) {
	case VIDEO_STOP:
		vid_stop();
		av.vstate = VIDEO_STOPPED;
		return 0;
	case VIDEO_PLAY:
		if (av_mem())
			return -ENOMEM;
		if (av.vstate == VIDEO_FREEZED)
			vid_freeze(0);
		else if (av.vstate == VIDEO_STOPPED)
			vid_start();
		av.vstate = VIDEO_PLAYING;
		return 0;
	case VIDEO_FREEZE:
		if (av.vstate != VIDEO_PLAYING)
			return -EINVAL;
		vid_freeze(1);
		av.vstate = VIDEO_FREEZED;
		return 0;
	case VIDEO_CONTINUE:
		if (av.vstate != VIDEO_FREEZED)
			return -EINVAL;
		vid_freeze(0);
		av.vstate = VIDEO_PLAYING;
		return 0;
	case VIDEO_SELECT_SOURCE:
		return arg == VIDEO_SOURCE_DEMUX ? 0 : -EINVAL;
	case VIDEO_SET_BLANK:
		av.blank = arg != 0;
		if (av.vstate == VIDEO_STOPPED)
			vid_blank(1);
		return 0;
	case VIDEO_GET_STATUS: {
		struct video_status s;

		s.video_blank = av.blank;
		s.play_state = av.vstate;
		s.stream_source = VIDEO_SOURCE_DEMUX;
		s.video_format = av.format;
		s.display_format = av.dformat;
		return copy_to_user((void *) arg, &s, sizeof(s)) ? -EFAULT : 0;
	}
	case VIDEO_GET_CAPABILITIES:
		return put_user(VIDEO_CAP_MPEG1 | VIDEO_CAP_MPEG2, (unsigned int *) arg);
	case VIDEO_CLEAR_BUFFER:
		vid_clear();
		return 0;
	case VIDEO_SET_FORMAT:
		if (arg > VIDEO_FORMAT_221_1)
			return -EINVAL;
		av.format = arg;
		return 0;
	case VIDEO_SET_DISPLAY_FORMAT:
		av.dformat = arg;
		return 0;
	case VIDEO_SET_STREAMTYPE:
	case VIDEO_SET_SYSTEM:
	case VIDEO_SET_ID:
		return 0;
	}
	return -ENOTTY;
}

static int audio_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		       unsigned long arg)
{
	switch (cmd) {
	case AUDIO_STOP:
		aud_stop();
		av.astate = AUDIO_STOPPED;
		return 0;
	case AUDIO_PLAY:
		if (av_mem())
			return -ENOMEM;
		if (av.astate == AUDIO_STOPPED)
			aud_start();
		else if (av.astate == AUDIO_PAUSED)
			aud_mute(av.mute);
		av.astate = AUDIO_PLAYING;
		return 0;
	case AUDIO_PAUSE:
		if (av.astate != AUDIO_PLAYING)
			return -EINVAL;
		aud_mute(1);
		av.astate = AUDIO_PAUSED;
		return 0;
	case AUDIO_CONTINUE:
		if (av.astate != AUDIO_PAUSED)
			return -EINVAL;
		aud_mute(av.mute);
		av.astate = AUDIO_PLAYING;
		return 0;
	case AUDIO_SELECT_SOURCE:
		return arg == AUDIO_SOURCE_DEMUX ? 0 : -EINVAL;
	case AUDIO_SET_MUTE:
		av.mute = arg != 0;
		aud_mute(av.mute);
		return 0;
	case AUDIO_SET_AV_SYNC:
		av.av_sync = arg != 0;
		return 0;
	case AUDIO_SET_BYPASS_MODE:
		av.bypass = arg != 0;
		return 0;
	case AUDIO_CHANNEL_SELECT:
		if (arg > AUDIO_MONO_RIGHT)
			return -EINVAL;
		av.channel = arg;
		aud_volume(av.mixer.volume_left, av.mixer.volume_right);
		return 0;
	case AUDIO_GET_STATUS: {
		audio_status_t s;

		s.AV_sync_state = av.av_sync;
		s.mute_state = av.mute;
		s.play_state = av.astate;
		s.stream_source = AUDIO_SOURCE_DEMUX;
		s.channel_select = av.channel;
		s.bypass_mode = av.bypass;
		s.mixer_state = av.mixer;
		return copy_to_user((void *) arg, &s, sizeof(s)) ? -EFAULT : 0;
	}
	case AUDIO_GET_CAPABILITIES:
		return put_user(AUDIO_CAP_MP1 | AUDIO_CAP_MP2, (unsigned int *) arg);
	case AUDIO_CLEAR_BUFFER:
		aud_clear();
		return 0;
	case AUDIO_SET_MIXER:
		if (copy_from_user(&av.mixer, (void *) arg, sizeof(av.mixer)))
			return -EFAULT;
		aud_volume(av.mixer.volume_left, av.mixer.volume_right);
		return 0;
	case AUDIO_SET_ID:
	case AUDIO_SET_STREAMTYPE:
		return 0;
	}
	return -ENOTTY;
}

static int video_open(struct inode *inode, struct file *file)
{
	av.vusers++;
	return 0;
}

static int video_release(struct inode *inode, struct file *file)
{
	av.vusers--;
	return 0;
}

static int audio_open(struct inode *inode, struct file *file)
{
	av.ausers++;
	return 0;
}

static int audio_release(struct inode *inode, struct file *file)
{
	av.ausers--;
	return 0;
}

static struct file_operations video_fops = {
	ioctl: video_ioctl,
	open: video_open,
	release: video_release,
};

static struct file_operations audio_fops = {
	ioctl: audio_ioctl,
	open: audio_open,
	release: audio_release,
};

static int av_proc(char *buf)
{
	return sprintf(buf, "video: state %d, %dx%d %s, %lu pictures, %lu vsyncs, %lu errors, "
		       "bit buffer %u x 256 bytes, shown %d; audio: state %d, mute %d, volume "
		       "%u/%u, sync %d; memory %08lx %08lx\n", av.vstate, vd.w, vd.h,
		       vd.mpeg2 ? "MPEG-2" : "MPEG-1", vd.pictures, vd.vsyncs, vd.errors,
		       (vr(0x16) & 0x3f) << 8 | vr(0x17), vd.shown, av.astate, av.mute,
		       av.mixer.volume_left, av.mixer.volume_right, vr(0x826) & 3,
		       av_vmem, av_amem);
}

static void av_init(void)
{
	av.mixer.volume_left = av.mixer.volume_right = 255;
	av.blank = 1;
	vd.oldref = vd.newref = vd.show = vd.shown = -1;
	vw(0x61, 0);
	vw(0x60, 0);
	vw(0x3c, 0);
	vd.irq = request_irq(VID_IRQ, av_interrupt, 0, "mpeg", &vd) == 0;
}

static void av_exit(void)
{
	vid_stop();
	aud_stop();
	if (vd.irq)
		free_irq(VID_IRQ, &vd);
	if (!st20_avmem_size) {
		if (av_vmem)
			free_pages(av_vmem, 9);
	}
}
