# Theme Palette Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Centralize ~100 hardcoded `Color::rgba()` calls into a flat semantic `Theme` struct, threaded through `WidgetNode` so delegates can read `node.theme->field`.

**Architecture:** Populate existing placeholder `Theme{}` in `context.hpp` with semantic color fields. Add `const Theme* theme` to `WidgetNode`. `WidgetTree` owns a `Theme` and propagates the pointer during `build_widget_node()`. Replace all hardcoded colors in delegates, layout, and chrome with theme field reads. Zero visual change — defaults match current values exactly.

**Tech Stack:** C++26, SDL3, doctest, Meson

---

### File Map

**Modify:**
- `include/prism/core/context.hpp` — populate `Theme` with semantic color fields, add `default_theme()`
- `include/prism/core/widget_node.hpp` — add `const Theme* theme` to `WidgetNode`
- `include/prism/core/widget_tree.hpp` — `WidgetTree` owns `Theme`, propagates pointer in `build_widget_node()`, pass theme to table rendering
- `include/prism/core/delegate.hpp` — replace all hardcoded `Color::rgba()` with theme reads
- `include/prism/core/text_delegates.hpp` — replace all hardcoded colors with theme reads
- `include/prism/core/dropdown_delegates.hpp` — replace all hardcoded colors with theme reads
- `include/prism/core/tabs_delegates.hpp` — replace all hardcoded colors with theme reads
- `include/prism/core/window_chrome.hpp` — `render()` takes `const Theme&`
- `include/prism/core/layout.hpp` — pass theme to table header and scrollbar rendering
- `src/backends/sdl_window.cpp` — clear color from theme, pass theme to chrome

**Create:**
- `tests/test_theme.cpp` — tests for Theme defaults and WidgetNode theme propagation

---

### Task 1: Theme struct and default_theme()

**Files:**
- Modify: `include/prism/core/context.hpp`
- Create: `tests/test_theme.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write tests for Theme defaults**

Create `tests/test_theme.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/context.hpp>

TEST_CASE("default_theme returns consistent values") {
    auto t = prism::default_theme();
    // Surface
    CHECK(t.surface.r == 45);
    CHECK(t.surface.g == 45);
    CHECK(t.surface.b == 55);
    // Primary
    CHECK(t.primary.r == 40);
    CHECK(t.primary.g == 105);
    CHECK(t.primary.b == 180);
    // Focus ring
    CHECK(t.focus_ring.r == 80);
    CHECK(t.focus_ring.g == 160);
    CHECK(t.focus_ring.b == 240);
    // Accent (slider/checkbox)
    CHECK(t.accent.r == 0);
    CHECK(t.accent.g == 140);
    CHECK(t.accent.b == 200);
}

TEST_CASE("Theme is copy-constructible and modifiable") {
    auto t = prism::default_theme();
    t.primary = prism::Color::rgba(255, 0, 0);
    CHECK(t.primary.r == 255);
    // default_theme() still returns original
    auto t2 = prism::default_theme();
    CHECK(t2.primary.r == 40);
}
```

- [ ] **Step 2: Add test to meson.build**

In `tests/meson.build`, add to the `headless_tests` dict:

```
  'theme' : files('test_theme.cpp'),
```

- [ ] **Step 3: Populate Theme in context.hpp**

Replace the placeholder `struct Theme {};` in `include/prism/core/context.hpp` with the full Theme struct. Replace the entire file content:

```cpp
#pragma once

#include <prism/core/draw_list.hpp>

#include <cstdint>

namespace prism {

enum class WidgetState : uint8_t {
    Normal   = 0,
    Hovered  = 1 << 0,
    Focused  = 1 << 1,
    Pressed  = 1 << 2,
    Disabled = 1 << 3,
};

[[nodiscard]] constexpr WidgetState operator|(WidgetState a, WidgetState b)
{
    return static_cast<WidgetState>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr bool has(WidgetState mask, WidgetState flag)
{
    return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(flag)) != 0;
}

struct Theme {
    // Surface backgrounds (widget fill)
    Color surface         = Color::rgba(45, 45, 55);
    Color surface_hover   = Color::rgba(55, 55, 68);
    Color surface_active  = Color::rgba(65, 65, 78);

