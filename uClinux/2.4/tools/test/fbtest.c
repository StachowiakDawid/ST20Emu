/*
 * fbtest [BPP [SECONDS]] - draws a test pattern on /dev/fb0 and keeps it shown
 * (the OSD shows the frame buffer while /dev/fb0 is open).  BPP 2, 4 or 8 (default 8):
 * colour bars of the first 16 palette entries (all entries at 2 bpp) in the top half, the
 * 8 bpp colour cube / grey ramp or a checkerboard below, a white frame and diagonal.
 * Uses mmap(); "-w" as first argument writes with lseek/write instead.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

struct bitfield { unsigned long offset, length, msb_right; };
struct var_info {
	unsigned long xres, yres, xres_virtual, yres_virtual, xoffset, yoffset;
	unsigned long bits_per_pixel, grayscale;
	struct bitfield red, green, blue, transp;
	unsigned long nonstd, activate, height, width, accel_flags;
	unsigned long pixclock, left_margin, right_margin, upper_margin, lower_margin;
	unsigned long hsync_len, vsync_len, sync, vmode, reserved[6];
};
struct fix_info {
	char id[16];
	unsigned long smem_start, smem_len, type, type_aux, visual;
	unsigned short xpanstep, ypanstep, ywrapstep;
	unsigned long line_length, mmio_start, mmio_len, accel;
	unsigned short reserved[3];
};
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602

static unsigned char *fb;
static int bpp, stride, use_write, fd;
static unsigned char line[768];

static void pset(unsigned char *l, int x, int c)
{
	int ppb = 8 / bpp, shift = (ppb - 1 - x % ppb) * bpp;
	unsigned char *p = l + x / ppb;

	*p = (*p & ~(((1 << bpp) - 1) << shift)) | (c << shift);
}

int main(int argc, char **argv)
{
	struct var_info var;
	struct fix_info fix;
	int x, y, secs = 60, a = 1, ncol;

	if (argc > a && !strcmp(argv[a], "-w")) {
		use_write = 1;
		a++;
	}
	bpp = argc > a ? atoi(argv[a]) : 8;
	if (argc > a + 1)
		secs = atoi(argv[a + 1]);
	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0) {
		perror("/dev/fb0");
		return 1;
	}
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
		perror("FBIOGET_VSCREENINFO");
		return 1;
	}
	var.bits_per_pixel = bpp;
	var.activate = 0;
	if (ioctl(fd, FBIOPUT_VSCREENINFO, &var) < 0)
		perror("FBIOPUT_VSCREENINFO");
	ioctl(fd, FBIOGET_VSCREENINFO, &var);
	ioctl(fd, FBIOGET_FSCREENINFO, &fix);
	bpp = var.bits_per_pixel;
	stride = fix.line_length;
	printf("fb0: %s %lux%lu %lu bpp, line %lu, smem %08lx+%lu, visual %lu\n", fix.id,
	       var.xres, var.yres, var.bits_per_pixel, fix.line_length, fix.smem_start,
	       fix.smem_len, fix.visual);
	if (!use_write) {
		fb = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (fb == (unsigned char *) -1) {
			perror("mmap");
			use_write = 1;
		} else
			printf("mmap: %p\n", fb);
	}
	ncol = bpp == 2 ? 4 : 16;
	for (y = 0; y < (int) var.yres; y++) {
		unsigned char *l = use_write ? line : fb + y * stride;

		for (x = 0; x < (int) var.xres; x++) {
			int c;

			if (y < 240)
				c = x * ncol / 640;
			else if (bpp == 8)
				c = y < 400 ? 16 + ((y - 240) / 27) * 36 + (x / 107) * 6 + (x / 18) % 6
					    : 232 + x * 24 / 640;
			else
				c = ((x / 32) ^ (y / 32)) & 1 ? (1 << bpp) - 1 : 0;
			if (x == 0 || y == 0 || x == 639 || y == 479 || x == y || x == 639 - y)
				c = bpp == 8 ? 15 : (1 << bpp) - 1;
			if (c > 255)
				c = 255;
			pset(l, x, c);
		}
		if (use_write) {
			lseek(fd, y * stride, SEEK_SET);
			write(fd, line, var.xres * bpp / 8);
		}
	}
	printf("pattern drawn, holding /dev/fb0 for %d s\n", secs);
	fflush(stdout);
	sleep(secs);
	close(fd);
	return 0;
}
