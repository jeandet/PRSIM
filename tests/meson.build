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
  'state' : files('test_state.cpp'),
  'list' : files('test_list.cpp'),
  'reflect' : files('test_reflect.cpp'),
  'widget_tree' : files('test_widget_tree.cpp'),
  'widget' : files('test_widget.cpp'),
  'exec' : files('test_exec.cpp'),
  'text_field' : files('test_text_field.cpp'),
  'dropdown' : files('test_dropdown.cpp'),
  'password' : files('test_password.cpp'),
  'text_area' : files('test_text_area.cpp'),
  'types' : files('test_types.cpp'),
  'view' : files('test_view.cpp'),
  'node' : files('test_node.cpp'),
  'scroll' : files('test_scroll.cpp'),
  'virtual_list' : files('test_virtual_list.cpp'),
  'animation' : files('test_animation.cpp'),
  'table' : files('test_table.cpp'),
  'tabs' : files('test_tabs.cpp'),
  'window' : files('test_window.cpp'),
  'window_registry' : files('test_window_registry.cpp'),
  'theme' : files('test_theme.cpp'),
  'svg_export' : files('test_svg_export.cpp'),
  'plot' : files('test_plot.cpp'),
  'transaction' : files('test_transaction.cpp'),
  'transaction_cycle_guard' : files('test_transaction_cycle_guard.cpp'),
  'nested_scroll_bubble' : files('test_nested_scroll_bubble.cpp'),
  'table_hover_alias' : files('test_table_hover_alias.cpp'),
  'derived' : files('test_derived.cpp'),
  'shared' : files('test_shared.cpp'),
  'field_mirror' : files('test_field_mirror.cpp'),
  'inspector' : files('test_inspector.cpp'),
  'fixed_string' : files('test_fixed_string.cpp'),
  'debug_name' : files('test_debug_name.cpp'),
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
  'model_app' : files('test_model_app.cpp'),
}

foreach name, src : backend_base_tests
  exe = executable('test_' + name, src,
    dependencies : [prism_software_backend_dep, doctest_dep],
  )
  test(name, exe)
endforeach

# SoftwareBackend::request_window includes <prism/backends/software_backend.hpp>
# directly, which pulls in SDL3/SDL3_ttf headers — prism_software_backend_dep only
# forwards prism_core_dep, so this one needs sdl3_dep/sdl3_ttf_dep explicitly too.
test_software_backend_request_window = executable('test_software_backend_request_window',
  files('test_software_backend_request_window.cpp'),
  dependencies : [prism_software_backend_dep, sdl3_dep, sdl3_ttf_dep, doctest_dep],
)
test('software_backend_request_window', test_software_backend_request_window)

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
