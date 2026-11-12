#!/bin/sh
# Makes a small DVB-like transport stream for the emulated DSR9500 front end
# (`--transponder OUT.ts:FREQ:SR:POL`): two TV services, each MPEG-2 video 720x576 25 fps
# (4:3, I/P/B GOPs) plus MPEG-1 layer II stereo audio at 48 kHz, with PAT, PMT, SDT (service
# names "Emu Test 1" and "Emu Test 2") and, where FFmpeg supports it, NIT. The multiplex is padded with null packets to the useful bit
# rate of a DVB-S QPSK channel at SR kSym/s and code rate FEC, so that the emulator, which
# plays the file at that rate, delivers it in real time. Video is at most 3 Mbit/s with the
# MP@ML VBV and a 0.5 s mux delay, so that a receiver's 384 KB bit buffer never overflows.
#
# usage: tools/mkts.sh OUT.ts [SECONDS] [SR_KSPS] [FEC]   (defaults: 4 s, 6000 kSym/s, 3/4)
set -eu

out=${1:?usage: mkts.sh OUT.ts [SECONDS] [SR_KSPS] [FEC]}
seconds=${2:-4}
sr=${3:-6000}
fec=${4:-3/4}
fec_n=${fec%/*}
fec_d=${fec#*/}
command -v ffmpeg >/dev/null || { echo "mkts.sh: ffmpeg not found" >&2; exit 1; }

# useful bit rate of DVB-S QPSK: SR * 2 * FEC * 188/204
muxrate=$(( sr * 1000 * 2 * fec_n / fec_d * 188 / 204 ))
# leave room for audio, tables and PCR in each service's share
video_rate=$(( muxrate / 2 - 400000 ))
# keep a transport-stream decoder model within the STi5512's bit buffer (384 KB): MP@ML VBV
# (1835008 bit) and at most 0.5 s between arrival and decoding
[ "$video_rate" -gt 3000000 ] && video_rate=3000000
if [ "$video_rate" -lt 1000000 ]; then
  echo "mkts.sh: SR $sr kSym/s is too low for two services (need >= 2000)" >&2
  exit 1
fi

ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc2=size=720x576:rate=25" \
  -f lavfi -i "sine=frequency=440:sample_rate=48000:beep_factor=4" \
  -f lavfi -i "smptehdbars=size=720x576:rate=25,drawtext=text='SERVICE 2 %{pts\\:hms}':fontsize=40:x=60:y=60:fontcolor=white:box=1:boxcolor=black" \
  -f lavfi -i "sine=frequency=1000:sample_rate=48000" \
  -t "$seconds" \
  -map 0:v -map 1:a -map 2:v -map 3:a \
  -c:v mpeg2video -pix_fmt yuv420p -aspect 4:3 -g 12 -bf 2 \
  -b:v "$video_rate" -maxrate "$video_rate" -bufsize 1835008 \
  -c:a mp2 -b:a 192k -ar 48000 -ac 2 \
  -program "program_num=1:title=Emu Test 1:st=0:st=1" \
  -program "program_num=2:title=Emu Test 2:st=2:st=3" \
  -mpegts_transport_stream_id 1 -mpegts_original_network_id 1 \
  -mpegts_flags +nit -muxrate "$muxrate" -pcr_period 40 -muxdelay 0.5 -muxpreload 0.5 \
  -f mpegts "$out" 2>/dev/null || {
  # older ffmpeg without the drawtext filter (no libfreetype) or the nit flag
  ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "testsrc2=size=720x576:rate=25" \
    -f lavfi -i "sine=frequency=440:sample_rate=48000:beep_factor=4" \
    -f lavfi -i "smptehdbars=size=720x576:rate=25" \
    -f lavfi -i "sine=frequency=1000:sample_rate=48000" \
    -t "$seconds" \
    -map 0:v -map 1:a -map 2:v -map 3:a \
    -c:v mpeg2video -pix_fmt yuv420p -aspect 4:3 -g 12 -bf 2 \
    -b:v "$video_rate" -maxrate "$video_rate" -bufsize 1835008 \
    -c:a mp2 -b:a 192k -ar 48000 -ac 2 \
    -program "program_num=1:title=Emu Test 1:st=0:st=1" \
    -program "program_num=2:title=Emu Test 2:st=2:st=3" \
      -mpegts_transport_stream_id 1 -mpegts_original_network_id 1 \
    -muxrate "$muxrate" -pcr_period 40 -muxdelay 0.5 -muxpreload 0.5 \
    -f mpegts "$out"
}
echo "$out: $(( $(wc -c < "$out") / 188 )) packets, $seconds s at $muxrate bit/s (SR $sr kSym/s, QPSK $fec)"