    // Primary accent (buttons)
    Color primary         = Color::rgba(40, 105, 180);
    Color primary_hover   = Color::rgba(50, 120, 200);
    Color primary_active  = Color::rgba(30, 90, 160);
    Color primary_outline = Color::rgba(60, 140, 220);

    // Accent (slider thumbs, checked checkboxes — purer blue)
    Color accent          = Color::rgba(0, 140, 200);
    Color accent_hover    = Color::rgba(0, 160, 220);
    Color accent_active   = Color::rgba(0, 120, 180);

    // Text
    Color text            = Color::rgba(220, 220, 220);
    Color text_muted      = Color::rgba(180, 180, 190);
    Color text_placeholder= Color::rgba(120, 120, 130);
    Color text_on_primary = Color::rgba(240, 240, 240);

    // Borders & outlines
    Color border          = Color::rgba(90, 90, 105);
    Color border_hover    = Color::rgba(120, 120, 135);
    Color focus_ring      = Color::rgba(80, 160, 240);

    // Track (slider, scrollbar track backgrounds)
    Color track           = Color::rgba(60, 60, 70);
    Color track_hover     = Color::rgba(70, 70, 82);

    // Scrollbar thumb
    Color scrollbar_thumb = Color::rgba(120, 120, 130, 160);

    // Text cursor
    Color cursor          = Color::rgba(220, 220, 240);

    // Dropdown popup
    Color popup_bg        = Color::rgba(50, 50, 62);
    Color popup_border    = Color::rgba(80, 80, 95);
    Color popup_highlight = Color::rgba(60, 100, 180);

    // Table
    Color table_row_even  = Color::rgba(30, 30, 50);
    Color table_row_odd   = Color::rgba(26, 26, 46);
    Color table_selected  = Color::rgba(50, 50, 120);
    Color table_header    = Color::rgba(42, 42, 74);
    Color table_divider   = Color::rgba(50, 50, 70);
    Color table_header_divider = Color::rgba(74, 74, 106);
    Color table_header_text    = Color::rgba(136, 136, 204);

    // Tabs
    Color tab_bar_bg      = Color::rgba(42, 42, 58);
    Color tab_active_bg   = Color::rgba(30, 30, 46);
    Color tab_text        = Color::rgba(140, 140, 160);
    Color tab_text_active = Color::rgba(220, 220, 240);
    Color tab_accent      = Color::rgba(124, 111, 255);

    // Window chrome
    Color chrome_bg       = Color::rgba(45, 45, 48);
    Color chrome_border   = Color::rgba(60, 60, 65);
    Color chrome_text     = Color::rgba(200, 200, 200);
    Color chrome_icon     = Color::rgba(180, 180, 180);
    Color chrome_close    = Color::rgba(196, 43, 28);

    // Canvas clear color (backend render background)
    Color canvas_bg       = Color::rgba(30, 30, 30);
};

inline Theme default_theme() { return {}; }

struct Context {
    const Theme& theme;
    WidgetState  state = WidgetState::Normal;
};

template <typename T>
concept Widget = requires(const T w, DrawList& dl, const Context& ctx) {
    { w.record(dl, ctx) } -> std::same_as<void>;
};

} // namespace prism
```

- [ ] **Step 4: Run tests**

Run: `meson test -C builddir theme --print-errorlogs`

Expected: All tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/context.hpp tests/test_theme.cpp tests/meson.build
git commit -m "feat(theme): populate Theme struct with semantic color fields"
```

---

### Task 2: Theme pointer on WidgetNode + WidgetTree ownership

**Files:**
- Modify: `include/prism/core/widget_node.hpp`
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_theme.cpp`

- [ ] **Step 1: Add theme test for WidgetNode propagation**

Append to `tests/test_theme.cpp`:

```cpp
#include <prism/core/field.hpp>
#include <prism/core/widget_tree.hpp>

