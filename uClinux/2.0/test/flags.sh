L=/work/tools/lib/gcc-lib/t450/2.7.2-t800.12
for o in "-O2 -fno-schedule-insns" "-O2 -fno-schedule-insns2" "-O2 -fno-strength-reduce -fno-rerun-cse-after-loop" "-O2 -fno-cse-follow-jumps -fno-cse-skip-blocks" "-O2 -fno-expensive-optimizations" "-O1" "-O2 -fno-thread-jumps" "-O2 -fno-defer-pop" "-O2 -fno-peephole" "-O2 -fno-caller-saves" "-O2 -fno-gcse"; do
  if $L/cc1 minix.i $o -fomit-frame-pointer -mt450 -fsigned-char -quiet -o /dev/null >/dev/null 2>&1; then r=ok; else r=CRASH; fi; echo "[$o] $r"; done
