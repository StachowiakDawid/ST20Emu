/*
 * dmx.c - /dev/dvb/adapter0/demux0 and dvr0: DVB API v3 section and PES filters.
 *
 * Every PID in use is one raw PTI stream (pti.c); its packets are demultiplexed here in
 * software like linux-2.6's dvb_demux: sections are reassembled per PID and matched
 * against the 16-byte filter/mask/mode of each section filter (bytes 0, 3..17 of the
 * section), optionally CRC-checked; PES filters pass the TS payload (DMX_OUT_TAP) to
 * demux0 or whole packets (DMX_OUT_TS_TAP) to dvr0.  DMX_OUT_DECODER filters of type
 * video / audio / PCR program the PTI's decoder slots instead (no software path).
 */

#define DMX_DEF_BUF	8192
#define DVR_BUF		(188 * 128)

struct dmx_ring {
	u8 *data;
	int size, head, count;		/* head = read position */
	int error;			/* -EOVERFLOW / -ETIMEDOUT for the next read */
	wait_queue_head_t wq;
};

struct dmx_pidfeed;

struct dmx_flt {
	int state;			/* 0 none, 1 set, 2 started */
	int type;			/* 1 section, 2 PES */
	struct dmx_sct_filter_params sct;
	struct dmx_pes_filter_params pes;
	u8 pos[DMX_FILTER_SIZE], neg[DMX_FILTER_SIZE];	/* mask & ~mode, mask & mode */
	int doneq;
	struct dmx_ring ring;
	int bufsize;
	struct dmx_pidfeed *feed;	/* software feed, or 0 */
	int decoder;			/* PTI decoder slot programmed: 0 video, 1 audio, 2 PCR, -1 */
	struct dmx_flt *next;	/* filters on the same feed */
	struct timer_list timer;	/* section timeout */
	unsigned long sections;
};

struct dmx_pidfeed {
	int pid, slot, users;
	int cc, synced, have, seclen;
	struct dmx_flt *filters;
	struct dmx_pidfeed *next;
	u8 sec[4096 + 188];
};

static struct dmx_pidfeed *dmx_feeds;
static struct dmx_ring dvr_ring;
static int dvr_users, dmx_filters_open;
static u32 crc_tab[256];
static unsigned long dmx_sections, dmx_crc_errors;

static u32 dmx_crc32(const u8 *p, int n)
{
	u32 c = 0xffffffff;

	while (n--)
		c = (c << 8) ^ crc_tab[((c >> 24) ^ *p++) & 0xff];
	return c;
}

/* ---- rings ---- */

static int ring_alloc(struct dmx_ring *r, int size)
{
	u8 *d = kmalloc(size, GFP_KERNEL);

	if (!d)
		return -ENOMEM;
	if (r->data)
		kfree(r->data);
	r->data = d;
	r->size = size;
	r->head = r->count = r->error = 0;
	return 0;
}

/* in interrupt/tasklet context; all-or-nothing (a section is never split) */
static int ring_put(struct dmx_ring *r, const u8 *p, int n)
{
	int t, c;

	if (!r->data || r->count + n > r->size) {
		r->error = -EOVERFLOW;
		wake_up_interruptible(&r->wq);
		return -1;
	}
	t = (r->head + r->count) % r->size;
	c = n < r->size - t ? n : r->size - t;
	memcpy(r->data + t, p, c);
	memcpy(r->data, p + c, n - c);
	r->count += n;
	wake_up_interruptible(&r->wq);
	return 0;
}

static ssize_t ring_read(struct dmx_ring *r, struct file *file, char *buf, size_t count,
			 int started)
{
	unsigned long f;
	int n, c, done = 0;

	while (!r->count && !r->error) {
		if (!started)
			return 0;
		if (file->f_flags & O_NONBLOCK)
			return -EWOULDBLOCK;
		if (wait_event_interruptible(r->wq, r->count || r->error))
			return -ERESTARTSYS;
	}
	if (!r->count && r->error) {
		n = r->error;
		r->error = 0;
		return n;
	}
	while (done < count && r->count) {
		n = count - done < r->count ? count - done : r->count;
		c = n < r->size - r->head ? n : r->size - r->head;
		if (copy_to_user(buf + done, r->data + r->head, c))
			return -EFAULT;
		save_flags(f);
		cli();
		r->head = (r->head + c) % r->size;
		r->count -= c;
		restore_flags(f);
		done += c;
	}
	return done;
}

/* ---- software demux ---- */

static void dmx_filter_stop(struct dmx_flt *f);

static int sec_match(struct dmx_flt *f, const u8 *s, int len)
{
	int i, neq = 0;
	u8 x;

	for (i = 0; i < DMX_FILTER_SIZE; i++) {
		int k = i ? i + 2 : 0;

		x = (k < len ? s[k] : 0) ^ f->sct.filter.filter[i];
		if (f->pos[i] & x)
			return 0;
		neq |= f->neg[i] & x;
	}
	return !f->doneq || neq;
}

