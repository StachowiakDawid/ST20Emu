#!/bin/sh
# End-to-end check with the original DSR9500 firmware (firmware/dsr9500mcmain.bin): a test
# transponder made by tools/mkts.sh is put on air at 10788 MHz V 22000 kSym/s 5/6 (the manual
# scan's first preset), the firmware's Installation > Manual Scanning finds its services, saves
# them, and after a standby/wake cycle the channel starts: lock, PAT, SDT, PMT, video and audio
# slots, PCR, live pictures on screen, and the (unmuted) audio decoder output in a WAV file.
#
# usage: tools/dvb-firmware-test.sh [ST20EMU] [WORKDIR]
#   ST20EMU defaults to builddir-release/st20emu. Exit status 77 (skipped) when the firmware or
#   ffmpeg is missing, or under `meson test` unless ST20EMU_FIRMWARE_TESTS=1 (the run executes
#   about 1.5 billion instructions).
set -u

here=$(cd "$(dirname "$0")/.." && pwd)
emu=${1:-$here/builddir-release/st20emu}
firmware=$here/firmware/dsr9500mcmain.bin
if [ -n "${MESON_TEST_ITERATION:-}${MESON_BUILD_ROOT:-}" ] && [ "${ST20EMU_FIRMWARE_TESTS:-0}" != 1 ]; then
  echo "skipped: set ST20EMU_FIRMWARE_TESTS=1 to run the firmware scan test"
  exit 77
fi
[ -f "$firmware" ] || { echo "skipped: $firmware missing"; exit 77; }
command -v ffmpeg >/dev/null || { echo "skipped: ffmpeg missing"; exit 77; }
[ -x "$emu" ] || { echo "no emulator at $emu"; exit 1; }
emu=$(cd "$(dirname "$emu")" && pwd)/$(basename "$emu")

work=${2:-$(mktemp -d "${TMPDIR:-/tmp}/st20emu-dvb.XXXXXX")}
mkdir -p "$work"
"$here/tools/mkts.sh" "$work/tp.ts" 3 22000 5/6 >/dev/null || exit 1

# keys: the box comes up in the main menu (no channels); PIN 0000 opens Installation
cat > "$work/scan.txt" <<'EOF'
run 150000000
ir ok
run 50000000
ir 0
run 10000000
ir 0
run 10000000
ir 0
run 10000000
ir 0
run 100000000
ir 5
run 100000000
ir ok
run 300000000
screen scan.bmp
tuner
ir ok
run 150000000
run 100000000
ir exit
run 60000000
ir exit
run 60000000
ir exit
run 200000000
ir power
run 100000000
ir power
audio audio.wav
run 400000000
screen tv.bmp
audio stop
audio
video
tuner
EOF

cd "$work" || exit 1
ST20_PRINT_HOOK=7fe925f8@c ST20_PTI_TRACE=1 "$emu" --soc sti5512 --board dsr9500 --firmware "$firmware" \
  --transponder "$work/tp.ts:10788:22000:V:5/6" --script scan.txt > run.log 2>&1

status=0
check() {
  if grep -q -- "$2" run.log; then
    echo "ok   $1"
  else
    echo "FAIL $1 (no '$2' in $work/run.log)"
    status=1
  fi
}
check "demodulator locked to the test transponder" "locked to $work/tp.ts"
check "SDT section slot (scan)" "PTI slot [0-9]* pid 0x0011 cfg 0x2010"
check "tuner lock reported to the channel task" "TUNER TUNED"
check "PAT section slot" "PTI slot [0-9]* pid 0x0000 cfg 0x2010"
check "PMT section slot" "PTI slot [0-9]* pid 0x1000 cfg 0x2010"
check "PMT parsed by the firmware" "PMT received!"
check "video PES slot" "PTI slot 0 pid 0x0100 cfg 0x0404"
check "audio PES slot" "PTI slot 1 pid 0x0101 cfg 0x0008"
check "video decoder fed with PES" "input bytes (PES)"
check "live picture on screen" "showing a picture"
pictures=$(sed -n 's/.* \([0-9]*\) decoded, [0-9]* frame buffers.*/\1/p' run.log | tail -1)
if [ "${pictures:-0}" -gt 500 ]; then echo "ok   live pictures decoded ($pictures)"; else echo "FAIL live pictures decoded (${pictures:-0})"; status=1; fi
frames=$(sed -n 's/.*audio decoder: [a-z]*[ (a-z)]*, \([0-9]*\) frames decoded.*/\1/p' run.log | tail -1)
if [ "${frames:-0}" -gt 100 ]; then echo "ok   audio frames decoded ($frames)"; else echo "FAIL audio frames decoded (${frames:-0})"; status=1; fi
check "audio unmuted by the firmware" "audio decoder: playing, "
if [ -s audio.wav ] && [ "$(wc -c < audio.wav)" -gt 100000 ] &&
   ffmpeg -hide_banner -i audio.wav -af volumedetect -f null - 2>&1 | grep -q "max_volume: -[0-9]"; then
  echo "ok   audio capture $(wc -c < audio.wav) bytes, not silent"
else
  echo "FAIL audio capture"
  status=1
fi
echo "artifacts in $work (scan.bmp, tv.bmp, audio.wav, run.log)"
exit $status
