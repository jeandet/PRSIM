# Codebase Reorganization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize PRISM's flat `include/prism/core/` (40 headers) into 6 logical module directories with matching namespaces.

**Architecture:** Move files with `git mv`, wrap contents in new namespaces, update all `#include` paths. Move `Color` from `draw_list.hpp` to `types.hpp`. Single atomic reorganization — no compatibility shims.

**Tech Stack:** C++23, Meson build system, doctest tests

---

### Task 1: Move Color to types.hpp and fix input_event.hpp dependency

This must happen first because the reorganization changes `input_event.hpp` to include `core/types.hpp` instead of `render/draw_list.hpp`.

**Files:**
- Modify: `include/prism/core/types.hpp`
- Modify: `include/prism/core/draw_list.hpp`
- Modify: `include/prism/core/input_event.hpp`

- [ ] **Step 1: Move Color struct from draw_list.hpp to types.hpp**

Cut the `Color` struct (with its `rgba` static method) from `include/prism/core/draw_list.hpp` and paste it at the end of `include/prism/core/types.hpp`, before the closing `}` of the namespace.

Remove the `#include <cstdint>` from draw_list.hpp if it was only needed for Color (types.hpp already has it).

- [ ] **Step 2: Update input_event.hpp include**

Change `#include <prism/core/draw_list.hpp>` to `#include <prism/core/types.hpp>` in `input_event.hpp`. The file only needs `Point`, `DX`, `DY`, `Rect`, `Offset` — all in types.hpp. With Color also moved there, draw_list.hpp is no longer needed.

- [ ] **Step 3: Build and run all tests**

```bash
ninja -C builddir && meson test -C builddir --print-errorlogs
```

Expected: all 37 tests pass. If any file can't find `Color`, add `#include <prism/core/types.hpp>` to it.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "refactor: move Color to types.hpp, decouple input_event from draw_list"
```

---

### Task 2: Create directory structure and move files

All `git mv` operations. No content changes yet.

**Directory creation + file moves:**

- [ ] **Step 1: Create new directories**

```bash
mkdir -p include/prism/{render,input,ui,delegates,app}
```

- [ ] **Step 2: Move core/ files that stay in core/**

These files stay in `include/prism/core/` (no move needed):
- types.hpp, traits.hpp, connection.hpp, field.hpp, state.hpp, list.hpp
- atomic_cell.hpp, mpsc_queue.hpp, exec.hpp, on.hpp, reflect.hpp

- [ ] **Step 3: Move render/ files**

```bash
git mv include/prism/core/draw_list.hpp include/prism/render/
git mv include/prism/core/scene_snapshot.hpp include/prism/render/
git mv include/prism/core/pixel_buffer.hpp include/prism/render/
git mv include/prism/core/software_renderer.hpp include/prism/render/
git mv include/prism/core/svg_export.hpp include/prism/render/
```

- [ ] **Step 4: Move input/ files**

```bash
git mv include/prism/core/input_event.hpp include/prism/input/
git mv include/prism/core/hit_test.hpp include/prism/input/
```

- [ ] **Step 5: Move ui/ files**

```bash
git mv include/prism/core/delegate.hpp include/prism/ui/
git mv include/prism/core/context.hpp include/prism/ui/
git mv include/prism/core/widget_node.hpp include/prism/ui/
git mv include/prism/core/node.hpp include/prism/ui/
git mv include/prism/core/layout.hpp include/prism/ui/
git mv include/prism/core/table.hpp include/prism/ui/
git mv include/prism/core/animation.hpp include/prism/ui/
git mv include/prism/core/window_chrome.hpp include/prism/ui/
```

- [ ] **Step 6: Move delegates/ files**

```bash
git mv include/prism/core/text_delegates.hpp include/prism/delegates/
git mv include/prism/core/dropdown_delegates.hpp include/prism/delegates/
git mv include/prism/core/tabs_delegates.hpp include/prism/delegates/
```

- [ ] **Step 7: Move app/ files**

```bash
git mv include/prism/core/window.hpp include/prism/app/
git mv include/prism/core/backend.hpp include/prism/app/
git mv include/prism/core/headless_window.hpp include/prism/app/
git mv include/prism/core/null_backend.hpp include/prism/app/
git mv include/prism/core/test_backend.hpp include/prism/app/
git mv include/prism/core/app.hpp include/prism/app/
git mv include/prism/core/ui.hpp include/prism/app/
git mv include/prism/core/model_app.hpp include/prism/app/
git mv include/prism/core/widget_tree.hpp include/prism/app/
```

- [ ] **Step 8: Verify core/ only has the intended files**

```bash
ls include/prism/core/
```

Expected: types.hpp, traits.hpp, connection.hpp, field.hpp, state.hpp, list.hpp, atomic_cell.hpp, mpsc_queue.hpp, exec.hpp, on.hpp, reflect.hpp (11 files)

- [ ] **Step 9: Commit the moves (will not compile yet)**

```bash
git add -A && git commit -m "refactor: move headers to module directories (includes broken)"
```

---

### Task 3: Update all #include paths

Mechanical transformation: update every `#include <prism/core/X.hpp>` to point to the new location. The mapping is:

