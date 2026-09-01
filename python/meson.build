pymod = import('python')
py = pymod.find_installation('python3', pure : false, required : false)
if py.found()
  nanobind_dep = dependency('nanobind', fallback : ['nanobind', 'nanobind_dep'], required : false)
  if nanobind_dep.found()
    # limited_api disabled: no stable ABI on free-threaded builds (3.14t/3.15t),
    # nanobind ignores it there but Meson would still define Py_LIMITED_API.
    py.extension_module('_prism_ext',
      'src/prism_ext.cpp',
      dependencies : [prism_dep, nanobind_dep],
      install : true,
      subdir : 'prism',
      override_options : ['werror=false', 'warning_level=0'],
    )
  endif
endif
