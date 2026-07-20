# NOTE: per-subproject 'sdl3:werror=false'/'sdl3:warning_level=0' below does NOT take effect on
# Meson 1.8+ due to a known upstream bug (mesonbuild/meson#14528: parent-project default_options
# cannot set a subproject's warning_level/werror). Kept here as documentation of intent (harmless,
# and would start working if that bug is ever fixed), but a genuinely clean `meson setup builddir`
# on an affected Meson version still needs the equivalent passed on the command line:
#   meson setup builddir -Dsdl3:werror=false -Dsdl3:warning_level=0
# sdl3_ttf's own wrap doesn't hit this in practice here, but is listed for the same intent.
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
