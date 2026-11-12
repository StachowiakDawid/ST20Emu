/*
 * dvb.c - DVB-S receiver of the Samsung DSR9500 (STi5512) for uClinux/ST20, one loadable
 * module (insmod /lib/modules/dvb.mod), Linux DVB API version 3 devices on char major 212:
 *
 *   /dev/dvb/adapter0/frontend0  (212,3)   HM1221 QPSK + tuner, LNB, DiSEqC   dvb/fe.c
 *   /dev/dvb/adapter0/demux0     (212,4)   section / PES filters on the PTI    dvb/dmx.c
 *   /dev/dvb/adapter0/dvr0       (212,5)   transport stream of the TS-tap filters
 *   /dev/dvb/adapter0/video0     (212,0)   MPEG video decoder                  dvb/av.c
 *   /dev/dvb/adapter0/audio0     (212,1)   MPEG audio decoder
 *
 * Minor numbers as linux-2.6 (adapter << 6 | type << 4 | index); /proc/dvb shows the
 * state.  The parts are #included: modules are single compilation units here
 * (modules/build.sh).
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/semaphore.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/video.h>
#include <linux/dvb/audio.h>

#define DVB_MAJOR	212
#define DVB_VIDEO	0
#define DVB_AUDIO	1
#define DVB_FRONTEND	3
#define DVB_DEMUX	4
#define DVB_DVR		5

#include "dvb/hw.h"
#include "dvb/fe.c"
#include "dvb/pti.c"
#include "dvb/dmx.c"
#include "dvb/av.c"

static struct file_operations *dvb_fops(int minor)
{
	switch (minor) {
	case DVB_FRONTEND: return &fe_fops;
	case DVB_DEMUX: return &dmx_fops;
	case DVB_DVR: return &dvr_fops;
	case DVB_VIDEO: return &video_fops;
	case DVB_AUDIO: return &audio_fops;
	}
	return 0;
}

static int dvb_open(struct inode *inode, struct file *file)
{
	struct file_operations *f = dvb_fops(MINOR(inode->i_rdev));

	if (!f)
		return -ENODEV;
	file->f_op = f;
	return f->open ? f->open(inode, file) : 0;
}

static struct file_operations dvb_dev_fops = {
	open: dvb_open,
};

static int dvb_read_proc(char *page, char **start, off_t off, int count, int *eof,
			 void *data)
{
	int n = fe_proc(page);

	n += pti_proc(page + n);
	n += dmx_proc(page + n);
	n += av_proc(page + n);
	*eof = 1;
	return n;
}

static int __init dvb_init(void)
{
	int r;

	if ((r = register_chrdev(DVB_MAJOR, "dvb", &dvb_dev_fops)) < 0)
		return r;
	if ((r = fe_init()))
		goto out;
	if ((r = pti_init())) {
		fe_exit();
		goto out;
	}
	dmx_init();
	av_init();
	create_proc_read_entry("dvb", 0, 0, dvb_read_proc, 0);
	printk(KERN_INFO "dvb: DSR9500 DVB-S (%s), PTI %s\n",
	       fe.present ? "HM1221" : "no demodulator", pti_tc_loaded ? "TC loaded" : "no TC code");
	return 0;
out:
	unregister_chrdev(DVB_MAJOR, "dvb");
	return r;
}

static void __exit dvb_exit(void)
{
	remove_proc_entry("dvb", 0);
	av_exit();
	dmx_exit();
	pti_exit();
	fe_exit();
	unregister_chrdev(DVB_MAJOR, "dvb");
}

module_init(dvb_init);
module_exit(dvb_exit);
