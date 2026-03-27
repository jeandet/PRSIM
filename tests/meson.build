doctest_dep = dependency('doctest', fallback : ['doctest', 'doctest_dep'])

# Headless tests — no SDL, no backend
headless_tests = {
  'mpsc_queue' : files('test_mpsc_queue.cpp'),
  'atomic_cell' : files('test_atomic_cell.cpp'),
  'draw_list' : files('test_draw_list.cpp'),
  'input_event' : files('test_input_event.cpp'),
  'pixel_buffer' : files('test_pixel_buffer.cpp'),
  'software_renderer' : files('test_software_renderer.cpp'),
  'layout' : files('test_layout.cpp'),
  'hit_test' : files('test_hit_test.cpp'),
  'connection' : files('test_connection.cpp'),
  'field' : files('test_field.cpp'),
  'list' : files('test_list.cpp'),
  'reflect' : files('test_reflect.cpp'),
  'widget_tree' : files('test_widget_tree.cpp'),
}

foreach name, src : headless_tests
  exe = executable('test_' + name, src,
    dependencies : [prism_core_dep, doctest_dep],
  )
  test(name, exe)
endforeach

# Tests that need BackendBase vtable but not SDL
backend_base_tests = {
  'null_backend' : files('test_null_backend.cpp'),
  'test_backend' : files('test_test_backend.cpp'),
  'ui' : files('test_ui.cpp'),
}

foreach name, src : backend_base_tests
  exe = executable('test_' + name, src,
    dependencies : [prism_software_backend_dep, doctest_dep],
  )
  test(name, exe)
endforeach

# Tests that need a backend (SDL)
backend_tests = {
  'app' : files('test_app.cpp'),
}

foreach name, src : backend_tests
  exe = executable('test_' + name, src,
    dependencies : [prism_dep, doctest_dep],
  )
  test(name, exe)
endforeach
