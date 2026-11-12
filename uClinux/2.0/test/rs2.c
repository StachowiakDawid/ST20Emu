struct a { unsigned short x; }; struct b { unsigned long y; unsigned short z; };
void g(struct b *p, struct a *q) { p->y = q->x; p->z = q->x; }
int h(unsigned short *p) { return *p == 0x137f; }
void k(unsigned short *d, unsigned short *s) { *d = *s; }
void m(unsigned short *d, int v) { *d = v; }
