# Core: header-only + stdexec (header-only P2300 implementation)
stdexec_dep = dependency('stdexec', fallback : ['stdexec', 'stdexec_dep'])
fmt_dep = dependency('fmt', fallback : ['fmt', 'fmt_dep'])

cpp = meson.get_compiler('cpp')
has_reflection = cpp.compiles('''
  #if __cpp_impl_reflection
  int main() { return 0; }
  #else
  #error no reflection
  #endif
''', args : ['-freflection'],
   name : '__cpp_impl_reflection check')

if not has_reflection
  magic_enum_dep = dependency('magic_enum', fallback : ['magic_enum', 'magic_enum_dep'])
else
  magic_enum_dep = dependency('', required : false)
endif

prism_core_dep = declare_dependency(
  include_directories : prism_inc,
  dependencies : [stdexec_dep, magic_enum_dep, fmt_dep],
)

# Software backend: links SDL3 + SDL3_ttf
sdl3_dep = dependency('sdl3', fallback : ['sdl3', 'sdl3_dep'])
sdl3_ttf_dep = dependency('sdl3_ttf', fallback : ['sdl3_ttf', 'sdl3_ttf_dep'])

font_path = meson.project_source_root() / 'assets' / 'fonts' / 'JetBrainsMonoNerdFont-Regular.ttf'

prism_software_backend_lib = library('prism-software-backend',
  'backend.cpp',
  'backends/software_backend.cpp',
  'backends/sdl_window.cpp',
  dependencies : [prism_core_dep, sdl3_dep, sdl3_ttf_dep],
  cpp_args : ['-DPRISM_FONT_PATH="' + font_path + '"'],
  install : true,
)

prism_software_backend_dep = declare_dependency(
  link_with : prism_software_backend_lib,
  dependencies : [prism_core_dep],
)

# Convenience: core + default software backend (SDL3 headers exposed for keycodes etc.)
prism_dep = declare_dependency(
  dependencies : [prism_core_dep, prism_software_backend_dep, sdl3_dep],
)