struct ThemeTestModel {
    prism::Field<bool> toggle{false};
    prism::Field<std::string> name{"hello"};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(toggle, name);
    }
};

TEST_CASE("WidgetTree propagates theme to all nodes") {
    ThemeTestModel model;
    prism::WidgetTree tree(model);
    auto& root = tree.root();
    CHECK(root.theme != nullptr);
    for (auto& child : root.children) {
        CHECK(child.theme != nullptr);
        CHECK(child.theme == root.theme);
    }
}

TEST_CASE("WidgetTree theme matches default_theme") {
    ThemeTestModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.theme().primary.r == 40);
    CHECK(tree.theme().primary.g == 105);
}
```

- [ ] **Step 2: Add `const Theme* theme` to WidgetNode**

In `include/prism/core/widget_node.hpp`, add to the `WidgetNode` struct after the `ExpandAxis expand_axis` line:

```cpp
    const Theme* theme = nullptr;
```

Also add `#include <prism/core/context.hpp>` at the top of the file (after the existing includes).

- [ ] **Step 3: Add Theme ownership to WidgetTree + propagation**

In `include/prism/core/widget_tree.hpp`, add a `Theme` member and a `theme()` accessor. In the private section (after line 670 `WidgetNode root_;`), add:

```cpp
    Theme theme_;
```

Add public accessor (after `clear_dirty()` around line 446):

```cpp
    const Theme& theme() const { return theme_; }
```

