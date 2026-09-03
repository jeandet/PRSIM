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

    # Never link the C++ runtime statically into the extension: the prism shared libs it
    # loads (software-backend, fmt) use the shared libstdc++, and two runtimes in one
    # process abort in std::ios_base::Init ("pointer being freed was not allocated").
    prism_link_args = []

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

    # Python binding tests — part of `meson test` so WTF doesn't happen again.
    # Runs pytest against the source test file with PYTHONPATH pointing at
    # the just-built extension in builddir/python.
    python_bindings_env = ['PYTHONPATH=' + meson.project_build_root() / 'python']
    sanitize = get_option('b_sanitize')
    if sanitize.contains('address')
      # Loading a sanitized extension into an unsanitized CPython needs libasan
      # preloaded first; detect_leaks=0 keeps CPython's own allocations (which
      # LeakSanitizer can't unwind through the interpreter for) out of the report.
      # Resolved via ldconfig, not `gcc -print-file-name=libasan.so`: on some distros
      # (observed on Fedora 45) that command returns a GNU ld linker script
      # ("INPUT ( /usr/lib64/libasan.so.8.0.0 )"), not the actual ELF shared object,
      # which LD_PRELOAD then refuses to load ("file too short"). ldconfig's cache
      # always names the real runtime .so and works the same way on Ubuntu (CI).
      # `set -o pipefail` (bash, not sh — dash doesn't support it) + the empty-stdout
      # check make this fail loudly at configure time instead of silently proceeding
      # with an empty LD_PRELOAD (grep's exit code would otherwise be masked by awk's).
      libasan = run_command('bash', '-c', 'set -o pipefail; ldconfig -p | grep -m1 "libasan\.so" | awk \'{print $NF}\'', check : true).stdout().strip()
      if libasan == ''
        error('libasan.so not found via ldconfig -p; needed for LD_PRELOAD under b_sanitize=address')
      endif
      # detect_odr_violation=0: unsanitized CPython host trips false positives.
      python_bindings_env += ['LD_PRELOAD=' + libasan, 'ASAN_OPTIONS=detect_leaks=0:detect_odr_violation=0']
    endif
    if sanitize.contains('undefined')
      python_bindings_env += ['UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1']
    endif

    # --capture=sys (not the default fd capture): sanitizer reports are written to
    # fd 2 from C and the process exits right after, so with fd capture pytest
    # swallows the report and the log shows only "exit status 1".
    test('python_bindings',
      python3,
      args : ['-m', 'pytest', meson.project_source_root() / 'python/tests/test_prism_python.py', '-q', '-ra', '--capture=sys', '--import-mode=importlib'],
      env : python_bindings_env,
      depends : _prism_ext,
      timeout : 60,
    )
  endif
endif
