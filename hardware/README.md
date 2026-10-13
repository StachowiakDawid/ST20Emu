# STi5512 Interruptible-Copy Probe

This OS20 test runs the ST20 `I`-marked copy instructions from the low-priority
main task. A high-priority task wakes on OS20 timer ticks and uses `stshadow`
with selector `0xf` to save the low-priority shadow bank into RAM. The test
prints the raw 12-word snapshots over ASC3 after each copy and checks the
destination data.

The probe covers `move`, `devmove`, `move2dall`, `move2dnonzero`, and
`move2dzero`. `tin`, `taltwt`, and `dist` are not exercised here. A snapshot
count of zero means the copy finished before the sampler captured the shadow;
increase `PROBE_ROWS` or decrease the sampler delay in
`interruptible_probe.c` for a faster board/clock combination.

## Build

On the Windows toolset host, set `ST20ROOT` to the R2.3.1 installation and
initialize its environment, then run the build script from this directory:

```bat
set ST20ROOT=C:\ST\ST20R2.3.1
build.cmd
```

The build uses the SDK's `STi5512MB282B_sdram` profile. Adjust that profile in
`build.cmd` if your hardware differs.

## Run

Load `interruptible_probe.lku` with the configured board target, for example:

```bat
st20run -t sti5512_sdram interruptible_probe.lku
```

Connect a terminal to the board's ASC3 data port at 9600 baud, 8 data bits, no
parity, one stop bit, and no flow control. The SDK maps ASC3 TX/RX to PIO4[0]
and PIO4[1] on STi5512. Program loading/debugging still requires the transport
configured for the board; the serial port is used for probe output.

Each operation uses a 1 MiB source range. Samples contain the raw words written
by `stshadow`: enables, Status, Wptr, Iptr, A/B/C, and the five extended block
words. Interpret the latter as raw hardware observations; the probe does not
assume a continuation format.