static void dmx_oneshot(unsigned long x)
{
	dmx_filter_stop((struct dmx_flt *) x);
}

static void sec_deliver(struct dmx_pidfeed *d)
{
	struct dmx_flt *f, *next;
	int len = d->seclen, crc_done = 0, crc_ok = 0;

	dmx_sections++;
	for (f = d->filters; f; f = next) {
		next = f->next;
		if (f->type != 1 || f->state != 2 || !sec_match(f, d->sec, len))
			continue;
		if (f->sct.flags & DMX_CHECK_CRC) {
			if (!crc_done) {
				crc_ok = dmx_crc32(d->sec, len) == 0;
				crc_done = 1;
				if (!crc_ok)
					dmx_crc_errors++;
			}
			if (!crc_ok)
				continue;
		}
		if (ring_put(&f->ring, d->sec, len))
			continue;
		f->sections++;
		if (f->sct.timeout)
			mod_timer(&f->timer, jiffies + (f->sct.timeout * HZ + 999) / 1000);
		if (f->sct.flags & DMX_ONESHOT) {
			/* stop later (from a timer): the feed list is being walked */
			f->state = 3;
			del_timer(&f->timer);
			f->timer.function = dmx_oneshot;
			mod_timer(&f->timer, jiffies);
		}
	}
}

static void sec_bytes(struct dmx_pidfeed *d, const u8 *b, int n)
{
	int need, c;

	while (n > 0) {
		if (!d->have && b[0] == 0xff) {		/* stuffing up to the next PUSI */
			d->synced = 0;
			return;
		}
		need = d->have < 3 ? 3 - d->have : d->seclen - d->have;
		c = need < n ? need : n;
		memcpy(d->sec + d->have, b, c);
		d->have += c;
		b += c;
		n -= c;
		if (d->have == 3) {
			d->seclen = 3 + ((d->sec[1] & 0x0f) << 8 | d->sec[2]);
			if (d->seclen > 4096) {
				d->synced = d->have = 0;
				return;
			}
		}
		if (d->have >= 3 && d->have == d->seclen) {
			sec_deliver(d);
			d->have = 0;
		}
	}
}

/* one transport packet of the feed's PID (from the PTI tasklet) */
static void dmx_ts(int pid, const u8 *p)
{
	struct dmx_pidfeed *d;
	struct dmx_flt *f;
	int off = 4, cc = p[3] & 15, afc = (p[3] >> 4) & 3, pusi = p[1] & 0x40, sections = 0;

	for (d = dmx_feeds; d && d->pid != pid; d = d->next)
		;
	if (!d)
		return;
	if (afc & 2)
		off += 1 + p[4];
	for (f = d->filters; f; f = f->next) {
		if (f->state != 2)
			continue;
		if (f->type == 1)
			sections = 1;
		else if (f->pes.output == DMX_OUT_TS_TAP) {
			if (dvr_users)
				ring_put(&dvr_ring, p, 188);
		} else if (f->pes.output == DMX_OUT_TAP && (afc & 1) && off < 188)
			ring_put(&f->ring, p + off, 188 - off);
	}
	if (!sections || !(afc & 1) || off >= 188 || (p[1] & 0x80))
		return;
	if (d->cc >= 0 && cc == d->cc)
		return;					/* duplicate packet */
	if (d->cc >= 0 && cc != ((d->cc + 1) & 15))
		d->synced = d->have = 0;		/* discontinuity */
	d->cc = cc;
	if (pusi) {
		int ptr = p[off++];

		if (off + ptr > 188)
			return;
		if (d->synced && d->have)
			sec_bytes(d, p + off, ptr);
		off += ptr;
		d->have = 0;
		d->synced = 1;
	}
	if (d->synced && off < 188)
		sec_bytes(d, p + off, 188 - off);
}

static struct dmx_pidfeed *feed_get(int pid)
{
	struct dmx_pidfeed *d;
	int s;

	for (d = dmx_feeds; d; d = d->next)
		if (d->pid == pid) {
			d->users++;
			return d;
		}
	if (!(d = kmalloc(sizeof(*d), GFP_KERNEL)))
		return 0;
	memset(d, 0, sizeof(*d) - sizeof(d->sec));
	d->pid = pid;
	d->cc = -1;
	d->users = 1;
	if ((s = pti_raw_open(pid, dmx_ts)) < 0) {
		kfree(d);
		return 0;
	}
	d->slot = s;
	cli();
	d->next = dmx_feeds;
	dmx_feeds = d;
	sti();
	return d;
}