Modify `build_widget_node()` to NOT set theme (it's static). Instead, add a new method `propagate_theme()` and call it in the constructor. Add right before `build_index`:

```cpp
    void propagate_theme(WidgetNode& node) {
        node.theme = &theme_;
        for (auto& child : node.children)
            propagate_theme(child);
    }
```

In the constructor (around line 428-435), add `propagate_theme(root_)` after `build_widget_node`:

```cpp
    template <typename Model>
    explicit WidgetTree(Model& model) {
        auto node_tree = build_node_tree(model);
        root_ = build_widget_node(node_tree);
        propagate_theme(root_);
        connect_dirty(node_tree, root_);
        build_index(root_);
        clear_dirty();
    }
```

Also, wherever new WidgetNodes are created dynamically (virtual list pool, tab materialization), propagate_theme must be called. Search for other calls to `build_widget_node` in widget_tree.hpp — there's one around line 1120 for tab materialization. After that `build_widget_node` call, add `propagate_theme(child_wn);`.

- [ ] **Step 4: Run tests**

Run: `meson test -C builddir theme --print-errorlogs`

Expected: All tests PASS.

- [ ] **Step 5: Run full test suite**

Run: `meson test -C builddir --print-errorlogs`

Expected: All 35 tests PASS (34 existing + 1 new).

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/widget_node.hpp include/prism/core/widget_tree.hpp tests/test_theme.cpp
git commit -m "feat(theme): add Theme to WidgetNode and WidgetTree, propagate to all nodes"
```

---

### Task 3: Replace hardcoded colors in delegate.hpp

**Files:**
- Modify: `include/prism/core/delegate.hpp`

This is the largest file with hardcoded colors. Every `Color::rgba(...)` in record() methods gets replaced with a theme field read via `node.theme->`.

- [ ] **Step 1: Replace colors in primary Delegate<T> (fallback)**

In `include/prism/core/delegate.hpp`, in `struct Delegate` (the primary template, around line 277):

Replace:
```cpp
        auto bg = vs.hovered ? Color::rgba(60, 60, 72) : Color::rgba(50, 50, 60);
        dl.filled_rect(detail::make_rect(0, 0, 200, 30), bg);
```

With:
```cpp
        auto& t = *node.theme;
        auto bg = vs.hovered ? t.surface_hover : t.surface;
        dl.filled_rect(detail::make_rect(0, 0, 200, 30), bg);
```

- [ ] **Step 2: Replace colors in Delegate<StringLike T>**

In the StringLike specialization (around line 291):

Replace:
```cpp
        auto& vs = node_vs(node);
        auto bg = vs.hovered ? Color::rgba(60, 60, 72) : Color::rgba(50, 50, 60);
        dl.filled_rect(detail::make_rect(0, 0, 200, 30), bg);
        dl.text(std::string(field.get().data(), field.get().size()),
                detail::make_point(4, 4), 14, Color::rgba(220, 220, 220));
```

With:
```cpp
        auto& vs = node_vs(node);
        auto& t = *node.theme;
        auto bg = vs.hovered ? t.surface_hover : t.surface;
        dl.filled_rect(detail::make_rect(0, 0, 200, 30), bg);
        dl.text(std::string(field.get().data(), field.get().size()),
                detail::make_point(4, 4), 14, t.text);
```

- [ ] **Step 3: Replace colors in draw_check_box**

In `draw_check_box()` (around line 310), add a `const Theme& t` parameter and replace all colors:

Replace the entire function:
```cpp
inline void draw_check_box(DrawList& dl, float x, float y, bool checked,
                           const WidgetVisualState& vs, const Theme& t) {
    constexpr float box_size = 16.f;
    constexpr float border = 1.5f;

    if (checked) {
        auto fill = vs.pressed  ? t.accent_active
                  : vs.hovered  ? t.accent_hover
                  :               t.accent;
        dl.filled_rect(detail::make_rect(x, y, box_size, box_size), fill);
        dl.text("\xe2\x9c\x93", detail::make_point(x + 2, y + 1), 13, t.text_on_primary);
    } else {
        auto fill = vs.pressed  ? t.surface_active
                  : vs.hovered  ? t.surface_hover
                  :               t.surface;
        dl.filled_rect(detail::make_rect(x, y, box_size, box_size), fill);
    }
    dl.rect_outline(detail::make_rect(x, y, box_size, box_size),
                    vs.hovered ? t.border_hover : t.border,
                    border);
}
```

- [ ] **Step 4: Replace colors in Delegate<Checkbox>**

In `Delegate<Checkbox>::record()` (around line 338), update to use theme and pass `t` to `draw_check_box`:

Replace the record body:
```cpp
    static void record(DrawList& dl, const Field<Checkbox>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = *node.theme;
        auto& cb = field.get();

        auto bg = vs.hovered ? t.surface_hover : t.surface;
        dl.filled_rect(detail::make_rect(0, 0, widget_w, widget_h), bg);

        float box_y = (widget_h - box_size) / 2.f;
        draw_check_box(dl, 8, box_y, cb.checked, vs, t);

        if (!cb.label.empty())
            dl.text(cb.label, detail::make_point(32, 7), 14, t.text);

        if (vs.focused)
            dl.rect_outline(detail::make_rect(-1, -1, widget_w + 2, widget_h + 2),
                            t.focus_ring, 2.0f);
    }
```

- [ ] **Step 5: Replace colors in Delegate<bool>**

In `Delegate<bool>::record()` (around line 376):

Replace the record body:
```cpp
    static void record(DrawList& dl, const Field<bool>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = *node.theme;
        constexpr float widget_w = 200.f, widget_h = 30.f;
        constexpr float box_size = 16.f;

        auto bg = vs.hovered ? t.surface_hover : t.surface;
        dl.filled_rect(detail::make_rect(0, 0, widget_w, widget_h), bg);

        float box_y = (widget_h - box_size) / 2.f;
        draw_check_box(dl, 8, box_y, field.get(), vs, t);

        if (vs.focused)
            dl.rect_outline(detail::make_rect(-1, -1, widget_w + 2, widget_h + 2),
                            t.focus_ring, 2.0f);
    }
```

- [ ] **Step 6: Replace colors in Delegate<Label<T>>**

In `Delegate<Label<T>>::record()` (around line 406):

Replace:
```cpp
    static void record(DrawList& dl, const Field<Label<T>>& field, WidgetNode&) {
        dl.filled_rect(detail::make_rect(0, 0, 200, 24), Color::rgba(40, 40, 48));
        dl.text(std::string(field.get().value.data(), field.get().value.size()),
                detail::make_point(4, 4), 14, Color::rgba(180, 180, 190));
    }
```

With:
```cpp
    static void record(DrawList& dl, const Field<Label<T>>& field, WidgetNode& node) {
        auto& t = *node.theme;
        dl.filled_rect(detail::make_rect(0, 0, 200, 24), t.surface);
        dl.text(std::string(field.get().value.data(), field.get().value.size()),
                detail::make_point(4, 4), 14, t.text_muted);
    }
```

Note: the `WidgetNode&` parameter loses the `unused` status — remove the unnamed parameter.

- [ ] **Step 7: Replace colors in Delegate<Slider>**

In `Delegate<Slider<T,O>>::record()` (around line 449):

Replace:
```cpp
        auto track_bg = vs.hovered ? Color::rgba(70, 70, 82) : Color::rgba(60, 60, 70);
        auto thumb_color = vs.pressed ? Color::rgba(0, 120, 180)
                         : vs.hovered ? Color::rgba(0, 160, 220)
                         : Color::rgba(0, 140, 200);
```

With:
```cpp
        auto& t = *node.theme;
        auto track_bg = vs.hovered ? t.track_hover : t.track;
        auto thumb_color = vs.pressed ? t.accent_active
                         : vs.hovered ? t.accent_hover
                         : t.accent;
```

Also replace both focus ring colors in the same method (lines ~466 and ~474):

Replace `Color::rgba(80, 160, 240)` with `t.focus_ring` (2 occurrences in the if constexpr branches).

- [ ] **Step 8: Replace colors in Delegate<Button>**

In `Delegate<Button>::record()` (around line 515):

Replace:
```cpp
        Color bg = vs.pressed ? Color::rgba(30, 90, 160)
                 : vs.hovered ? Color::rgba(50, 120, 200)
                 : Color::rgba(40, 105, 180);
        dl.filled_rect(detail::make_rect(0, 0, 200, 32), bg);
        dl.rect_outline(detail::make_rect(0, 0, 200, 32), Color::rgba(60, 140, 220), 1.0f);
        dl.text(field.get().text, detail::make_point(8, 7), 14, Color::rgba(240, 240, 240));
        if (vs.focused)
            dl.rect_outline(detail::make_rect(-2, -2, 204, 36), Color::rgba(80, 160, 240), 2.0f);
```

With:
```cpp
        auto& t = *node.theme;
        Color bg = vs.pressed ? t.primary_active
                 : vs.hovered ? t.primary_hover
                 : t.primary;
        dl.filled_rect(detail::make_rect(0, 0, 200, 32), bg);
        dl.rect_outline(detail::make_rect(0, 0, 200, 32), t.primary_outline, 1.0f);
        dl.text(field.get().text, detail::make_point(8, 7), 14, t.text_on_primary);
        if (vs.focused)
            dl.rect_outline(detail::make_rect(-2, -2, 204, 36), t.focus_ring, 2.0f);
```

- [ ] **Step 9: Run full test suite**

Run: `meson test -C builddir --print-errorlogs`

Expected: All tests PASS.

- [ ] **Step 10: Commit**

```bash
git add include/prism/core/delegate.hpp
git commit -m "feat(theme): replace hardcoded colors in delegate.hpp with theme reads"
```

---

### Task 4: Replace hardcoded colors in text_delegates.hpp

**Files:**
- Modify: `include/prism/core/text_delegates.hpp`

- [ ] **Step 1: Replace colors in text_field_record()**

In `include/prism/core/text_delegates.hpp`, in `text_field_record()` (around line 41), replace all Color::rgba calls:

Replace the color section:
```cpp
    auto bg = vs.focused ? Color::rgba(65, 65, 78)
            : vs.hovered ? Color::rgba(55, 55, 68)
            : Color::rgba(45, 45, 55);
    dl.filled_rect(make_rect(0, 0, tf_widget_w, tf_widget_h), bg);

    if (vs.focused)
        dl.rect_outline(make_rect(-1, -1, tf_widget_w + 2, tf_widget_h + 2),
                        Color::rgba(80, 160, 240), 2.0f);
```

With:
```cpp
    auto& t = *node.theme;
    auto bg = vs.focused ? t.surface_active
            : vs.hovered ? t.surface_hover
            : t.surface;
    dl.filled_rect(make_rect(0, 0, tf_widget_w, tf_widget_h), bg);

    if (vs.focused)
        dl.rect_outline(make_rect(-1, -1, tf_widget_w + 2, tf_widget_h + 2),
                        t.focus_ring, 2.0f);
```

Replace the placeholder color:
```cpp
                Color::rgba(120, 120, 130));
