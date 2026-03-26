prism_lib = library('prism',
  'prism.cpp',
  dependencies : [],
  include_directories : prism_inc,
  install : true,
)

prism_dep = declare_dependency(
  include_directories : prism_inc,
  link_with : prism_lib,
)
