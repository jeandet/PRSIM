py_mod = import('python')
python3 = py_mod.find_installation('python3', pure : false, required : false)
if python3.found()
  nanobind_dep = dependency('nanobind', fallback : ['nanobind', 'nanobind_dep'], required : false)
  if nanobind_dep.found()
    fs = import('fs')
    # Copy pure-python sources to builddir so PYTHONPATH=builddir works like in Sciqlop-cache
    fs.copyfile('__init__.py', '__init__.py')

    if meson.get_compiler('cpp').get_id() == 'msvc'
      prism_cpp_args = ['/bigobj']
    else
      prism_cpp_args = []
    endif

    if build_machine.system() == 'windows'
      prism_link_args = ['-static-libstdc++', '-static-libgcc', '-static']
    elif build_machine.system() == 'darwin' and meson.get_compiler('cpp').get_id() != 'clang'
      prism_link_args = ['-static-libstdc++', '-static-libgcc']
    else
      prism_link_args = []
    endif

    _prism_ext = python3.extension_module('_prism_ext',
      '../src/prism_ext.cpp',
      dependencies : [prism_dep, nanobind_dep, python3.dependency()],
      link_args : prism_link_args,
      cpp_args : prism_cpp_args,
      subdir : 'prism',
      install : true,
      override_options : ['werror=false', 'warning_level=0'],
    )

    python3.install_sources('__init__.py', pure : false, subdir : 'prism')
  endif
endif