```
With:
```cpp
                t.text_placeholder);
```

Replace the text color:
```cpp
                Color::rgba(220, 220, 220));
```
With:
```cpp
                t.text);
```

Replace the cursor color:
```cpp
                       Color::rgba(220, 220, 240));
```
With:
```cpp
                       t.cursor);
```

- [ ] **Step 2: Replace colors in text_area_record()**

In `text_area_record()` (around line 224), apply the same pattern:

Replace:
```cpp
    auto bg = vs.focused ? Color::rgba(65, 65, 78)
            : vs.hovered ? Color::rgba(55, 55, 68)
            : Color::rgba(45, 45, 55);
```
With:
```cpp
    auto& t = *node.theme;
    auto bg = vs.focused ? t.surface_active
            : vs.hovered ? t.surface_hover
            : t.surface;
```

Replace `Color::rgba(80, 160, 240)` with `t.focus_ring`.

Replace `Color::rgba(120, 120, 130)` (placeholder) with `t.text_placeholder`.

Replace `Color::rgba(220, 220, 220)` (text) with `t.text`.

Replace `Color::rgba(220, 220, 240)` (cursor) with `t.cursor`.

- [ ] **Step 3: Run full test suite**

Run: `meson test -C builddir --print-errorlogs`

Expected: All tests PASS.

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/text_delegates.hpp
git commit -m "feat(theme): replace hardcoded colors in text delegates with theme reads"
```