**render/:** draw_list, scene_snapshot, pixel_buffer, software_renderer, svg_export
**input/:** input_event, hit_test
**ui/:** delegate, context, widget_node, node, layout, table, animation, window_chrome
**delegates/:** text_delegates, dropdown_delegates, tabs_delegates
**app/:** window, backend, headless_window, null_backend, test_backend, app, ui, model_app, widget_tree

Everything else stays `prism/core/`.

- [ ] **Step 1: Update includes in all headers under include/prism/**

Apply the path mapping to every `#include` directive in:
- All files in `include/prism/core/` (11 files)
- All files in `include/prism/render/` (5 files)
- All files in `include/prism/input/` (2 files)
- All files in `include/prism/ui/` (8 files)
- All files in `include/prism/delegates/` (3 files)
- All files in `include/prism/app/` (9 files)
- All files in `include/prism/widgets/` (2 files)
- All files in `include/prism/backends/` (2 files)
- `include/prism/prism.hpp`

Full include path mapping (old → new):
```
prism/core/types.hpp         → prism/core/types.hpp        (unchanged)
prism/core/traits.hpp        → prism/core/traits.hpp       (unchanged)
prism/core/connection.hpp    → prism/core/connection.hpp   (unchanged)
prism/core/field.hpp         → prism/core/field.hpp        (unchanged)
prism/core/state.hpp         → prism/core/state.hpp        (unchanged)
prism/core/list.hpp          → prism/core/list.hpp         (unchanged)
prism/core/atomic_cell.hpp   → prism/core/atomic_cell.hpp  (unchanged)
prism/core/mpsc_queue.hpp    → prism/core/mpsc_queue.hpp   (unchanged)
prism/core/exec.hpp          → prism/core/exec.hpp         (unchanged)
prism/core/on.hpp            → prism/core/on.hpp           (unchanged)
prism/core/reflect.hpp       → prism/core/reflect.hpp      (unchanged)
prism/core/draw_list.hpp     → prism/render/draw_list.hpp
prism/core/scene_snapshot.hpp→ prism/render/scene_snapshot.hpp
prism/core/pixel_buffer.hpp  → prism/render/pixel_buffer.hpp
prism/core/software_renderer.hpp → prism/render/software_renderer.hpp
prism/core/svg_export.hpp    → prism/render/svg_export.hpp
prism/core/input_event.hpp   → prism/input/input_event.hpp
prism/core/hit_test.hpp      → prism/input/hit_test.hpp
prism/core/delegate.hpp      → prism/ui/delegate.hpp
prism/core/context.hpp       → prism/ui/context.hpp
prism/core/widget_node.hpp   → prism/ui/widget_node.hpp
prism/core/node.hpp          → prism/ui/node.hpp
prism/core/layout.hpp        → prism/ui/layout.hpp
prism/core/table.hpp         → prism/ui/table.hpp
prism/core/animation.hpp     → prism/ui/animation.hpp
prism/core/window_chrome.hpp → prism/ui/window_chrome.hpp
prism/core/text_delegates.hpp     → prism/delegates/text_delegates.hpp
prism/core/dropdown_delegates.hpp → prism/delegates/dropdown_delegates.hpp
prism/core/tabs_delegates.hpp     → prism/delegates/tabs_delegates.hpp
prism/core/window.hpp        → prism/app/window.hpp
prism/core/backend.hpp       → prism/app/backend.hpp
prism/core/headless_window.hpp → prism/app/headless_window.hpp
prism/core/null_backend.hpp  → prism/app/null_backend.hpp
prism/core/test_backend.hpp  → prism/app/test_backend.hpp
prism/core/app.hpp           → prism/app/app.hpp
prism/core/ui.hpp            → prism/app/ui.hpp
prism/core/model_app.hpp     → prism/app/model_app.hpp
prism/core/widget_tree.hpp   → prism/app/widget_tree.hpp
```

- [ ] **Step 2: Update includes in src/ files**

Files to update:
- `src/backend.cpp`: `prism/core/backend.hpp` → `prism/app/backend.hpp`
- `src/backends/software_backend.cpp`: `prism/core/window_chrome.hpp` → `prism/ui/window_chrome.hpp`
- `src/backends/sdl_window.cpp`: no prism includes directly (includes sdl_window.hpp)

- [ ] **Step 3: Update includes in tests/ files**

Apply the same mapping to all 35 test files. Key changes by test:
- test_animation.cpp: draw_list→render, field stays core
- test_app.cpp: app→app/app
- test_delegate.cpp: delegate→ui, draw_list→render, input_event→input, widget_tree→app
- test_dropdown.cpp: delegate→ui, hit_test→input, widget_tree→app
- test_exec.cpp: on→core (unchanged), field→core (unchanged)
- test_hit_test.cpp: hit_test→input
- test_input_event.cpp: input_event→input
- test_layout.cpp: layout→ui
- test_model_app.cpp: model_app→app, null_backend→app, headless_window→app, hit_test→input, scene_snapshot→render, delegate→ui, on→core
- test_null_backend.cpp: null_backend→app, backend→app, input_event→input
- test_plot.cpp: widgets/ paths unchanged
- test_software_renderer.cpp: software_renderer→render
- test_svg_export.cpp: svg_export→render
- test_table.cpp: table→ui, delegate→ui, hit_test→input, widget_tree→app, list→core
- test_ui.cpp: ui→app/ui, null_backend→app, test_backend→app, headless_window→app
- test_window.cpp: window→app, headless_window→app
- All widget tests (text_field, password, text_area, tabs, scroll, virtual_list, view, node, theme): delegate→ui, widget_tree→app, field→core, layout→ui, etc.

- [ ] **Step 4: Update includes in examples/ files**

- `examples/model_dashboard.cpp`: `prism/core/svg_export.hpp` → `prism/render/svg_export.hpp`
- `examples/model_plot.cpp`: no change (uses `prism/widgets/plot.hpp` and `prism/prism.hpp`)
- `examples/hello_rect.cpp`: no change (uses `prism/prism.hpp`)

- [ ] **Step 5: Build**

```bash
ninja -C builddir
```

Fix any remaining include path errors.

- [ ] **Step 6: Run all tests**

```bash
meson test -C builddir --print-errorlogs
```

Expected: all 37 tests pass.

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "refactor: update all include paths to new module directories"
```

---

### Task 4: Add namespaces to core/ files

Wrap each file's contents in `namespace prism::core { ... }`. The 11 files staying in core/ already use `namespace prism`. Change to `namespace prism::core`.

**Files:** All 11 files in `include/prism/core/`

- [ ] **Step 1: Update namespace in each core/ file**

For each of these files, replace `namespace prism {` with `namespace prism::core {` and update the closing comment:

- `types.hpp` — contains `namespace prism {` → `namespace prism::core {`
- `traits.hpp` — same transformation
- `connection.hpp` — same
- `field.hpp` — same
- `state.hpp` — same
- `list.hpp` — same
- `atomic_cell.hpp` — same
- `mpsc_queue.hpp` — same
- `exec.hpp` — may not have a prism namespace (just wraps stdexec). If so, add `namespace prism::core { }` around any prism-specific content.
- `on.hpp` — same
- `reflect.hpp` — same

Note: Some files may have `namespace prism::detail` blocks. These become `namespace prism::core::detail`.

- [ ] **Step 2: Update all references to core types throughout the codebase**

Every file that uses `prism::Field`, `prism::Point`, `prism::Color`, `prism::Connection`, etc. must now use `prism::core::Field`, `prism::core::Point`, etc.

Strategy: Add `using namespace prism::core;` at the top of each downstream namespace block where core types are used extensively. This avoids mass-qualifying every type reference.

For each file in render/, input/, ui/, delegates/, app/, widgets/, backends/:
- If the file's own namespace block uses core types extensively, add `using namespace prism::core;` inside that namespace.
- If the file uses core types in function signatures or struct definitions, qualify them.

For test files: add `using namespace prism::core;` after the existing `using namespace prism;` (or replace it).

- [ ] **Step 3: Build and test**

```bash
ninja -C builddir && meson test -C builddir --print-errorlogs
```

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "refactor: add prism::core namespace to core/ headers"
```

---

### Task 5: Add namespaces to render/ files

**Files:** All 5 files in `include/prism/render/`

- [ ] **Step 1: Update namespace in each render/ file**

For each file, replace `namespace prism {` with `namespace prism::render {`:
- `draw_list.hpp` — DrawList, DrawCmd, TextAnchor, all command structs
- `scene_snapshot.hpp` — SceneSnapshot
- `pixel_buffer.hpp` — PixelBuffer
- `software_renderer.hpp` — SoftwareRenderer
- `svg_export.hpp` — to_svg(), SvgEmitter

- [ ] **Step 2: Update references**

Files that use render types (DrawList, Color, SceneSnapshot, etc.) need qualifying or `using namespace prism::render;`.

Key consumers:
- ui/delegate.hpp, ui/context.hpp, ui/widget_node.hpp — use DrawList, Color
- ui/layout.hpp — uses DrawList, SceneSnapshot
- app/app.hpp, app/ui.hpp — use DrawList, SceneSnapshot
- app/model_app.hpp — uses SceneSnapshot
- All delegate files — use DrawList
- widgets/plot*.hpp — use DrawList
- backends/ — use DrawList, SceneSnapshot
- src/backends/ — use DrawList
- tests — use DrawList, SceneSnapshot, etc.

- [ ] **Step 3: Build and test**

```bash
ninja -C builddir && meson test -C builddir --print-errorlogs
```

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "refactor: add prism::render namespace to render/ headers"
```

---

### Task 6: Add namespaces to input/ files

**Files:** 2 files in `include/prism/input/`

- [ ] **Step 1: Update namespace in each input/ file**

- `input_event.hpp` — InputEvent, MouseMove/Button/Scroll, keys::, mods::, buttons::, localize_mouse()
  - `namespace prism {` → `namespace prism::input {`
  - Sub-namespaces: `prism::keys` → `prism::input::keys`, `prism::mods` → `prism::input::mods`, `prism::buttons` → `prism::input::buttons`
- `hit_test.hpp` — hit_test(), find_widget_rect()

- [ ] **Step 2: Update references**

Key consumers of input types:
- ui/delegate.hpp — uses InputEvent
- ui/widget_node.hpp — uses InputEvent
- app/model_app.hpp — uses MouseMove, MouseButton, MouseScroll, keys::, mods::
- widgets/plot.hpp — uses InputEvent, MouseMove, MouseButton, MouseScroll, buttons::
- tests — use InputEvent variants

- [ ] **Step 3: Build and test**

```bash
ninja -C builddir && meson test -C builddir --print-errorlogs
```

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "refactor: add prism::input namespace to input/ headers"
```

---

### Task 7: Add namespaces to ui/ files

**Files:** All 8 files in `include/prism/ui/`

- [ ] **Step 1: Update namespace in each ui/ file**

- `delegate.hpp` — WidgetState, Theme, char_width(), WidgetVisualState → `namespace prism::ui`
- `context.hpp` — sentinels, EditState, FocusPolicy, LayoutKind → `namespace prism::ui`
- `widget_node.hpp` — WidgetNode, VirtualListState, TabsState → `namespace prism::ui`
- `node.hpp` — Node → `namespace prism::ui`
- `layout.hpp` — LayoutNode, SizeHint, layout functions → `namespace prism::ui`
- `table.hpp` — TableSource, ColumnStorage, TableState → `namespace prism::ui`
- `animation.hpp` — AnimationClock, easing, Animation<T> → `namespace prism::ui`
  - `prism::ease::` → `prism::ui::ease::`
- `window_chrome.hpp` — WindowChrome → `namespace prism::ui`

- [ ] **Step 2: Update references**

Key consumers:
- delegates/ — use WidgetNode, Theme, EditState
- app/widget_tree.hpp — uses WidgetNode, LayoutNode, layout functions
- app/model_app.hpp — uses AnimationClock, WindowChrome, Theme
- widgets/plot*.hpp — use Theme, WidgetNode, char_width()
- backends/sdl_window.hpp — uses WindowChrome, Theme
- tests — use Theme, WidgetNode, LayoutNode, etc.

- [ ] **Step 3: Build and test**

```bash
ninja -C builddir && meson test -C builddir --print-errorlogs
```

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "refactor: add prism::ui namespace to ui/ headers"
```

---

### Task 8: Add namespaces to delegates/ files

**Files:** 3 files in `include/prism/delegates/`

- [ ] **Step 1: Update namespace in each delegates/ file**

- `text_delegates.hpp` → `namespace prism::delegates`
- `dropdown_delegates.hpp` → `namespace prism::delegates`
- `tabs_delegates.hpp` → `namespace prism::delegates`

These files currently use `namespace prism::detail`. Change to `namespace prism::delegates::detail` or `namespace prism::delegates`.

- [ ] **Step 2: Update references**

Only consumer: `app/widget_tree.hpp` — calls delegate detail functions.

- [ ] **Step 3: Build and test**

```bash
ninja -C builddir && meson test -C builddir --print-errorlogs
```

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "refactor: add prism::delegates namespace to delegates/ headers"
```

---

### Task 9: Add namespaces to app/ files

**Files:** All 9 files in `include/prism/app/`

- [ ] **Step 1: Update namespace in each app/ file**

- `window.hpp` — Window, WindowId, WindowConfig, DecorationMode → `namespace prism::app`
- `backend.hpp` — BackendBase, Backend → `namespace prism::app`
- `headless_window.hpp` — HeadlessWindow → `namespace prism::app`
- `null_backend.hpp` — NullBackend → `namespace prism::app`
- `test_backend.hpp` — TestBackend → `namespace prism::app`
- `app.hpp` — Frame → `namespace prism::app`
- `ui.hpp` — Ui<State> → `namespace prism::app`
- `model_app.hpp` — model_app(), AppContext → `namespace prism::app`
- `widget_tree.hpp` — WidgetTree, ViewBuilder → `namespace prism::app`

- [ ] **Step 2: Update references**

Key consumers:
- backends/ — uses Backend, Window, WindowConfig
- widgets/plot.hpp — uses WidgetNode (ui), WidgetTree (app)
- examples — use model_app(), AppContext, DecorationMode
- tests — use WidgetTree, NullBackend, TestBackend, etc.
- src/backend.cpp — uses BackendBase

- [ ] **Step 3: Build and test**

```bash
ninja -C builddir && meson test -C builddir --print-errorlogs
```

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "refactor: add prism::app namespace to app/ headers"
```

---

### Task 10: Update prism.hpp master include and final cleanup

- [ ] **Step 1: Update prism.hpp**

Replace all `#include <prism/core/...>` with the new paths. The master include should list all public headers from all modules.

- [ ] **Step 2: Verify include/prism/core/ has exactly 11 files**

```bash
ls include/prism/core/ | wc -l
```

Expected: 11

- [ ] **Step 3: Full build + all tests**

```bash
ninja -C builddir && meson test -C builddir --print-errorlogs
```

Expected: all 37 tests pass, both examples build.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "refactor: update prism.hpp master include for new module layout"
```

---

### Task 11: Update examples and verify end-to-end

- [ ] **Step 1: Build and run each example**

```bash
ninja -C builddir examples/hello_rect examples/model_dashboard examples/model_plot
```

All three should compile. Run each briefly to verify no runtime issues.

- [ ] **Step 2: Final commit if any fixes needed**

```bash
git add -A && git commit -m "refactor: fix example includes for new module layout"
```
