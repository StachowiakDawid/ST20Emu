struct ms { unsigned short ninodes, nzones, imap, zmap, first, log; unsigned long max; unsigned short magic, state; unsigned long zones; };
struct sb { unsigned long ninodes, nzones, imap, zmap, first, log, max; unsigned short state; unsigned long magic; int dirsize, namelen; };
int f(struct sb *s, struct ms *ms)
{
	s->state = ms->state;
	s->ninodes = ms->ninodes;
	s->imap = ms->imap;
	s->zmap = ms->zmap;
	s->first = ms->first;
	s->log = ms->log;
	s->max = ms->max;
	s->magic = ms->magic;
	if (s->magic == 0x137f) { s->nzones = ms->nzones; s->dirsize = 16; s->namelen = 14; }
	else if (s->magic == 0x138f) { s->nzones = ms->nzones; s->dirsize = 32; s->namelen = 30; }
	else if (s->magic == 0x2468) { s->nzones = ms->zones; s->dirsize = 16; s->namelen = 14; }
	else return 1;
	return 0;
}
