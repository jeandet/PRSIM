# Scroll Area — Design Spec

## Summary

Add a general-purpose scroll container to PRISM. A scroll node is a layout container that lays out children in an unbounded virtual content region, clips rendering to a viewport, and offsets draw commands by the current scroll position. Vertical scrolling ships first; the design accommodates horizontal and 2D scrolling for later.

## Motivation

Any non-trivial UI needs scrollable regions — settings panels with many fields, data lists, canvas viewports. Without scroll areas, content that exceeds the window is simply clipped with no way to reach it. Scroll areas are also the foundation for virtual lists and data tables (Phase 4 targets).

## Data Types

All scroll-related values use strong `Scalar<Tag>` coordinate types. No raw floats.

### Enums

```cpp
enum class ScrollBarPolicy : uint8_t { Auto, Always, Never };
enum class ScrollEventPolicy : uint8_t { ConsumeAlways, BubbleAtBounds };
```

`ScrollBarPolicy` is designed for three modes but only `Auto` (overlay, show on scroll/hover) is implemented in v1. `Always` and `Never` are reserved.

`ScrollEventPolicy` controls nested scroll behavior:
- `ConsumeAlways`: innermost scroll area eats the event even at bounds
- `BubbleAtBounds`: if the innermost scroll area is at its limit, the event propagates to the parent scroll area

### ScrollState (internal)

Stored in `WidgetNode::edit_state`, not user-facing:

```cpp
struct ScrollState {
    DY offset_y{0};         // how far content is scrolled (positive = down)
    DX offset_x{0};         // horizontal offset (future)
    Height content_h{0};    // total content height from sub-layout
    Width content_w{0};     // total content width (future)
    Height viewport_h{0};   // allocated viewport height
    Width viewport_w{0};    // allocated viewport width
    ScrollBarPolicy scrollbar = ScrollBarPolicy::Auto;
    ScrollEventPolicy event_policy = ScrollEventPolicy::BubbleAtBounds;
    uint8_t show_ticks = 0; // scrollbar visibility countdown (frames)
};
```

### ScrollArea sentinel (user-facing)

```cpp
struct ScrollArea {
    ScrollBarPolicy scrollbar = ScrollBarPolicy::Auto;
    ScrollEventPolicy event_policy = ScrollEventPolicy::BubbleAtBounds;
    DY scroll_y{0};    // observable scroll position (positive = scrolled down)
    DX scroll_x{0};    // horizontal (future)
};
```

When used via `Field<ScrollArea>`, the user can observe scroll position changes and programmatically set scroll position.

## Node & Layout Integration

### New LayoutKind

```cpp
enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas, Scroll };
```

### Node metadata for scroll containers

`Node` gains optional scroll metadata:

```cpp
struct Node {
    // ... existing fields ...

    // Scroll container metadata (only set for LayoutKind::Scroll nodes)
    ScrollBarPolicy scroll_bar_policy = ScrollBarPolicy::Auto;
    ScrollEventPolicy scroll_event_policy = ScrollEventPolicy::BubbleAtBounds;
};
```

When `ViewBuilder::scroll()` is called with a `Field<ScrollArea>&`, the Node also stores a pointer to wire the field to the internal `ScrollState` during `build_widget`.

### LayoutNode::Kind::Scroll

Added to the existing `LayoutNode::Kind` enum.

### Measure phase

A scroll node reports `expand = true` to fill available space in its parent (same as Canvas). Its children are measured normally — they report their natural preferred size without any viewport constraint.

```
layout_measure(scroll_node):
    for each child: layout_measure(child, LayoutAxis::Vertical)
    // Sum children preferred heights → content_h
    // Max children cross widths → content_w
    scroll_node.hint.expand = true
    scroll_node.hint.preferred = 0  // wants to fill
```

### Arrange phase

The scroll node receives its allocated viewport rect from the parent. It then runs an internal arrange on its children with the viewport width but unbounded height:

```
layout_arrange(scroll_node, viewport_rect):
    scroll_node.allocated = viewport_rect
    content_rect = Rect{
        viewport_rect.origin,
        Size{viewport_rect.extent.w, Height{content_preferred_h}}
    }
    // Arrange children within content_rect (normal column layout)
    layout_arrange_children_as_column(scroll_node.children, content_rect)
```

### Flatten phase

During flattening, the scroll node:

1. Emits a `ClipPush` for the viewport rect
2. Translates all child draw commands by `-offset_y` (scroll offset)
3. Only emits geometry for children that intersect the viewport (foundation for virtual lists)
4. Emits a `ClipPop`
5. Draws the scrollbar into `overlay_draws`

