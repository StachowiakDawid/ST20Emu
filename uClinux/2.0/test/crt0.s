// minimal bare-metal start: segments text, data, bss back to back (created in that
// order, see README), Wptr = 0xc0800000, call _start
.text
.data
.bss
.text
.globl text@@start;
.globl text@@size;
.globl data@@start;
.globl data@@size;
.globl bss@@start;
data@@start = text@@start + text@@size
bss@@start = data@@start + data@@size

.globl _start;
.globl @@hook;
@@hook:
	ldc 0xc0800000
	gajw
	ajw -8
	call _start
L1:	j L1
