doctest_dep = dependency('doctest', fallback : ['doctest', 'doctest_dep'])

test_sources = {
  'mpsc_queue' : files('test_mpsc_queue.cpp'),
  'atomic_cell' : files('test_atomic_cell.cpp'),
  'draw_list' : files('test_draw_list.cpp'),
}

foreach name, src : test_sources
  exe = executable('test_' + name, src,
    dependencies : [prism_dep, doctest_dep],
  )
  test(name, exe)
endforeach
