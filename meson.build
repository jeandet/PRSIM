project('prism', 'cpp',
  version : '0.1.0',
  default_options : [
    'cpp_std=c++26',
    'warning_level=3',
    'werror=true',
    'b_ndebug=if-release',
  ],
  license : 'MIT',
  meson_version : '>= 1.5.0',
)

prism_inc = include_directories('include')

sdl3_dep = dependency('sdl3', fallback : ['sdl3', 'sdl3_dep'])

subdir('src')
subdir('tests')
subdir('examples')
