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

cpp = meson.get_compiler('cpp')
if cpp.has_argument('-freflection')
  add_project_arguments('-freflection', language : 'cpp')
endif

prism_inc = include_directories('include')

subdir('src')
subdir('tests')
subdir('examples')
