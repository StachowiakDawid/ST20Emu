project('st20emu', 'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++23',
    'warning_level=3',
    'buildtype=debug',
    'b_sanitize=address,undefined',
    'b_ndebug=if-release',
  ]
)

add_project_arguments('-Wconversion', '-Wshadow', '-Wold-style-cast', language: 'cpp')

compiler = meson.get_compiler('cpp')
if compiler.has_link_argument('-fuse-ld=mold')
  add_project_link_arguments('-fuse-ld=mold', language: 'cpp')
endif

core_sources = files(
  'src/core/cpu/instrentry.cpp',
  'src/core/cpu/instructions.cpp',
  'src/core/cpu/st20.cpp',
  'src/core/memory/memory.cpp',
  'src/soc/common/STi55xx_Asc.cpp',
  'src/soc/common/STi55xx_Display.cpp',
  'src/soc/common/STi55xx_Inc.cpp',
  'src/soc/common/STi55xx_Peripherals.cpp',
  'src/soc/common/STi55xx_Pio.cpp',
  'src/soc/common/STi55xx_Pwm.cpp',
  'src/soc/common/STi55xx_RegisterBlocks.cpp',
  'src/soc/common/STi55xx_Ssc.cpp',
  'src/soc/sti5512/STi5512_Devices.cpp',
  'src/soc/sti5512/STi5512_AudioDecoder.cpp',
  'src/soc/sti5512/STi5512_Display.cpp',
  'src/soc/sti5512/STi5512_Mpeg.cpp',
  'src/soc/sti5512/STi5512_Pti.cpp',
  'src/soc/sti5512/STi5512_PtiDma.cpp',
  'src/soc/sti5512/STi5512_RegisterBlocks.cpp',
  'src/soc/sti5512/STi5512_VideoDecoder.cpp',
  'src/soc/sti5518/STi5518_Devices.cpp',
  'src/soc/sti5518/STi5518_Display.cpp',
  'src/soc/sti5518/STi5518_Irb.cpp',
  'src/soc/sti5518/STi5518_RegisterBlocks.cpp',
  'src/board/Board.cpp',
  'src/board/DSR9500.cpp',
  'src/board/Dsr9500Frontend.cpp',
  'src/board/DSR7000.cpp',
  'src/board/BitBangI2c.cpp',
  'src/board/Stb6300.cpp',
  'src/board/AmdFlash.cpp',
)

sources = core_sources + files(
  'src/main.cpp',
  'src/console/serial_console.cpp',
  'src/console/serial_tcp.cpp',
  'src/cli/cli_commands.cpp',
  'src/cli/cli_engine.cpp',
  'src/cli/line_reader.cpp',
  'src/cli/cmds/control.cpp',
  'src/cli/cmds/devices.cpp',
  'src/cli/cmds/memory.cpp',
  'src/cli/cmds/registers.cpp',
)

threads = dependency('threads')

# FFmpeg's decoders give the MPEG video and audio models pictures and sound; without them the
# decoders keep their control behaviour only
libavcodec = dependency('libavcodec', required: get_option('libav'), include_type: 'system')
libavutil = dependency('libavutil', required: get_option('libav'), include_type: 'system')
deps = [threads]
if libavcodec.found() and libavutil.found()
  add_project_arguments('-DST20EMU_HAVE_LIBAV=1', language: 'cpp')
  deps += [libavcodec, libavutil]
endif

st20emu = executable('st20emu',
  sources,
  dependencies: deps,
  install: false
)

cpu_test = executable('cpu-instructions',
  core_sources + files('tests/cpu_instructions.cpp', 'src/console/serial_tcp.cpp'),
  dependencies: deps,
  install: false
)

test('cpu-instructions', cpu_test)

dvb_test = executable('dvb-frontend-pti',
  core_sources + files('tests/dvb_frontend_pti.cpp'),
  dependencies: deps,
  install: false
)

test('dvb-frontend-pti', dvb_test)

# end-to-end run of the original DSR9500 firmware (scan, lock, PAT/PMT, A/V); skipped unless
# ST20EMU_FIRMWARE_TESTS=1, as it executes about 1.5 billion instructions
test('dvb-firmware-scan', find_program('tools/dvb-firmware-test.sh'), args: [st20emu],
  suite: 'firmware', timeout: 3600, is_parallel: false)
