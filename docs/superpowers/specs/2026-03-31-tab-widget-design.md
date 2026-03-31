# Tab Widget Design

## Overview

A tab widget for PRISM that supports two modes: explicit (lambda-based tab definitions) and reflective (struct reflection auto-generates tabs from members). Horizontal top-positioned tab bar, static tabs (no close/reorder), lazy content materialization.

## Model & API

### TabBar sentinel (explicit mode)

```cpp
struct TabBar {
    size_t selected = 0;
};
```

The selected index is the only model state. Tab names and content are defined in `view()`.

### TabBar\<S\> sentinel (reflective mode)

```cpp
template <typename S>
struct TabBar {
    size_t selected = 0;
    S pages;
};
```

`S` is a struct whose members are sub-components (structs with their own `view()` methods). Reflection provides tab names from member names, and each member's `view()` builds that tab's content.

### Explicit usage

```cpp
struct Settings {
    Field<TabBar> tabs;
    Field<std::string> username{"jean"};
    Field<bool> dark_mode{false};

    void view(ViewBuilder& vb) {
        vb.tabs(tabs, [&] {
            vb.tab("General", [&] { vb.widget(username); });
            vb.tab("Advanced", [&] { vb.widget(dark_mode); });
        });
    }
};
```

Tab names are declared once, at the point of content definition. No duplication with the `TabBar` field.

### Reflective usage

```cpp
struct GeneralPage {
    Field<std::string> username{"jean"};
    void view(ViewBuilder& vb) { vb.widget(username); }
};

struct AdvancedPage {
    Field<bool> dark_mode{false};
    void view(ViewBuilder& vb) { vb.widget(dark_mode); }
};

struct Pages {
    GeneralPage general;
    AdvancedPage advanced;
};

struct Settings {
    Field<TabBar<Pages>> tabs;
};
```

Reflection over `Pages` produces two tabs: "general" and "advanced", each rendering the corresponding sub-component.

## Node Tree & LayoutKind

### New LayoutKind

```cpp
enum class LayoutKind { Leaf, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table, Tabs };
```

### Node structure

A Tabs node has two children:

```
Node(Tabs)
├── Node(Leaf)   — tab bar widget (clickable headers, focusable)
└── Node(Column) — content container (only active tab's children materialized)
```

### Tab metadata

```cpp
struct TabsMetadata {
    size_t tab_count;
    std::vector<std::string> tab_names;
    std::vector<std::function<void(ViewBuilder&)>> tab_builders;
};
```

Stored on the Tabs node. Each `tab_builders[i]` is either the explicit lambda from `vb.tab()` or a generated lambda calling `vb.component(pages.member_i)` for reflective mode.

### Content materialization

Only the active tab's subtree exists in the widget tree. On tab switch:

1. Content container's children are cleared (connections torn down)
2. Active tab's builder is re-invoked to produce new children
3. New subtree marked dirty, recorded, and laid out in the same frame

No blank frame — the switch is atomic within a single `build_snapshot` cycle.

Model-level `Field<T>` observers continue to fire for inactive tabs since the model structs persist independently of the widget tree. Changes to inactive fields are reflected when the user switches back (fresh build picks up current values).

## Layout

### Measurement

- **Tab bar**: measured as a Row. Each header item has text-based preferred width and fixed height (one text line + padding).
- **Content area**: measured as a Column containing the active tab's children.
- **Tabs node**: preferred height = tab bar height + content preferred height. Preferred width = max of tab bar and content widths.

### Arrangement

- Tab bar: gets its preferred height at the top, full available width.
- Content area: gets remaining height below, full available width.
- Content area arranges its children as a normal Column.

### Expand behavior

The Tabs node is expandable (like Canvas/Scroll) — it fills available space. The content area is expandable within the Tabs node.

## Input & Focus

### Tab bar

- `FocusPolicy::tab_and_click` — enters focus chain via Tab key or mouse click.
- When focused: Left/Right arrows switch `selected` index, wrapping at edges.
- Click on a specific tab header: hit test by x-position within the bar, switch to clicked tab.

### Focus flow

- Tab from tab bar → first focusable widget in active content.
- Tab from last widget in content → next widget after the Tabs node in parent.
- Shift+Tab reverses.

### On tab switch

1. `field.set(TabBar{.selected = new_index})` triggers change notification.
2. Content container children cleared, new tab's builder invoked.
3. New subtree marked dirty, rendered same frame.
4. Focus remains on the tab bar — user can Tab into content.

## Rendering

### Delegate\<TabBar\>

Draws:
- Background bar spanning full width.
- Per-header: text label with padding, hover highlight on mouseover.
- Active tab: bottom accent line (2px, theme color).
- Focus ring when the tab bar has keyboard focus.

### EditState

```cpp
struct TabBarEditState {
    std::optional<size_t> hovered_tab;
    std::vector<std::pair<float, float>> header_x_ranges;  // per-tab [x_start, x_end]
};
```

Added to the `EditState` variant. `hovered_tab` tracks mouseover for highlight. `header_x_ranges` computed during `record()` for click-to-tab-index mapping.

### Hit rect

The tab bar's hit rect is its drawn bounding box (content-based). Individual tab header bounds are computed during `record()` and stored in `TabBarEditState` for click dispatch — similar to dropdown option bounds.

### Content area

No special rendering — it's a plain Column container. Children render themselves normally via their own delegates.

## ViewBuilder API

### New methods

```cpp
// Explicit mode
template <typename F>
void tabs(Field<TabBar>& field, F&& builder);

void tab(std::string_view name, auto&& content_builder);

// Reflective mode
template <typename S>
void tabs(Field<TabBar<S>>& field);
```

`tabs()` creates the Tabs node and tab bar leaf, then invokes the builder lambda which calls `tab()` for each tab. In reflective mode, `tabs()` walks `S` via reflection and generates the tab entries automatically.

## Testing

- Tab switch changes visible content (verify draw commands).
- Keyboard: Left/Right on focused tab bar switches tabs.
- Keyboard: Tab from bar enters content, Shift+Tab returns.
- Click on header switches tab.
- Inactive tab field changes reflected on switch back.
- Reflective mode: correct tab count, names, and content from struct.
- Layout: tab bar gets measured height, content fills remainder.
- No flicker: switch produces valid snapshot in single frame.
