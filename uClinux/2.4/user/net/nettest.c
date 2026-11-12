/*
 * nettest: network test tool for the uClinux 2.4 ST20 port (uClibc, socketcall ABI).
 *
 *   nettest lo                         loopback self test: UNIX socketpair, UDP, TCP, ICMP ping
 *   nettest ping ADDR [COUNT]          ICMP echo through a raw socket
 *   nettest ifconfig IF ADDR [PEER]    set address (and point-to-point peer), bring IF up
 *   nettest route add GW               default route via GW
 *   nettest echod PORT                 TCP echo server (one client at a time, forever)
 *   nettest slip TTY ADDR PEER PORT    SLIP on TTY (N_SLIP line discipline, slattach), configure
 *                                      the interface, ping PEER until it answers, then echod PORT
 *
 * The SLIP mode does everything in one process because ttyS0 is also the console: once
 * the line discipline is N_SLIP nothing more can be typed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/route.h>

#ifndef SIOCADDRT
#define SIOCADDRT	0x890B
#endif
#ifndef SIOCGIFFLAGS
#define SIOCGIFFLAGS	0x8913
#define SIOCSIFFLAGS	0x8914
#define SIOCSIFADDR	0x8916
#define SIOCSIFDSTADDR	0x8918
#define SIOCSIFNETMASK	0x891c
#endif
#ifndef SIOCGIFNAME
#define SIOCGIFNAME	0x8910
#endif
#define TIOCSETD_	0x5423
#define N_SLIP_		1

static int fails;
static int ifconfig(const char *ifname, const char *addr, const char *peer);

static void check(const char *what, int ok)
{
	printf("%s: %s\n", what, ok ? "ok" : "FAILED");
	if (!ok)
		fails++;
}

static unsigned short cksum(const void *p, int n)
{
	const unsigned char *b = p;
	unsigned long s = 0;
	int i;
	for (i = 0; i + 1 < n; i += 2)
		s += b[i] << 8 | b[i + 1];
	if (n & 1)
		s += b[n - 1] << 8;
	while (s >> 16)
		s = (s & 0xffff) + (s >> 16);
	return ~s & 0xffff;
}

static void setaddr(struct sockaddr *sa, const char *addr)
{
	struct sockaddr_in *in = (struct sockaddr_in *) sa;
	memset(in, 0, sizeof(*in));
	in->sin_family = AF_INET;
	in->sin_addr.s_addr = inet_addr(addr);
}

static long now_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, 0);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* one ICMP echo; returns 1 when the reply came within timeout_ms */
static int ping1(int s, const char *addr, int seq, int timeout_ms)
{
	unsigned char pkt[64], buf[256];
	struct sockaddr_in to, from;
	int fromlen, n, hl;
	long t0 = now_ms();

	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = inet_addr(addr);
	memset(pkt, 0, sizeof(pkt));
	pkt[0] = 8;			/* echo request */
	pkt[4] = 0x12; pkt[5] = 0x34;	/* id */
	pkt[6] = seq >> 8; pkt[7] = seq;
	strcpy((char *) pkt + 8, "st20 ping");
	n = cksum(pkt, sizeof(pkt));
	pkt[2] = n >> 8; pkt[3] = n;
	if (sendto(s, pkt, sizeof(pkt), 0, (struct sockaddr *) &to, sizeof(to)) < 0) {
		perror("sendto");
		return 0;
	}
	for (;;) {
		fd_set r;
		struct timeval tv;
		long left = timeout_ms - (now_ms() - t0);
		if (left <= 0)
			return 0;
		FD_ZERO(&r);
		FD_SET(s, &r);
		tv.tv_sec = left / 1000;
		tv.tv_usec = left % 1000 * 1000;
		if (select(s + 1, &r, 0, 0, &tv) <= 0)
			return 0;
		fromlen = sizeof(from);
		n = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *) &from, &fromlen);
		if (n < 28)
			continue;
		hl = (buf[0] & 15) * 4;
		if (buf[hl] == 0 && buf[hl + 4] == 0x12 && buf[hl + 5] == 0x34 &&
		    buf[hl + 7] == (seq & 255)) {
			printf("%d bytes from %s: icmp_seq=%d time=%ld ms\n", n - hl,
			       inet_ntoa(from.sin_addr), seq, now_ms() - t0);
			return 1;
		}
	}
}

