#!/usr/bin/env python3
"""slip_host.py: the host end of a SLIP link to st20emu (--serial-tcp 3:PORT), with a tiny
user-space IPv4 stack (no root, no TUN, no slattach on macOS):

  * answers ICMP echo requests from the target (the target's ping),
  * pings the target,
  * opens a TCP connection to the target's echo server, sends a message, checks the echo,
    closes (FIN both ways),
  * optionally fetches `GET /` from an HTTP server on the target (--http PORT).

  slip_host.py [--port 5555] [--host-ip 10.0.0.1] [--target-ip 10.0.0.2] [--echo 7]
               [--timeout 600]
Exit status 0 when every step worked.  Waits for the emulator's TCP port, then for the
target's first IP packet (it pings the host when its SLIP interface comes up).
"""
import argparse
import random
import socket
import struct
import sys
import time

END, ESC, ESC_END, ESC_ESC = 0xc0, 0xdb, 0xdc, 0xdd


def csum(data):
    if len(data) & 1:
        data += b'\0'
    s = sum(struct.unpack(f'!{len(data) // 2}H', data))
    while s >> 16:
        s = (s & 0xffff) + (s >> 16)
    return ~s & 0xffff


def ip4(a):
    return socket.inet_aton(a)


class Link:
    def __init__(self, sock, log):
        self.sock, self.log = sock, log
        self.buf, self.frame, self.esc = [], bytearray(), False
        self.stats = {'rx': 0, 'tx': 0, 'junk': 0}
        self.log_raw = None
        self.verbose = False

    def send_ip(self, pkt):
        out = bytearray([END])
        for b in pkt:
            if b == END:
                out += bytes([ESC, ESC_END])
            elif b == ESC:
                out += bytes([ESC, ESC_ESC])
            else:
                out.append(b)
        out.append(END)
        if self.verbose:
            self.log(f'tx {bytes(pkt).hex()}')
        self.sock.sendall(out)
        self.stats['tx'] += 1

    def recv_ip(self, timeout):
        """next valid IPv4 packet (bytes) or None on timeout"""
        deadline = time.time() + timeout
        while True:
            while self.buf:
                b = self.buf.pop(0)
                if b == END:
                    f, self.frame = bytes(self.frame), bytearray()
                    if len(f) >= 20 and f[0] >> 4 == 4 and csum(f[:(f[0] & 15) * 4]) == 0:
                        self.stats['rx'] += 1
                        if self.verbose:
                            self.log(f'rx {f.hex()}')
                        return f
                    if f:
                        self.stats['junk'] += 1
                elif self.esc:
                    self.frame.append(END if b == ESC_END else ESC if b == ESC_ESC else b)
                    self.esc = False
                elif b == ESC:
                    self.esc = True
                else:
                    self.frame.append(b)
            left = deadline - time.time()
            if left <= 0:
                return None
            self.sock.settimeout(left)
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                return None
            if not data:
                raise SystemExit('emulator closed the serial line')
            self.buf.extend(data)
            if self.log_raw:
                self.log_raw.write(data); self.log_raw.flush()


