// user/boot/bootcrt.s - entry of the boot init blob: the ROM loader calls the first byte
.text
.globl _bootinit;
.globl @@hook;
@@hook:
	j _bootinit