---

### Task 5: Replace hardcoded colors in dropdown_delegates.hpp

**Files:**
- Modify: `include/prism/core/dropdown_delegates.hpp`

- [ ] **Step 1: Replace colors in dropdown_record()**

In `include/prism/core/dropdown_delegates.hpp`, in `dropdown_record()` (around line 34), replace all Color::rgba calls:

Replace:
```cpp
    auto bg = es.open    ? Color::rgba(65, 65, 78)
            : vs.hovered ? Color::rgba(55, 55, 68)
            : Color::rgba(45, 45, 55);
```
With:
```cpp
    auto& t = *node.theme;
    auto bg = es.open    ? t.surface_active
            : vs.hovered ? t.surface_hover
            : t.surface;
```

Replace `Color::rgba(220, 220, 220)` (label) with `t.text`.

Replace `Color::rgba(160, 160, 170)` (arrow) with `t.text_muted`.

Replace `Color::rgba(80, 160, 240)` (focus) with `t.focus_ring`.

Replace `Color::rgba(50, 50, 62)` (popup bg) with `t.popup_bg`.

Replace `Color::rgba(80, 80, 95)` (popup border) with `t.popup_border`.

Replace `Color::rgba(60, 100, 180)` (highlight) with `t.popup_highlight`.

Replace `Color::rgba(255, 255, 255)` (highlighted text) with `t.text_on_primary`.

Replace `Color::rgba(200, 200, 210)` (normal option text) with `t.text`.

- [ ] **Step 2: Run full test suite**

Run: `meson test -C builddir --print-errorlogs`

Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add include/prism/core/dropdown_delegates.hpp
git commit -m "feat(theme): replace hardcoded colors in dropdown delegates with theme reads"
```

---

### Task 6: Replace hardcoded colors in tabs_delegates.hpp

**Files:**
- Modify: `include/prism/core/tabs_delegates.hpp`

- [ ] **Step 1: Replace colors in tabs_record()**

In `include/prism/core/tabs_delegates.hpp`, in `tabs_record()` (around line 29), replace all Color::rgba calls:

Replace `Color::rgba(42, 42, 58)` (bar bg, line 38) with `t.tab_bar_bg`.

Add `auto& t = *node.theme;` at the start of the function body (after `auto& es = ...`).

Replace tab item bg (around line 50):
```cpp
        auto bg = is_selected ? Color::rgba(30, 30, 46)
                : is_hovered  ? Color::rgba(55, 55, 68)
                :               Color::rgba(42, 42, 58);