class Host:
    def __init__(self, link, me, peer):
        self.link, self.me, self.peer = link, ip4(me), ip4(peer)
        self.ipid = 1
        self.pinged = 0  # echo requests from the target answered

    def ip(self, proto, payload):
        self.ipid += 1
        h = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(payload), self.ipid, 0, 64, proto, 0,
                        self.me, self.peer)
        h = h[:10] + struct.pack('!H', csum(h)) + h[12:]
        self.link.send_ip(h + payload)

    def handle_icmp(self, pkt):
        """answers an echo request; returns the ICMP message otherwise"""
        hl = (pkt[0] & 15) * 4
        icmp = pkt[hl:]
        if icmp[0] == 8:
            rep = b'\0\0\0\0' + icmp[4:]
            rep = rep[:2] + struct.pack('!H', csum(rep)) + rep[4:]
            self.ip(1, rep)
            self.pinged += 1
            return None
        return icmp

    def wait(self, want, timeout):
        """dispatches packets until want(pkt) returns a value"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            pkt = self.link.recv_ip(deadline - time.time())
            if pkt is None:
                break
            if pkt[9] == 1 and self.handle_icmp(pkt) is None:
                continue
            r = want(pkt)
            if r is not None:
                return r
        return None

    def ping(self, seq, timeout=20):
        body = struct.pack('!HH', 0x4242, seq) + b'host ping over SLIP'
        msg = b'\x08\0\0\0' + body
        msg = msg[:2] + struct.pack('!H', csum(msg)) + msg[4:]
        t0 = time.time()
        self.ip(1, msg)

        def reply(pkt):
            hl = (pkt[0] & 15) * 4
            if pkt[9] == 1 and pkt[hl] == 0 and pkt[hl + 4:hl + 8] == body[:4]:
                return time.time() - t0
        return self.wait(reply, timeout)

    # --- TCP client -------------------------------------------------------------------
    def tcp(self, sport, dport, seq, ack, flags, data=b''):
        h = struct.pack('!HHIIBBHHH', sport, dport, seq, ack, 5 << 4, flags, 4096, 0, 0)
        pseudo = self.me + self.peer + struct.pack('!BBH', 0, 6, len(h) + len(data))
        c = csum(pseudo + h + data)
        self.ip(6, h[:16] + struct.pack('!H', c) + h[18:] + data)

    def tcp_session(self, dport, message, log, request=False):
        sport = random.randint(20000, 60000)
        iss = random.randint(0, 1 << 31)
        SYN, ACK, FIN, PSH, RST = 2, 16, 1, 8, 4

        def seg(pkt):
            hl = (pkt[0] & 15) * 4
            if pkt[9] != 6:
                return None
            t = pkt[hl:]
            sp, dp, s, a, off, fl = struct.unpack('!HHIIBB', t[:14])
            if sp != dport or dp != sport:
                return None
            return s, a, fl, t[(off >> 4) * 4:]

        r = None
        for attempt in range(5):
            self.tcp(sport, dport, iss, 0, SYN)
            r = self.wait(seg, 15)
            if r:
                break
        if not r or r[2] & (SYN | ACK) != SYN | ACK:
            log(f'tcp: no SYN-ACK ({r})')
            return None
        rcv = (r[0] + 1) & 0xffffffff
        snd = (iss + 1) & 0xffffffff
        log(f'tcp: connected {socket.inet_ntoa(self.peer)}:{dport} from port {sport}')
        self.tcp(sport, dport, snd, rcv, ACK)
        self.tcp(sport, dport, snd, rcv, ACK | PSH, message)
        sent_until = snd + len(message)
        got = b''
        fin = False
        deadline = time.time() + 120
        while time.time() < deadline and not fin:
            r = self.wait(seg, 10)
            if r is None:  # retransmit what is unacknowledged
                if not got and not request:
                    self.tcp(sport, dport, snd, rcv, ACK | PSH, message)
                continue
            s, a, fl, data = r
            if fl & RST:
                log('tcp: reset by target')
                return None
            if data and s == rcv:
                got += data
                rcv = (rcv + len(data)) & 0xffffffff
            if fl & FIN and (s + len(data)) & 0xffffffff == rcv:
                rcv = (rcv + 1) & 0xffffffff
                fin = True
            self.tcp(sport, dport, sent_until & 0xffffffff, rcv, ACK)
            if not request and len(got) >= len(message):
                break
        # close: our FIN, wait for the target's FIN (echo server closes after EOF)
        self.tcp(sport, dport, sent_until & 0xffffffff, rcv, ACK | FIN)
        fin_acked = False
        deadline = time.time() + 60
        while time.time() < deadline and not (fin and fin_acked):
            r = self.wait(seg, 10)
            if r is None:
                self.tcp(sport, dport, sent_until & 0xffffffff, rcv, ACK | FIN)
                continue
            s, a, fl, data = r
            if a == (sent_until + 1) & 0xffffffff:
                fin_acked = True
            if data and s == rcv:
                got += data
                rcv = (rcv + len(data)) & 0xffffffff
            if fl & FIN and not fin:
                rcv = (rcv + 1) & 0xffffffff
                fin = True
            self.tcp(sport, dport, (sent_until + 1) & 0xffffffff, rcv, ACK)
        log(f'tcp: closed (target FIN {fin}, our FIN acknowledged {fin_acked})')
        return got if fin and fin_acked else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=5555)
    ap.add_argument('--host-ip', default='10.0.0.1')
    ap.add_argument('--target-ip', default='10.0.0.2')
    ap.add_argument('--echo', type=int, default=7)
    ap.add_argument('--http', type=int, default=0)
    ap.add_argument('--timeout', type=float, default=600)
    ap.add_argument('-v', action='store_true', help='log every packet')
    ap.add_argument('--raw', help='also write every byte received to this file')
    a = ap.parse_args()
    t0 = time.time()

    def log(msg):
        print(f'[slip_host {time.time() - t0:6.1f}s] {msg}', flush=True)

    sock = None
    while time.time() - t0 < a.timeout:
        try:
            sock = socket.create_connection(('127.0.0.1', a.port))
            break
        except OSError:
            time.sleep(0.5)
    if not sock:
        raise SystemExit('cannot connect to the emulator')
    log(f'connected to st20emu serial line on port {a.port}')
    link = Link(sock, log)
    link.verbose = a.v
    if a.raw:
        link.log_raw = open(a.raw, 'wb')
    host = Host(link, a.host_ip, a.target_ip)
    ok = True
    # the target pings us when its interface is up
    while host.pinged == 0 and time.time() - t0 < a.timeout:
        pkt = link.recv_ip(5)
        if pkt is not None and pkt[9] == 1:
            host.handle_icmp(pkt)
    if host.pinged == 0:
        log('no packet from the target')
        return 1
    log(f'target is up: answered {host.pinged} echo request(s) from {a.target_ip}')
    for seq in range(3):
        rtt = host.ping(seq)
        log(f'ping {a.target_ip} seq {seq}: ' + (f'reply in {rtt * 1000:.0f} ms' if rtt is not None else 'TIMEOUT'))
        ok &= rtt is not None
    msg = b'Hello ST20 over SLIP! ' + bytes(range(0xb0, 0xe0)) + b' (incl. SLIP END/ESC bytes)\n'
    got = host.tcp_session(a.echo, msg, log)
    good = got == msg
    log(f'tcp echo port {a.echo}: ' + ('ok, ' + repr(got[:22]) + '...' if good else f'FAILED: {got!r}'))
    ok &= good
    if a.http:
        got = host.tcp_session(a.http, b'GET / HTTP/1.0\r\n\r\n', log, request=True)
        log(f'http port {a.http}: ' + (repr(got[:200]) if got else 'FAILED'))
        ok &= bool(got)
    log(f'SLIP frames: {link.stats["rx"]} IP packets received, {link.stats["tx"]} sent, '
        f'{link.stats["junk"]} non-IP frames ignored (console text before SLIP)')
    log('RESULT: ' + ('all ok' if ok else 'FAILED'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
