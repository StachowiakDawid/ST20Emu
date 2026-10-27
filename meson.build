project('st20emu', 'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++23',
    'optimization=3',
    'warning_level=3'
  ]
)

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
  'src/soc/common/STi55xx_Pio.cpp',
  'src/soc/common/STi55xx_Pwm.cpp',
  'src/soc/common/STi55xx_Ssc.cpp',
  'src/soc/sti5512/STi5512_Devices.cpp',
  'src/soc/sti5512/STi5512_Display.cpp',
  'src/soc/sti5512/STi5512_PtiDma.cpp',
  'src/soc/sti5512/STi5512_RegisterBlocks.cpp',
  'src/soc/sti5512/STi5512_VideoDecoder.cpp',
  'src/soc/sti5518/STi5518_Devices.cpp',
  'src/board/DSR9500.cpp',
)

sources = core_sources + files(
  'src/main.cpp',
  'src/cli/cli_commands.cpp',
  'src/cli/cli_engine.cpp',
  'src/cli/line_reader.cpp',
  'src/cli/cmds/control.cpp',
  'src/cli/cmds/devices.cpp',
  'src/cli/cmds/memory.cpp',
  'src/cli/cmds/registers.cpp',
)

executable('st20emu',
  sources,
  dependencies: dependency('threads'),
  install: false
)

cpu_test = executable('cpu-instructions',
  core_sources + files('tests/cpu_instructions.cpp'),
  install: false
)

test('cpu-instructions', cpu_test)
