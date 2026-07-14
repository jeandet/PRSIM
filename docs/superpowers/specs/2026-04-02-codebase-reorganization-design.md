# Codebase Reorganization Design

## Goal

Reorganize PRISM's flat `include/prism/core/` directory (40 headers) into logical module directories with matching namespaces. Clean break — no compatibility shims.

## Directory → Namespace Mapping

```
include/prism/
├── core/              prism::core     — fundamental types, reactive data, concurrency, exec
├── render/            prism::render   — draw commands, scene snapshot, rasterization, SVG
├── input/             prism::input    — events, hit testing
├── ui/                prism::ui       — widget model, layout, delegates, theme, animation
├── delegates/         prism::delegates — sentinel-specific rendering (text, dropdown, tabs)
├── app/               prism::app      — entry points, window, backend, widget tree
├── widgets/           prism::plot     — plot widget (existing namespace preserved)
├── backends/          prism::backends — SDL backend (existing)
└── prism.hpp          master include
```

## File Assignment

### core/ (prism::core)
| File | Contents |
|------|----------|
| types.hpp | Scalar<Tag>, Point, Size, Rect, Offset, Color, Progress, ExpandAxis |
| traits.hpp | is_field_v, is_state_v, is_component_v |
| connection.hpp | SenderHub<Args...>, Connection, Then adaptor |
| field.hpp | Field<T>, ObservableValue |
| state.hpp | State<T> |
| list.hpp | List<T> |
| atomic_cell.hpp | atomic_cell<T> |
| mpsc_queue.hpp | mpsc_queue<T> |
| exec.hpp | stdexec wrapper |
| on.hpp | On<Scheduler> adaptor |
| reflect.hpp | for_each_field, check_is_component |

### render/ (prism::render)
| File | Contents |
|------|----------|
| draw_list.hpp | TextAnchor, DrawCmd variant, DrawList, all command structs |
| scene_snapshot.hpp | SceneSnapshot |
| pixel_buffer.hpp | PixelBuffer |
| software_renderer.hpp | SoftwareRenderer |
| svg_export.hpp | to_svg() |

### input/ (prism::input)
| File | Contents |
|------|----------|
| input_event.hpp | InputEvent variant, MouseMove/Button/Scroll, keys::, mods::, buttons::, localize_mouse() |
| hit_test.hpp | hit_test(), find_widget_rect(), hit_test_overlay() |

### ui/ (prism::ui)
| File | Contents |
|------|----------|
| delegate.hpp | WidgetState, Theme, char_width(), WidgetVisualState |
| context.hpp | Sentinel types (Label, Slider, Checkbox, TextField, etc.), EditState, FocusPolicy, LayoutKind |
| widget_node.hpp | WidgetNode, VirtualListState, TabsState |
| node.hpp | Node |
| layout.hpp | LayoutNode, SizeHint, layout_measure/arrange/flatten, scrollbar constants |
| table.hpp | TableSource, ColumnStorage, TableState |
| animation.hpp | AnimationClock, easing functions, Animation<T>, TransitionGuard<T> |
| window_chrome.hpp | WindowChrome, HitZone |

### delegates/ (prism::delegates)
| File | Contents |
|------|----------|
| text_delegates.hpp | text_field_record(), mask_string(), text cursor rendering |
| dropdown_delegates.hpp | dropdown_record(), DropdownEditState |
| tabs_delegates.hpp | tabs_record(), TabBarEditState |

### app/ (prism::app)
| File | Contents |
|------|----------|
| window.hpp | Window, WindowId, WindowConfig, RenderConfig, DecorationMode |
| backend.hpp | BackendBase, Backend |
| headless_window.hpp | HeadlessWindow |
| null_backend.hpp | NullBackend |
| test_backend.hpp | TestBackend |
| app.hpp | Frame |
| ui.hpp | Ui<State> |
| model_app.hpp | model_app(), AppContext, route_* functions |
| widget_tree.hpp | WidgetTree, ViewBuilder, CanvasHandle, TableBuilder |

### widgets/ (prism::plot — preserved)
| File | Contents |
|------|----------|
| plot.hpp | PlotSource, Series, PlotModel |
| plot_render.hpp | PlotMapping, nice_ticks, rendering functions |

### backends/ (prism::backends — preserved)
| File | Contents |
|------|----------|
| software_backend.hpp | SoftwareBackend |
| sdl_window.hpp | SdlWindow |

## Type Relocation

**Color moves from draw_list.hpp to core/types.hpp.** Color is a fundamental type used by Theme, delegates, and draw commands alike. This eliminates the current dependency where input_event.hpp includes draw_list.hpp just to get Point (it was also pulling Color indirectly).

After the move:
- `core/types.hpp` — all geometry types + Color
- `render/draw_list.hpp` includes `core/types.hpp`
- `input/input_event.hpp` includes `core/types.hpp` (not render/)
- `ui/delegate.hpp` includes `core/types.hpp`

## Dependency Flow

```
core  ←  render  ←  input  ←  ui  ←  delegates  ←  app
                                          ↑
                                       widgets
```

No reverse dependencies. Each layer only includes from layers to its left.

## Namespace Rules

- Each directory gets its own namespace: `namespace prism::core { ... }`
- No re-exports — users must qualify (`prism::core::Field`) or use `using` declarations
- Sub-namespaces within a module are allowed (e.g., `prism::input::keys::`)
- `prism::plot` namespace is preserved as-is
- `prism::backends` namespace is preserved as-is
- `prism::detail` stays as sub-namespace within whichever module owns it

## Migration Strategy

Single atomic commit:
1. `git mv` files to new directories
2. Wrap each file's contents in the correct namespace
3. Update all `#include` paths across the entire codebase (headers, src/, tests/, examples/)
4. Move Color to core/types.hpp
5. Update prism.hpp master include
6. Update meson.build if needed (include dirs should remain `include/`)
7. Build and run all 37 tests

No forwarding headers, no compatibility shims, no deprecated aliases.

## Test Impact

Tests stay in flat `tests/` directory. Only changes:
- Update `#include` paths
- Add `using namespace` declarations or qualify types with new namespaces

## Build Impact

Meson `include_directories('include')` is already the top-level include dir. Subdirectory moves within `include/prism/` require no meson.build changes for include paths. Source file lists in `src/meson.build` stay the same.
