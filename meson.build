project('st20emu', 'c', 'cpp',
  version: '0.1.0',
  default_options: [
    'c_std=c23',
    'cpp_std=c++23',
    'optimization=3',
    'warning_level=3'
  ]
)

ncurses_dep = dependency('ncurses', required: false)

compiler = meson.get_compiler('cpp')
if compiler.has_link_argument('-fuse-ld=mold')
  add_project_link_arguments('-fuse-ld=mold', language: ['c', 'cpp'])
endif

sources = [
  'st20emu.cpp',
  'omr.cpp',
  'stdafx.cpp',
  'commands.c',
  'memory.c',
  'st20.c'
]

executable('st20emu',
  sources,
  dependencies: ncurses_dep,
  install: false
)
