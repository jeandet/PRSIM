model_plot_exe = executable('model_plot', 'model_plot.cpp',
  dependencies : [prism_dep],
)

custom_target('svg_model_plot',
  output : 'model_plot.svg',
  command : [model_plot_exe, '@OUTPUT@'],
  depends : model_plot_exe,
  build_by_default : true,
)