static int do_ping(const char *addr, int count)
{
	int s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP), i, got = 0;
	if (s < 0) {
		perror("socket(raw)");
		return 0;
	}
	for (i = 0; i < count; i++)
		got += ping1(s, addr, i, 2000);
	close(s);
	printf("%s: %d/%d replies\n", addr, got, count);
	return got;
}

static int test_lo(void)
{
	int sv[2], s, c, a, n, len;
	char buf[100];
	struct sockaddr_in sin, peer;

	check("ifconfig lo 127.0.0.1", ifconfig("lo", "127.0.0.1", 0) == 0);

	/* UNIX domain */
	check("unix socketpair", socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	write(sv[0], "hello unix", 10);
	n = read(sv[1], buf, sizeof(buf));
	check("unix stream", n == 10 && memcmp(buf, "hello unix", 10) == 0);
	close(sv[0]); close(sv[1]);

	/* UDP */
	s = socket(AF_INET, SOCK_DGRAM, 0);
	c = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(7777);
	sin.sin_addr.s_addr = inet_addr("127.0.0.1");
	check("udp bind", bind(s, (struct sockaddr *) &sin, sizeof(sin)) == 0);
	check("udp sendto", sendto(c, "hello udp", 9, 0, (struct sockaddr *) &sin, sizeof(sin)) == 9);
	len = sizeof(peer);
	n = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *) &peer, &len);
	check("udp recvfrom", n == 9 && memcmp(buf, "hello udp", 9) == 0);
	close(s); close(c);

	/* TCP: the kernel completes the handshake from the listen backlog */
	s = socket(AF_INET, SOCK_STREAM, 0);
	sin.sin_port = htons(8888);
	n = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));
	check("tcp bind", bind(s, (struct sockaddr *) &sin, sizeof(sin)) == 0);
	check("tcp listen", listen(s, 2) == 0);
	c = socket(AF_INET, SOCK_STREAM, 0);
	check("tcp connect", connect(c, (struct sockaddr *) &sin, sizeof(sin)) == 0);
	len = sizeof(peer);
	a = accept(s, (struct sockaddr *) &peer, &len);
	check("tcp accept", a >= 0);
	printf("  peer %s:%d\n", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
	check("tcp send", write(c, "hello tcp", 9) == 9);
	n = read(a, buf, sizeof(buf));
	check("tcp recv", n == 9 && memcmp(buf, "hello tcp", 9) == 0);
	{
		static char big[20000], back[20000];
		int i, done = 0;
		for (i = 0; i < (int) sizeof(big); i++)
			big[i] = i * 7;
		/* 20000 bytes through the socket buffers in pieces */
		while (done < (int) sizeof(big)) {
			int w = write(c, big + done, sizeof(big) - done > 4000 ? 4000 : sizeof(big) - done);
			int r, got = 0;
			if (w <= 0)
				break;
			while (got < w && (r = read(a, back + done + got, w - got)) > 0)
				got += r;
			done += w;
		}
		check("tcp 20000 bytes", done == sizeof(big) && memcmp(big, back, sizeof(big)) == 0);
	}
	close(c);
	n = read(a, buf, sizeof(buf));
	check("tcp eof", n == 0);
	close(a); close(s);

	/* ICMP */
	check("ping 127.0.0.1", do_ping("127.0.0.1", 2) == 2);
	printf("loopback: %s\n", fails ? "FAILED" : "all ok");
	return fails != 0;
}

static int ifconfig(const char *ifname, const char *addr, const char *peer)
{
	struct ifreq ifr;
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	setaddr(&ifr.ifr_addr, addr);
	if (ioctl(s, SIOCSIFADDR, &ifr) < 0) {
		perror("SIOCSIFADDR");
		return 1;
	}
	if (peer) {
		setaddr(&ifr.ifr_addr, peer);
		if (ioctl(s, SIOCSIFDSTADDR, &ifr) < 0) {
			perror("SIOCSIFDSTADDR");
			return 1;
		}
		setaddr(&ifr.ifr_addr, "255.255.255.255");
		ioctl(s, SIOCSIFNETMASK, &ifr);
	}
	if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) {
		perror("SIOCGIFFLAGS");
		return 1;
	}
	ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
	if (ioctl(s, SIOCSIFFLAGS, &ifr) < 0) {
		perror("SIOCSIFFLAGS");
		return 1;
	}
	close(s);
	printf("%s: %s%s%s up\n", ifname, addr, peer ? " peer " : "", peer ? peer : "");
	return 0;
}