static void feed_put(struct dmx_pidfeed *d)
{
	struct dmx_pidfeed **pp;

	if (--d->users)
		return;
	pti_raw_close(d->slot);
	cli();
	for (pp = &dmx_feeds; *pp; pp = &(*pp)->next)
		if (*pp == d) {
			*pp = d->next;
			break;
		}
	sti();
	kfree(d);
}

static void dmx_timeout(unsigned long x)
{
	struct dmx_flt *f = (struct dmx_flt *) x;

	if (f->state == 2) {
		f->ring.error = -ETIMEDOUT;
		wake_up_interruptible(&f->ring.wq);
	}
}

static void dmx_filter_stop(struct dmx_flt *f)
{
	struct dmx_flt **pp;

	if (f->state < 2)
		return;
	f->state = 1;
	del_timer(&f->timer);
	if (f->decoder >= 0) {
		if (f->decoder < 2)
			pti_av(f->decoder, -1);
		else
			pti_set_pcr(-1);
		f->decoder = -1;
	}
	if (f->feed) {
		cli();
		for (pp = &f->feed->filters; *pp; pp = &(*pp)->next)
			if (*pp == f) {
				*pp = f->next;
				break;
			}
		sti();
		feed_put(f->feed);
		f->feed = 0;
	}
	wake_up_interruptible(&f->ring.wq);
}

static int dmx_filter_start(struct dmx_flt *f)
{
	int pid;

	if (f->state == 0)
		return -EINVAL;
	if (f->state >= 2)
		dmx_filter_stop(f);
	f->ring.head = f->ring.count = f->ring.error = 0;
	if (f->type == 2 && f->pes.output == DMX_OUT_DECODER) {
		int t = f->pes.pes_type;

		pid = f->pes.pid;
		f->decoder = t == DMX_PES_VIDEO ? 0 : t == DMX_PES_AUDIO ? 1 :
			t == DMX_PES_PCR ? 2 : -1;
		if (f->decoder < 0)
			return -EINVAL;
		if (f->decoder < 2)
			pti_av(f->decoder, pid);
		else
			pti_set_pcr(pid);
		f->state = 2;
		return 0;
	}
	pid = f->type == 1 ? f->sct.pid : f->pes.pid;
	if (pid > 0x1fff)
		return -EINVAL;
	if (!(f->feed = feed_get(pid)))
		return -EBUSY;
	init_timer(&f->timer);
	f->timer.data = (unsigned long) f;
	f->timer.function = dmx_timeout;
	cli();
	f->next = f->feed->filters;
	f->feed->filters = f;
	f->state = 2;
	sti();
	if (f->type == 1 && f->sct.timeout)
		mod_timer(&f->timer, jiffies + (f->sct.timeout * HZ + 999) / 1000);
	return 0;
}

/* ---- demux0 ---- */

static int dmx_open(struct inode *inode, struct file *file)
{
	struct dmx_flt *f = kmalloc(sizeof(*f), GFP_KERNEL);

	if (!f)
		return -ENOMEM;
	memset(f, 0, sizeof(*f));
	init_waitqueue_head(&f->ring.wq);
	init_timer(&f->timer);
	f->bufsize = DMX_DEF_BUF;
	f->decoder = -1;
	file->private_data = f;
	dmx_filters_open++;
	return 0;
}

static int dmx_release(struct inode *inode, struct file *file)
{
	struct dmx_flt *f = file->private_data;

	dmx_filter_stop(f);
	if (f->ring.data)
		kfree(f->ring.data);
	kfree(f);
	dmx_filters_open--;
	return 0;
}

static ssize_t dmx_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct dmx_flt *f = file->private_data;

	if (!f->ring.data)
		return 0;
	return ring_read(&f->ring, file, buf, count, f->state >= 2);
}

static unsigned int dmx_poll(struct file *file, poll_table *wait)
{
	struct dmx_flt *f = file->private_data;
	unsigned int m = 0;

	poll_wait(file, &f->ring.wq, wait);
	if (f->ring.count)
		m |= POLLIN | POLLRDNORM | POLLPRI;
	if (f->ring.error)
		m |= POLLERR;
	return m;
}

