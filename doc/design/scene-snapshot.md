# Scene Snapshot

## Overview

The `SceneSnapshot` is the central data structure in PRISM's pipeline. It is a complete, immutable, plain-data description of what should be on screen at a given moment. The render thread only ever reads snapshots — it never modifies them.

## Structure

```cpp
struct SceneSnapshot {
    uint64_t                               version;
    std::vector<std::pair<WidgetId, Rect>> geometry;    // resolved layout
    std::vector<DrawList>                  draw_lists;   // one per widget
    std::vector<uint8_t>                   z_order;
};
```

All fields are plain data. No pointers to live objects, no callbacks, no vtables. Safe to read from any thread without synchronisation.

## Versioning

Each snapshot carries a monotonically increasing `version` number. This serves as:

- Frame correlation ID in traces (link app-side publish to render-side consume).
- Cache key for memoised layout (same version → same geometry → skip re-layout).
- Debugging aid — log which version is being rendered vs published.

## Diffing

Full snapshot rebuilds are the primary path. For incremental updates, `SceneDiff` entries patch a snapshot in-place on the render thread:

```cpp
struct SceneDiff {
    WidgetId  widget_id;
    Property  property;   // Opacity, Transform, Visibility
    float     value;
};
```

Diffs are intentionally limited to properties that don't affect layout (opacity, transform, visibility). Layout-affecting changes require a new snapshot.

## Open Questions

- Should `SceneSnapshot` own its draw lists by value or by shared_ptr for cheaper copies?
- Snapshot arena/pool allocator to avoid per-frame heap churn.
- Should geometry and draw_lists be indexed by WidgetId (map) or by position (parallel vectors)?
- Clip hierarchy representation — flat list of clip push/pop in DrawList, or a separate clip tree?