static int route_default(const char *gw)
{
	struct rtentry rt;
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&rt, 0, sizeof(rt));
	setaddr(&rt.rt_dst, "0.0.0.0");
	setaddr(&rt.rt_genmask, "0.0.0.0");
	setaddr(&rt.rt_gateway, gw);
	rt.rt_flags = RTF_UP | RTF_GATEWAY;
	if (ioctl(s, SIOCADDRT, &rt) < 0) {
		perror("SIOCADDRT");
		return 1;
	}
	close(s);
	printf("default via %s\n", gw);
	return 0;
}

static int echod(int port, int verbose)
{
	struct sockaddr_in sin, peer;
	int s = socket(AF_INET, SOCK_STREAM, 0), a, n, len, one = 1;
	char buf[512];
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(s, (struct sockaddr *) &sin, sizeof(sin)) < 0 || listen(s, 4) < 0) {
		perror("echod");
		return 1;
	}
	if (verbose)
		printf("echod: listening on %d\n", port);
	for (;;) {
		len = sizeof(peer);
		a = accept(s, (struct sockaddr *) &peer, &len);
		if (a < 0)
			continue;
		if (verbose)
			printf("echod: %s:%d\n", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
		while ((n = read(a, buf, sizeof(buf))) > 0)
			write(a, buf, n);
		close(a);
	}
}

static int slip(const char *tty, const char *addr, const char *peer, int port)
{
	struct termios t;
	int fd = open(tty, O_RDWR), disc = N_SLIP_, i;
	char ifname[IFNAMSIZ + 1];
	long syscall_nr = 103;	/* syslog(8, 0, 1): console log level 1 (no printk on the line) */
	typedef long (*vec_t)(long, long, long, long, long, long);
	(*(vec_t *) 0xc0000000)(syscall_nr, 8, 0, 1, 0, 0);

	if (fd < 0) {
		perror(tty);
		return 1;
	}
	tcgetattr(fd, &t);
	t.c_iflag = 0;
	t.c_oflag = 0;
	t.c_lflag = 0;
	t.c_cflag = (t.c_cflag & ~(CSIZE | PARENB)) | CS8 | CREAD | CLOCAL;
	tcsetattr(fd, TCSANOW, &t);
	if (ioctl(fd, TIOCSETD_, &disc) < 0) {
		perror("TIOCSETD");
		return 1;
	}
	memset(ifname, 0, sizeof(ifname));
	if (ioctl(fd, SIOCGIFNAME, ifname) < 0)
		strcpy(ifname, "sl0");
	if (ifconfig(ifname, addr, peer))
		return 1;
	/* until the far end answers (it may connect later) */
	{
		int s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
		for (i = 0; i < 1000; i++)
			if (ping1(s, peer, i, 1000))
				break;
		close(s);
	}
	return echod(port, 0);
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "lo"))
		return test_lo();
	if (argc >= 3 && !strcmp(argv[1], "ping"))
		return do_ping(argv[2], argc > 3 ? atoi(argv[3]) : 3) ? 0 : 1;
	if (argc >= 4 && !strcmp(argv[1], "ifconfig"))
		return ifconfig(argv[2], argv[3], argc > 4 ? argv[4] : 0);
	if (argc >= 4 && !strcmp(argv[1], "route") && !strcmp(argv[2], "add"))
		return route_default(argv[3]);
	if (argc >= 3 && !strcmp(argv[1], "echod"))
		return echod(atoi(argv[2]), 1);
	if (argc >= 6 && !strcmp(argv[1], "slip"))
		return slip(argv[2], argv[3], argv[4], atoi(argv[5]));
	fprintf(stderr, "usage: nettest lo | ping ADDR [N] | ifconfig IF ADDR [PEER] | "
		"route add GW | echod PORT | slip TTY ADDR PEER PORT\n");
	return 2;
}
