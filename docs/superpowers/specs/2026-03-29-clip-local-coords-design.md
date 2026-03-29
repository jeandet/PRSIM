# clip_push Implicit Local Coordinates

> **Goal:** Make `clip_push` establish a local coordinate system so that `{0,0}` inside a clip means "top-left of the clipped region." This eliminates the bug class where draw commands use widget-absolute coordinates inside a clip that starts at a non-zero offset.

## Motivation

TextField and TextArea delegates use `clip_push` to constrain text rendering to a padded sub-region. Before this change, draw commands inside the clip still used widget-absolute coordinates, meaning the caller had to manually offset every x/y by the clip origin. Forgetting this offset (or getting it wrong) crops content — a bug that is invisible in code review and only visible at runtime.

By making `clip_push` implicitly translate subsequent commands, drawing at `{0,0}` means "top-left of the clip region." The coordinate bookkeeping moves into DrawList, where it cannot be forgotten.

## New Type

```cpp
struct Size { float w, h; };
```

Added to `draw_list.hpp` alongside `Point` and `Rect`.

## API Change

```cpp
// Before
void clip_push(Rect r);

// After
void clip_push(Point origin, Size extent);
```

`Point origin` is the offset (in the current coordinate frame) where the clip region starts. `Size extent` is the width/height of the clip region. The split signature prevents accidental swapping of origin and size fields.

## DrawList Internal State

DrawList gains a private offset stack:

```cpp
std::vector<Point> origin_stack_;
```

A helper `current_offset()` returns the cumulative sum of all pushed origins (or `{0,0}` if the stack is empty).

### clip_push behavior

1. Compute absolute origin: `current_offset() + origin`
2. Push `origin` onto `origin_stack_`
3. Emit `ClipPush{{abs_x, abs_y, extent.w, extent.h}}`

### clip_pop behavior

1. Pop `origin_stack_`
2. Emit `ClipPop{}`

### Draw method behavior

All draw methods offset coordinates by `current_offset()` before appending:

- `filled_rect(Rect r, Color c)` — offsets `r.x`, `r.y`
- `rect_outline(Rect r, Color c, float t)` — offsets `r.x`, `r.y`
- `text(string s, Point p, float size, Color c)` — offsets `p.x`, `p.y`

Commands stored in the DrawList contain **absolute coordinates**. The backend receives the same command format as before and requires no changes.

## Nested Clips

Offsets compose additively. If you do:

```
clip_push({10, 10}, {100, 100});  // offset = {10,10}
clip_push({5, 5}, {80, 80});      // offset = {15,15}
text("hi", {0, 0}, ...);          // absolute = {15,15}
clip_pop();                        // offset = {10,10}
clip_pop();                        // offset = {0,0}
```

This matches the push/pop stack semantics and works like nested `<div>` elements or `QPainter::save()/translate()`.

## Delegate Migration

Only 2 call sites use `clip_push` today:

### TextField / Password (`detail::text_field_record`)

```
// Before
dl.clip_push({tf_padding, 0, text_area_w, tf_widget_h});
dl.text(display, {tf_padding - es.scroll_offset * cw, 2}, ...);
float cx = tf_padding + (es.cursor - ...) * cw - es.scroll_offset * cw;

// After
dl.clip_push({tf_padding, 0}, {text_area_w, tf_widget_h});
dl.text(display, {-es.scroll_offset * cw, 2}, ...);
float cx = (es.cursor - ...) * cw - es.scroll_offset * cw;
```

Remove `tf_padding` from text x and cursor x — the clip origin handles it.

### TextArea (`detail::text_area_record`)

```
// Before
dl.clip_push({ta_padding, ta_padding, text_area_w, text_area_h});
dl.text(line_text, {ta_padding, y + 2}, ...);
float cx = ta_padding + col * cw;

// After
dl.clip_push({ta_padding, ta_padding}, {text_area_w, text_area_h});
dl.text(line_text, {0, y + 2}, ...);
float cx = col * cw;
```

Remove `ta_padding` from text x/y, cursor x, and placeholder x.

## Backend Impact

**None.** The backend (SoftwareBackend, future GPU backends) receives the same `ClipPush{Rect}` command with absolute coordinates. The `ClipPush` struct itself is unchanged — only the DrawList method signature changes.

The headless `SoftwareRenderer` (which stubs clip commands) also requires no changes.

## Testing

1. **DrawList unit tests:**
   - `clip_push` + `filled_rect` at `{0,0}` → stored rect has clip origin as absolute position
   - `clip_push` + `text` at `{0,0}` → stored text origin equals clip origin
   - Nested clips: two `clip_push` calls, draw at `{0,0}` → absolute = sum of both origins
   - `clip_pop` restores previous offset
   - No clip: draw at `{5,5}` → stored as `{5,5}` (no offset)

2. **Existing delegate tests:** Should pass unchanged — the absolute coordinates in DrawList commands should be identical before and after this refactor.

## Future: map_to_parent / map_to_global

The offset stack inside DrawList is an internal implementation detail. When nested components, scroll containers, or drag-and-drop require explicit coordinate mapping APIs (`map_to_parent`, `map_to_global`), the offset stack provides the foundation. Not needed now — will emerge naturally in Phase 4.

## Out of Scope

- Strong types for coordinates (Point vs Size vs Offset) — deferred to a follow-up
- Clip rect intersection for nested clips (SDL handles this implicitly)
- Changes to the `ClipPush` command struct (stays `{Rect}`)
