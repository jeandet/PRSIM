showcases = {
  'counter':  'showcase_counter.cpp',
  'slider':   'showcase_slider.cpp',
  'layout':   'showcase_layout.cpp',
  'canvas':   'showcase_canvas.cpp',
  'plot':     'showcase_plot.cpp',
}

foreach name, src : showcases
  exe = executable('showcase_' + name, src,
    dependencies : [prism_dep],
  )

  custom_target('svg_' + name,
    output : name + '.svg',
    command : [exe, '@OUTPUT@'],
    depends : exe,
    build_by_default : true,
  )
endforeach
