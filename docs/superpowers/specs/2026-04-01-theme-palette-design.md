# Theme Palette Design

## Goal

Centralize the ~100 hardcoded `Color::rgba()` calls scattered across 7 delegate/layout/chrome files into a single `Theme` struct with semantic color names. No per-widget style structs, no per-instance overrides, no light theme. Just one source of truth for the current dark palette.

## Theme Struct

Populate the existing placeholder `Theme{}` in `context.hpp` with flat semantic fields. Colors are grouped by role, not by widget — most widgets share the same surface/primary/text colors.

```cpp
struct Theme {
    // Surface backgrounds (widget fill)
    Color surface         = Color::rgba(45, 45, 55);
    Color surface_hover   = Color::rgba(55, 55, 68);
    Color surface_active  = Color::rgba(65, 65, 78);

    // Primary accent (buttons, checked checkboxes, slider thumbs)
    Color primary         = Color::rgba(40, 105, 180);
    Color primary_hover   = Color::rgba(50, 120, 200);
    Color primary_active  = Color::rgba(30, 90, 160);

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
```

All default values match the current hardcoded colors exactly — zero visual change after migration.

## Plumbing: Theme Pointer on WidgetNode

`WidgetNode` gains a `const Theme* theme = nullptr;` member. The `WidgetTree` owns one `Theme` instance and sets the pointer on every node during `build_widget_node()`.

Delegates access `node.theme->field_name`:

```cpp
// Before
auto bg = node_vs(node).hovered ? Color::rgba(55, 55, 68) : Color::rgba(45, 45, 55);

// After
auto& t = *node.theme;
auto bg = node_vs(node).hovered ? t.surface_hover : t.surface;
```

No delegate signature changes. `record(DrawList&, const Field<T>&, const WidgetNode&)` stays the same.

### Window Chrome

`WindowChrome::render()` currently hardcodes colors. It gains a `const Theme&` parameter:

```cpp
// Before
static void render(DrawList& dl, int window_w, std::string_view title, TTF_Font* font);

// After
static void render(DrawList& dl, int window_w, std::string_view title, const Theme& theme);
```

The caller (SdlBackend) passes the theme from the window's associated widget tree (or a default theme for standalone windows).

### SDL Backend Clear Color

`SdlWindow::render_snapshot()` currently hardcodes `SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 255)`. It gains a `const Theme&` parameter (or the color is passed directly) to use `theme.canvas_bg`.

### WidgetTree Ownership

```cpp
class WidgetTree {
    Theme theme_;           // owned, default-constructed = dark palette
    WidgetNode root_;
    // ...
public:
    const Theme& theme() const { return theme_; }
    void set_theme(Theme t) { theme_ = t; mark_all_dirty(); }
};
```

`set_theme()` + `mark_all_dirty()` enables future runtime theme switching. Not wired to any UI in this phase.

## Color Mapping

Every hardcoded `Color::rgba(...)` maps to a Theme field. The mapping was derived from an audit of all delegate files:

