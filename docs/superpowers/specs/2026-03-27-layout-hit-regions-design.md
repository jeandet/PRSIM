# Phase 2: Layout Engine + Hit Regions

## Goal

Add layout containers (`row`, `column`, `spacer`) and hit testing to PRISM, producing per-widget geometry in `SceneSnapshot`. This is the foundation for `.on_X()` event dispatch (next step). No changes to the backend interface or draw command set.

## Layout Model

Layout is a two-pass process, separate from drawing.

**Pass 1 — Measure (bottom-up):** Each node reports a `SizeHint`. Leaf nodes know their size (explicit or intrinsic). Containers aggregate children (sum along main axis, max on cross axis).

**Pass 2 — Arrange (top-down):** Each container receives its allocated `Rect`, distributes space among children, and assigns each child a `Rect`.

```cpp
struct SizeHint {
    float preferred = 0;  // desired size along layout axis
    float min = 0;
    float max = std::numeric_limits<float>::max();
    float cross = 0;      // desired size on cross axis
    bool expand = false;  // spacer-like: absorb remaining space
};
```

The layout tree is ephemeral — built during the view pass, consumed to produce geometry, then discarded. The solver is a pure function: tree in → geometry out.

### Container Semantics

- `row(children)`: left-to-right. Each child gets its preferred width; expanders share remaining space equally. Height = parent height (stretch cross-axis).
- `column(children)`: top-to-bottom. Same logic on vertical axis.
- `spacer()`: `expand = true`, zero preferred size — absorbs remaining space.

### Evolution Path

Because layout is a data-driven pass producing `(WidgetId, Rect)` pairs, the solver is replaceable without changing the widget API:

- Flex weights: `ui.flex(2, [&] { ... })` — add a `weight` field to `SizeHint`
- Min/max constraints: already in `SizeHint`, just not enforced in Phase 2 solver
- Cross-axis alignment (center, start, end): one-field addition to container nodes
- Grid layout: new container kind, same output format
- Cassowary solver: drop-in replacement producing the same flat geometry list

## Layout Tree

```cpp
struct LayoutNode {
    WidgetId id;
    SizeHint hint;
    Rect allocated;                    // filled by arrange pass
    DrawList draws;                    // commands recorded by this node
    std::vector<LayoutNode> children;  // non-empty for row/column
    enum class Kind { Leaf, Row, Column, Spacer } kind;
};
```

### View Pass Integration

During the view pass, `Ui<State>` builds a `LayoutNode` tree:

```cpp
ui.row([&] {
    ui.frame().filled_rect({0,0,200,100}, red);   // leaf: explicit size
    ui.spacer();
    ui.column([&] {
        ui.frame().filled_rect({0,0,100,40}, blue);
        ui.frame().filled_rect({0,0,100,40}, green);
    });
});
```

Inside a layout container, `frame().filled_rect()` uses **local coordinates** (origin 0,0 = top-left of the widget's allocated rect). The layout pass translates them to absolute positions. Components are position-independent.

### Leaf Node Sizing

A leaf node's `SizeHint.preferred` and `SizeHint.cross` are derived from the bounding box of its recorded draw commands. For example, `filled_rect({0,0,200,100}, red)` produces `preferred = 200, cross = 100` (in a row) or `preferred = 100, cross = 200` (in a column). If a leaf records multiple commands, the bounding box encompasses all of them.

### Backward Compatibility

`ui.frame()` without any layout container works exactly as today — single full-viewport entry in the snapshot. Layout is opt-in.

## Widget Identity

Each widget gets a `WidgetId` from a sequential counter reset each frame. This suffices for hit testing. Stable identity across frames (for `.on_X()` lookup, animation, focus) will use `source_location` + loop index in the next phase.

## Hit Regions

The layout pass produces per-widget `(WidgetId, Rect)` pairs in `SceneSnapshot::geometry`.

```cpp
std::optional<WidgetId> hit_test(const SceneSnapshot& snap, Point pos);
```

Walks `z_order` back-to-front, returns first widget whose rect contains `pos`. Pure function, testable without a window.

### Snapshot After Layout

```
geometry:    [(0, {0,0,800,600}), (1, {0,0,200,600}), (2, {200,0,600,600})]
draw_lists:  [root_bg,             sidebar_draws,       content_draws]
z_order:     [0, 1, 2]
```

Each widget that records draw commands gets its own `DrawList`, positioned by the layout pass.

## New Types (header-only, `prism/core/`)

| Type | Purpose |
|------|---------|
| `SizeHint` | Preferred/min/max size along layout axis |
| `LayoutNode` | Ephemeral tree node (kind, hint, draws, children) |
| `layout_solve()` | Two-pass solver: measure + arrange, pure function |
| `hit_test()` | Z-order scan for point-in-rect |

## Changes to Existing Types

| Type | Change |
|------|--------|
| `Ui<State>` | Gains `row()`, `column()`, `spacer()`. Internally builds `LayoutNode` tree. |
| `Ui<State>` (snapshot path) | After view pass: solve layout → flatten into per-widget geometry/draw_lists |
| `frame()` inside layout | Records into current node's `DrawList` with local coordinates |

## No Changes To

- `BackendBase`, `SoftwareBackend`, `Backend` — still receive `SceneSnapshot`
- `DrawList`, `DrawCmd` — same commands
- `App` (low-level API) — unaffected
- `update()` callback — still `(State&, const InputEvent&)`

## Tests

- **Layout solver:** row distributes width, column distributes height, spacer absorbs remainder, nested containers
- **Hit test:** point-in-rect resolution, z-order precedence, miss returns nullopt
- **Integration:** `app<State>()` with layout containers produces correct snapshot geometry (via TestBackend)

## Example

`hello_rect.cpp` rewritten to use `ui.row()` / `ui.column()` instead of manual coordinates.