```
layout_flatten(scroll_node, snap):
    emit ClipPush(viewport_rect)
    for each child:
        if child.allocated intersects viewport (accounting for scroll offset):
            translate child draws by (viewport_origin.x, viewport_origin.y - offset_y)
            emit geometry clipped to viewport
    emit ClipPop
    emit scrollbar overlay draws
```

### Scroll offset storage during layout

The layout pass needs access to `ScrollState` to know the current `offset_y`. Since `LayoutNode` is a pure layout structure, the scroll offset is passed through a side-channel: the `WidgetNode::edit_state` is read during `build_layout` and stored in a new field on `LayoutNode`:

```cpp
struct LayoutNode {
    // ... existing fields ...
    DY scroll_offset{0};  // only meaningful for Kind::Scroll
};
```

This is populated during `build_layout` (in `WidgetTree::build_layout_node`), which already walks `WidgetNode` → `LayoutNode`. For scroll nodes, it reads `std::any_cast<ScrollState>(wn.edit_state).offset_y` and stores it in `ln.scroll_offset`.

## ViewBuilder API

### Path A: Sugar (common case)

```cpp
void view(ViewBuilder& vb) {
    vb.scroll([&] {
        vb.widget(field_a);
        vb.widget(field_b);
        vb.widget(field_c);
    });
}
```

Default policies: `ScrollBarPolicy::Auto`, `ScrollEventPolicy::BubbleAtBounds`.

Overload with policy:

```cpp
vb.scroll(ScrollBarPolicy::Always, [&] { ... });
```

### Path B: Field<ScrollArea> (programmatic control)

```cpp
struct Panel {
    Field<ScrollArea> scroller{{}};
    Field<int> count{0};
    Field<int> value{0};

    void view(ViewBuilder& vb) {
        vb.scroll(scroller, [&] {
            vb.widget(count);
            vb.widget(value);
        });
    }
};
```

The user can observe scroll position:

```cpp
scroller.on_change() | prism::then([](const ScrollArea& sa) {
    // sa.scroll_y is the current vertical offset
});
```

Or programmatically scroll:

```cpp
auto sa = scroller.get();
sa.scroll_y = DY{200};
scroller.set(sa);
```

### ViewBuilder implementation

```cpp
void scroll(std::invocable auto&& fn) {
    push_scroll_container(ScrollBarPolicy::Auto, ScrollEventPolicy::BubbleAtBounds, nullptr, fn);
}

void scroll(ScrollBarPolicy policy, std::invocable auto&& fn) {
    push_scroll_container(policy, ScrollEventPolicy::BubbleAtBounds, nullptr, fn);
}

template <typename F>
void scroll(Field<ScrollArea>& field, F&& fn) {
    placed_.insert(&field);
    push_scroll_container(field.get().scrollbar, field.get().event_policy, &field, fn);
}
```

`push_scroll_container` creates a `Node` with `LayoutKind::Scroll`, stores policies in the Node, and if a `Field<ScrollArea>*` is provided, captures it for wiring during `build_widget`.

## Input Handling

### MouseScroll dispatch

`MouseScroll` events are already defined in `input_event.hpp` but not handled in `model_app`. New handler:

```cpp
if (auto* ms = std::get_if<MouseScroll>(&ev); ms && current_snap) {
    auto id = hit_test(*current_snap, ms->position);
    if (id) {
        tree.scroll_at(*id, ms->dy);
    }
}
```

### WidgetTree::scroll_at

Walks up from the target widget using a parent map to find the nearest scroll container ancestor, applies the delta, clamps, and handles bubble policy:

```cpp
void scroll_at(WidgetId target, DY delta) {
    WidgetId current = target;
    while (current != 0) {
        auto* node = lookup(current);
        if (node && node->layout_kind == LayoutKind::Scroll) {
            auto& ss = ensure_scroll_state(*node);
            DY new_offset{ss.offset_y.raw() + delta.raw()};
            DY max_offset{std::max(0.f, ss.content_h.raw() - ss.viewport_h.raw())};
            DY clamped{std::clamp(new_offset.raw(), 0.f, max_offset.raw())};

            DY consumed{clamped.raw() - ss.offset_y.raw()};
            ss.offset_y = clamped;
            ss.show_ticks = 30;
            mark_dirty(root_, current);

            // Bubble remaining delta if policy allows
            if (ss.event_policy == ScrollEventPolicy::BubbleAtBounds) {
                DY remaining{delta.raw() - consumed.raw()};
                if (std::abs(remaining.raw()) > 0.001f) {
                    current = parent_of(current);
                    delta = remaining;
                    continue;
                }
            }
            return;
        }
        current = parent_of(current);
    }
}
```

### Parent map