```
With:
```cpp
        auto bg = is_selected ? t.tab_active_bg
                : is_hovered  ? t.surface_hover
                :               t.tab_bar_bg;
```

Replace tab text (around line 55):
```cpp
        auto text_color = is_selected ? Color::rgba(220, 220, 240)
                                      : Color::rgba(140, 140, 160);
```
With:
```cpp
        auto text_color = is_selected ? t.tab_text_active
                                      : t.tab_text;
```

Replace accent underline `Color::rgba(124, 111, 255)` with `t.tab_accent`.

Replace focus ring `Color::rgba(80, 160, 240)` with `t.focus_ring`.

- [ ] **Step 2: Run full test suite**

Run: `meson test -C builddir --print-errorlogs`

Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add include/prism/core/tabs_delegates.hpp
git commit -m "feat(theme): replace hardcoded colors in tabs delegates with theme reads"
```

---

### Task 7: Replace hardcoded colors in window_chrome.hpp and sdl_window.cpp

**Files:**
- Modify: `include/prism/core/window_chrome.hpp`
- Modify: `src/backends/sdl_window.cpp`

- [ ] **Step 1: Add Theme parameter to WindowChrome::render()**

In `include/prism/core/window_chrome.hpp`, add `#include <prism/core/context.hpp>` at the top.

Change `render()` signature from:
```cpp
    static void render(DrawList& dl, int w, std::string_view title) {
```
To:
```cpp
    static void render(DrawList& dl, int w, std::string_view title, const Theme& t) {
```

Replace all hardcoded colors in the function body:

Replace `Color::rgba(45, 45, 48)` (title bar bg) with `t.chrome_bg`.

Replace `Color::rgba(60, 60, 65)` (bottom border) with `t.chrome_border`.

Replace `Color::rgba(200, 200, 200)` (title text) with `t.chrome_text`.

Replace `Color::rgba(45, 45, 48, 0)` (min/max button bg — transparent) with `Color::rgba(t.chrome_bg.r, t.chrome_bg.g, t.chrome_bg.b, 0)`.

Replace `Color::rgba(180, 180, 180)` (min/max icons, 2 occurrences) with `t.chrome_icon`.

Replace `Color::rgba(196, 43, 28)` (close bg) with `t.chrome_close`.

Replace `Color::rgba(255, 255, 255)` (close icon) with `t.text_on_primary`.

- [ ] **Step 2: Update render_snapshot() in sdl_window.cpp**

In `src/backends/sdl_window.cpp`, the `render_snapshot()` method (around line 163):

Change the signature to accept a theme:
```cpp
void SdlWindow::render_snapshot(const SceneSnapshot& snap, TTF_Font* font, const Theme& theme) {
```

Replace the clear color:
```cpp
    SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 255);
```
With:
```cpp
    SDL_SetRenderDrawColor(renderer_, theme.canvas_bg.r, theme.canvas_bg.g,
                           theme.canvas_bg.b, theme.canvas_bg.a);
```

Replace the chrome render call:
```cpp
        WindowChrome::render(chrome_dl, w, title_);
```
With:
```cpp
        WindowChrome::render(chrome_dl, w, title_, theme);
```

- [ ] **Step 3: Update SdlWindow header**

In `include/prism/backends/sdl_window.hpp`, update the `render_snapshot` declaration:

Replace:
```cpp
    void render_snapshot(const SceneSnapshot& snap, TTF_Font* font);
```
With:
```cpp
    void render_snapshot(const SceneSnapshot& snap, TTF_Font* font, const Theme& theme);
```

Add `#include <prism/core/context.hpp>` to the includes.

