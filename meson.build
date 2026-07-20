project('prism', 'cpp',
  version : '0.1.0',
  default_options : [
    'cpp_std=c++26',
    'warning_level=3',
    'werror=true',
    'b_ndebug=if-release',
    'sdl3:werror=false',
    'sdl3:warning_level=0',
    'sdl3_ttf:werror=false',
    'sdl3_ttf:warning_level=0',
  ],
  license : 'MIT',
  meson_version : '>= 1.5.0',
)

cpp = meson.get_compiler('cpp')
if cpp.compiles('int main() {}', args : ['-freflection'], name : '-freflection support')
  add_project_arguments('-freflection', language : 'cpp')
endif

debug_tools_opt = get_option('prism_debug_tools')
if debug_tools_opt.auto()
  debug_tools_enabled = get_option('buildtype') != 'release'
else
  debug_tools_enabled = debug_tools_opt.enabled()
endif
if debug_tools_enabled
  add_project_arguments('-DPRISM_DEBUG_TOOLS_ENABLED', language : 'cpp')
endif

prism_inc = include_directories('include')

subdir('src')
subdir('tests')
subdir('examples')
