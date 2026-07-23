model_system_monitor_exe = executable('model_system_monitor', 'model_system_monitor.cpp',
  dependencies : [prism_dep],
)

custom_target('svg_system_monitor',
  output : 'system_monitor.svg',
  command : [model_system_monitor_exe, '@OUTPUT@'],
  depends : model_system_monitor_exe,
  build_by_default : true,
)