static int dmx_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg)
{
	struct dmx_flt *f = file->private_data;
	int i, r;

	switch (cmd) {
	case DMX_SET_FILTER:
		dmx_filter_stop(f);
		if (copy_from_user(&f->sct, (void *) arg, sizeof(f->sct)))
			return -EFAULT;
		f->type = 1;
		f->doneq = 0;
		for (i = 0; i < DMX_FILTER_SIZE; i++) {
			f->sct.filter.filter[i] &= f->sct.filter.mask[i];
			f->pos[i] = f->sct.filter.mask[i] & ~f->sct.filter.mode[i];
			f->neg[i] = f->sct.filter.mask[i] & f->sct.filter.mode[i];
			f->doneq |= f->neg[i];
		}
		goto set;
	case DMX_SET_PES_FILTER:
		dmx_filter_stop(f);
		if (copy_from_user(&f->pes, (void *) arg, sizeof(f->pes)))
			return -EFAULT;
		if (f->pes.input != DMX_IN_FRONTEND)
			return -EINVAL;
		f->type = 2;
		f->sct.flags = f->pes.flags;
	set:
		if (!f->ring.data || f->ring.size != f->bufsize)
			if ((r = ring_alloc(&f->ring, f->bufsize)))
				return r;
		f->state = 1;
		if (f->sct.flags & DMX_IMMEDIATE_START)
			return dmx_filter_start(f);
		return 0;
	case DMX_START:
		return dmx_filter_start(f);
	case DMX_STOP:
		dmx_filter_stop(f);
		return 0;
	case DMX_SET_BUFFER_SIZE:
		if (arg < 1024 || arg > 65536)
			return -EINVAL;
		f->bufsize = arg;
		if (f->state >= 2)
			return -EBUSY;
		return f->ring.data ? ring_alloc(&f->ring, arg) : 0;
	case DMX_GET_PES_PIDS: {
		u16 p[5];

		p[DMX_PES_AUDIO] = pti_av_pid[1];
		p[DMX_PES_VIDEO] = pti_av_pid[0];
		p[DMX_PES_TELETEXT] = 0xffff;
		p[DMX_PES_SUBTITLE] = 0xffff;
		p[DMX_PES_PCR] = pti_pcr_pid;
		return copy_to_user((void *) arg, p, sizeof(p)) ? -EFAULT : 0;
	}
	case DMX_GET_CAPS: {
		dmx_caps_t c;

		c.caps = 0;
		c.num_decoders = 2;
		return copy_to_user((void *) arg, &c, sizeof(c)) ? -EFAULT : 0;
	}
	case DMX_SET_SOURCE:
		return 0;
	case DMX_GET_STC: {
		struct dmx_stc s;

		if (copy_from_user(&s, (void *) arg, sizeof(s)))
			return -EFAULT;
		if (s.num)
			return -EINVAL;
		/* the last PCR's 33-bit base (90 kHz) */
		s.base = 1;
		s.stc = pti_get_pcr_base();
		return copy_to_user((void *) arg, &s, sizeof(s)) ? -EFAULT : 0;
	}
	}
	return -ENOTTY;
}

static struct file_operations dmx_fops = {
	read: dmx_read,
	poll: dmx_poll,
	ioctl: dmx_ioctl,
	open: dmx_open,
	release: dmx_release,
};

/* ---- dvr0 (read only: the TS-tap filters' packets) ---- */

static int dvr_open(struct inode *inode, struct file *file)
{
	int r;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY)
		return -EINVAL;
	if (!dvr_users && (r = ring_alloc(&dvr_ring, DVR_BUF)))
		return r;
	dvr_users++;
	return 0;
}

static int dvr_release(struct inode *inode, struct file *file)
{
	if (!--dvr_users) {
		kfree(dvr_ring.data);
		dvr_ring.data = 0;
	}
	return 0;
}

static ssize_t dvr_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	return ring_read(&dvr_ring, file, buf, count, 1);
}

static unsigned int dvr_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &dvr_ring.wq, wait);
	return (dvr_ring.count ? POLLIN | POLLRDNORM : 0) | (dvr_ring.error ? POLLERR : 0);
}

static struct file_operations dvr_fops = {
	read: dvr_read,
	poll: dvr_poll,
	open: dvr_open,
	release: dvr_release,
};

static int dmx_proc(char *buf)
{
	struct dmx_pidfeed *d;
	struct dmx_flt *f;
	int n = sprintf(buf, "demux: %d filters open, %lu sections, %lu CRC errors, dvr %d "
			"readers\n", dmx_filters_open, dmx_sections, dmx_crc_errors, dvr_users);

	for (d = dmx_feeds; d; d = d->next)
		for (f = d->filters; f; f = f->next)
			n += sprintf(buf + n, "  pid 0x%04x %s: %lu sections, %d bytes queued\n",
				     d->pid, f->type == 1 ? "section" : "pes", f->sections,
				     f->ring.count);
	return n;
}

static void dmx_init(void)
{
	int i, j;
	u32 c;

	for (i = 0; i < 256; i++) {
		c = (u32) i << 24;
		for (j = 0; j < 8; j++)
			c = c & 0x80000000 ? (c << 1) ^ 0x04c11db7 : c << 1;
		crc_tab[i] = c;
	}
	init_waitqueue_head(&dvr_ring.wq);
}

static void dmx_exit(void)
{
}
