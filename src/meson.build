# Core: header-only, no external dependencies
prism_core_dep = declare_dependency(
  include_directories : prism_inc,
)

# Software backend: links SDL3
sdl3_dep = dependency('sdl3', fallback : ['sdl3', 'sdl3_dep'])

prism_software_backend_lib = library('prism-software-backend',
  'backend.cpp',
  'backends/software_backend.cpp',
  dependencies : [prism_core_dep, sdl3_dep],
  install : true,
)

prism_software_backend_dep = declare_dependency(
  link_with : prism_software_backend_lib,
  dependencies : [prism_core_dep],
)

# Convenience: core + default software backend
prism_dep = declare_dependency(
  dependencies : [prism_core_dep, prism_software_backend_dep],
)