- [ ] **Step 4: Update SoftwareBackend caller**

In `src/backends/software_backend.cpp`, the call to `render_snapshot` (around line 211):

Replace:
```cpp
                    it->second->render_snapshot(*snap, font_);
```
With:
```cpp
                    it->second->render_snapshot(*snap, font_, default_theme());
```

Add `#include <prism/core/context.hpp>` at the top of the file.

Note: The backend doesn't have access to the WidgetTree's theme, so it uses `default_theme()`. A future improvement could store a `Theme` on the backend or pass it through the snapshot, but for now `default_theme()` gives identical results since all themes are default.

- [ ] **Step 5: Run full test suite**

Run: `meson compile -C builddir && meson test -C builddir --print-errorlogs`

Expected: All tests PASS.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/window_chrome.hpp include/prism/backends/sdl_window.hpp src/backends/sdl_window.cpp src/backends/software_backend.cpp
git commit -m "feat(theme): replace hardcoded colors in window chrome and SDL backend"
```

---

### Task 8: Replace hardcoded colors in layout.hpp and widget_tree.hpp

**Files:**
- Modify: `include/prism/core/layout.hpp`
- Modify: `include/prism/core/widget_tree.hpp`

These files have table header/scrollbar colors (layout.hpp) and table cell rendering colors (widget_tree.hpp). Both have access to `WidgetNode` which now carries `theme`.

- [ ] **Step 1: Replace colors in layout.hpp**

In `include/prism/core/layout.hpp`, the table header section (around line 270-278):

Replace `Color::rgba(42, 42, 74)` with `node.theme->table_header`.

Replace `Color::rgba(74, 74, 106)` with `node.theme->table_header_divider`.

The two scrollbar thumb colors (lines ~340 and ~395):

Replace `Color::rgba(120, 120, 130, 160)` with `node.theme->scrollbar_thumb` (2 occurrences).

- [ ] **Step 2: Replace colors in widget_tree.hpp table rendering**

In `include/prism/core/widget_tree.hpp`, the table cell rendering (around lines 1231-1279):

Replace `Color::rgba(30, 30, 50)` (even row) with `node.theme->table_row_even`.

Replace `Color::rgba(26, 26, 46)` (odd row) with `node.theme->table_row_odd`.

Replace `Color::rgba(50, 50, 120)` (selected) with `node.theme->table_selected`.

Replace `Color::rgba(200, 200, 220)` (row text) with `node.theme->text`.

Replace `Color::rgba(50, 50, 70)` (divider) with `node.theme->table_divider`.

Replace `Color::rgba(136, 136, 204)` (header text) with `node.theme->table_header_text`.

- [ ] **Step 3: Run full test suite**

Run: `meson test -C builddir --print-errorlogs`

Expected: All tests PASS.

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/layout.hpp include/prism/core/widget_tree.hpp
git commit -m "feat(theme): replace hardcoded colors in table layout and scrollbar"
```

---

### Task 9: Verify zero remaining hardcoded colors and run examples

- [ ] **Step 1: Grep for remaining Color::rgba in delegate/layout/chrome files**

Run: `grep -rn "Color::rgba" include/prism/core/delegate.hpp include/prism/core/text_delegates.hpp include/prism/core/dropdown_delegates.hpp include/prism/core/tabs_delegates.hpp include/prism/core/window_chrome.hpp include/prism/core/layout.hpp include/prism/core/widget_tree.hpp`

Expected: Zero matches in record()/render() functions. The only remaining `Color::rgba` should be in `context.hpp` (Theme defaults) and in test files.

- [ ] **Step 2: Run full test suite**

Run: `meson test -C builddir --print-errorlogs`

Expected: All tests PASS.

- [ ] **Step 3: Build and verify example compiles**

Run: `meson compile -C builddir`

Expected: Full build succeeds with no errors.

- [ ] **Step 4: Final commit if any cleanup needed**

If any stray colors were missed, fix and commit:

```bash
git add -u
git commit -m "feat(theme): final cleanup — all widget colors read from Theme"
```