| Widget | Element | State | Theme field |
|--------|---------|-------|-------------|
| **All widgets** | Focus ring | focused | `focus_ring` |
| **Button** | Background | normal | `primary` |
| | Background | hovered | `primary_hover` |
| | Background | pressed | `primary_active` |
| | Outline | all | `border_hover` → actually uses rgba(60,140,220). This is close to `focus_ring` but distinct. Add `primary_outline = Color::rgba(60, 140, 220)` |
| | Text | all | `text_on_primary` |
| **Checkbox** | Box (checked) | normal/hovered/pressed | `primary` / `primary_hover` / `primary_active` |
| | Box (unchecked) | normal/hovered/pressed | `surface` / `surface_hover` / `surface_active` |
| | Box outline | normal/hovered | `border` / `border_hover` |
| | Widget bg | normal/hovered | `surface` / `surface_hover` |
| | Check mark | all | `text_on_primary` |
| | Label text | all | `text` |
| **Label** | Background | all | `surface` (actually rgba(40,40,48) — slightly different. Use `surface` and accept the tiny shift, or add `label_bg`) |
| | Text | all | `text_muted` |
| **Slider** | Track | normal/hovered | `track` / `track_hover` |
| | Thumb | normal/hovered/pressed | `primary` (actually rgba(0,140,200) — slider uses a different blue. Add `slider_thumb` / `slider_thumb_hover` / `slider_thumb_active`, OR map to primary and accept the shift) |
| **TextField/Password/TextArea** | Background | normal/hovered/focused | `surface` / `surface_hover` / `surface_active` |
| | Text | all | `text` |
| | Placeholder | all | `text_placeholder` |
| | Cursor | all | `cursor` |
| **Dropdown** | Button bg | normal/hovered/open | `surface` / `surface_hover` / `surface_active` |
| | Arrow | all | `text_muted` (actually rgba(160,160,170) — close enough) |
| | Label | all | `text` |
| | Popup bg | all | `popup_bg` |
| | Popup border | all | `popup_border` |
| | Highlight | selected | `popup_highlight` |
| | Option text | normal/highlighted | `text` / `text_on_primary` |
| **Table** | Row bg | even/odd/selected | `table_row_even` / `table_row_odd` / `table_selected` |
| | Row text | all | `text` (actually rgba(200,200,220) — close to `text`) |
| | Header bg | all | `table_header` |
| | Header text | all | `table_header_text` |
| | Divider | all | `table_divider` |
| | Header divider | all | `table_header_divider` |
| **Tabs** | Bar bg | all | `tab_bar_bg` |
| | Tab bg | active/hovered/normal | `tab_active_bg` / `surface_hover` / `tab_bar_bg` |
| | Tab text | active/normal | `tab_text_active` / `tab_text` |
| | Accent underline | active | `tab_accent` |
| **Chrome** | Title bar bg | all | `chrome_bg` |
| | Bottom border | all | `chrome_border` |
| | Title text | all | `chrome_text` |
| | Button icons | all | `chrome_icon` |
| | Close button bg | all | `chrome_close` |
| **Scrollbar** | Thumb | all | `scrollbar_thumb` |
| **Canvas** | Clear color | all | `canvas_bg` |

### Discrepancies

Some widgets use slightly different shades that don't perfectly match the shared palette:

1. **Button outline** — rgba(60,140,220) vs `focus_ring` rgba(80,160,240). Add `primary_outline`.
2. **Label bg** — rgba(40,40,48) vs `surface` rgba(45,45,55). Accept the shift — label bg becomes identical to other surfaces. This is actually an improvement in consistency.
3. **Slider thumb** — rgba(0,140,200) / rgba(0,160,220) / rgba(0,120,180) vs `primary` rgba(40,105,180). These are notably different. Add `accent` / `accent_hover` / `accent_active` for controls that use a purer blue, and map slider thumb + checked checkbox to these.

Revised Theme additions:

```cpp
    // Accent (slider thumbs, checked checkboxes — purer blue than primary)
    Color accent          = Color::rgba(0, 140, 200);
    Color accent_hover    = Color::rgba(0, 160, 220);
    Color accent_active   = Color::rgba(0, 120, 180);

    // Button-specific
    Color primary_outline = Color::rgba(60, 140, 220);
```

Checked checkbox maps to `accent*` instead of `primary*`. Slider thumb maps to `accent*`. Button stays on `primary*`.

## Files Modified

- `include/prism/core/context.hpp` — populate `Theme` struct, add `default_theme()`
- `include/prism/core/widget_node.hpp` — add `const Theme* theme` to `WidgetNode`
- `include/prism/core/widget_tree.hpp` — `WidgetTree` owns `Theme`, propagates to nodes
- `include/prism/core/delegate.hpp` — replace all hardcoded colors
- `include/prism/core/text_delegates.hpp` — replace all hardcoded colors
- `include/prism/core/dropdown_delegates.hpp` — replace all hardcoded colors
- `include/prism/core/tabs_delegates.hpp` — replace all hardcoded colors
- `include/prism/core/window_chrome.hpp` — `render()` takes `const Theme&`
- `include/prism/core/layout.hpp` — scrollbar thumb color from theme
- `include/prism/core/widget_tree.hpp` — table cell colors from theme
- `src/backends/sdl_window.cpp` — clear color from theme
- `src/backends/software_backend.cpp` — pass theme to chrome render

## Future Path

1. **Per-widget style structs** — `ButtonStyle`, `SliderStyle`, etc. as Theme members, populated from the flat palette by default
2. **Per-instance overrides** — `std::optional<ButtonStyle>` on sentinel types, delegate resolves `field.get().style.value_or(theme.button)`
3. **Light theme** — just a different `Theme` instance with light values
4. **Runtime switching** — `WidgetTree::set_theme()` + mark all dirty (plumbing already in place)
