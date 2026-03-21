project('remote-bootselect', 'cpp',
  version : '0.0.0',
  default_options : ['warning_level=3', 'cpp_std=c++23'])

add_project_arguments('-Og', '-g', language : 'cpp')

srcs = [
'src/server/main.cpp',
'src/server/common.cpp',
'src/server/ConfigHandler.cpp',
'src/server/EventHandler.cpp',
'src/server/RequestHandler.cpp',
]
executable('remote-bootselect', srcs)

