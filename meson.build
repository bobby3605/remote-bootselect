project('remote-bootselect', 'cpp',
  version : '0.0.0',
  default_options : ['warning_level=3', 'cpp_std=c++23'])

add_project_arguments('-O3', language : 'cpp')

inc = include_directories('external')

deps = [ dependency('libmosquitto') ]

srcs = [
'src/server/main.cpp',
'src/server/common.cpp',
'src/server/ConfigHandler.cpp',
'src/server/EventHandler.cpp',
'src/server/RequestHandler.cpp',
'src/server/MQTTHandler.cpp',
]

executable('remote-bootselect', srcs, include_directories: inc, dependencies: deps)

