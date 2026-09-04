perf_lab_exe = executable('perf_lab', 'main.cpp',
  dependencies : [prism_dep],
)

custom_target('svg_perf_lab',
  output : 'perf_lab.svg',
  command : [perf_lab_exe, '@OUTPUT@'],
  depends : perf_lab_exe,
  build_by_default : true,
)