A `std::unordered_map<WidgetId, WidgetId>` built during `build_index`. Maps each widget to its parent. Root maps to 0.

### Keyboard scrolling

When the focused widget is inside a scroll container:
- `PageUp` / `PageDown` → scroll by viewport height
- `Home` / `End` → scroll to top / bottom (when no inner widget handles these keys)

New key constants:

```cpp
namespace keys {
    inline constexpr int32_t page_up   = 0x4000'004B;  // SDLK_PAGEUP
    inline constexpr int32_t page_down = 0x4000'004E;  // SDLK_PAGEDOWN
}
```

Keyboard scroll is dispatched via `scroll_at` with the computed delta, finding the scroll ancestor of the focused widget.

## Scrollbar Rendering

### Overlay scrollbar (v1)

Drawn in the scroll node's `overlay_draws` — rendered on top of content, does not affect layout.

**Geometry (strong types):**

```cpp
constexpr Width track_w{6};
constexpr DX track_inset{2};

Height thumb_h{
    std::max(20.f, viewport_h.raw() * (viewport_h.raw() / content_h.raw()))
};
DY thumb_y{
    offset_y.raw() * (viewport_h.raw() - thumb_h.raw())
    / (content_h.raw() - viewport_h.raw())
};
```

**Visibility:** Scrollbar is drawn when `ScrollState::show_ticks > 0` or when the scroll container is hovered. `show_ticks` is set to 30 on scroll events and decremented each frame during `build_snapshot`. No alpha animation in v1 — simple show/hide.

**When content fits viewport:** No scrollbar drawn. Scroll events pass through (nothing to consume).

## Hit Testing

Widgets inside a scroll area that are scrolled out of the viewport must not be hittable. During `layout_flatten`, only geometry entries for children whose allocated rect (after scroll offset) intersects the viewport rect are emitted into the `SceneSnapshot`. This naturally excludes invisible widgets from hit testing.

## What Changes

| File | Change |
|---|---|
| `include/prism/core/node.hpp` | Scroll metadata fields on `Node` |
| `include/prism/core/widget_tree.hpp` | `ViewBuilder::scroll()` overloads, `scroll_at()`, parent map, `ScrollState`, `ScrollArea` sentinel binding |
| `include/prism/core/layout.hpp` | `LayoutNode::Kind::Scroll`, `scroll_offset` field, measure/arrange/flatten for scroll |
| `include/prism/core/delegate.hpp` | `ScrollArea` sentinel type definition, `Delegate<ScrollArea>` specialization |
| `include/prism/core/input_event.hpp` | `keys::page_up`, `keys::page_down` |
| `include/prism/core/model_app.hpp` | `MouseScroll` event handler |
| `include/prism/core/hit_test.hpp` | Viewport clip check during hit testing |
| `tests/test_scroll.cpp` | **New.** Scroll construction, layout, input, dirty tracking, bubble policy |
| `tests/meson.build` | Add `test_scroll.cpp` |

## What Stays Unchanged

- `Field<T>`, `State<T>`, `Connection`, `SenderHub` — untouched
- All existing `Delegate<T>` specializations — untouched
- `DrawList`, `SceneSnapshot`, all backends — untouched
- stdexec integration — untouched
- Canvas escape hatch — untouched
- Node tree construction pipeline — extended, not replaced
- `build_widget_node`, `connect_dirty` — extended to handle scroll nodes

## Testing

### test_scroll.cpp

1. **Scroll node creation** — `vb.scroll([&]{...})` produces a Node with `LayoutKind::Scroll` and correct children
2. **Layout** — scroll container gets viewport-sized allocation, children get unbounded content height
3. **Content clipping** — widgets outside viewport are excluded from snapshot geometry
4. **Scroll delta** — `scroll_at()` updates offset, clamps to bounds
5. **Dirty tracking** — scrolling marks the scroll container dirty
6. **Bubble at bounds** — nested scroll areas: inner at limit bubbles to outer
7. **ConsumeAlways** — inner consumes even at limit
8. **Field<ScrollArea> binding** — programmatic scroll position, observe changes
9. **Scrollbar visibility** — `show_ticks` countdown, visible on hover
10. **Keyboard scroll** — PageUp/PageDown via scroll ancestor of focused widget
11. **No-scroll passthrough** — content fits viewport, scroll events not consumed

## Out of Scope

- Horizontal scrolling / 2D panning — designed for, not implemented
- `ScrollBarPolicy::Always` / `ScrollBarPolicy::Never` — enum exists, not wired
- Scrollbar thumb drag interaction
- Kinetic / momentum scrolling
- `FitContent(max)` size policy (Expand only in v1)
- Virtual list optimization (only render visible items) — the clip check in flatten is the foundation
- Smooth scroll animation
