project('prism', 'cpp',
  version : '0.1.0',
  default_options : [
    'cpp_std=c++26',
    'warning_level=3',
    'werror=true',
    'b_ndebug=if-release',
    'sdl3_ttf:werror=false',
    'sdl3_ttf:warning_level=0',
  ],
  license : 'MIT',
  meson_version : '>= 1.5.0',
)

add_project_arguments('-freflection', language : 'cpp')

prism_inc = include_directories('include')

subdir('src')
subdir('tests')
subdir('examples')
