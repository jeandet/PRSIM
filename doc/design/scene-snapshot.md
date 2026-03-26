# Scene Snapshot

## Overview

The `SceneSnapshot` is the central data structure in PRISM's pipeline. It is a complete, immutable, plain-data description of what should be on screen at a given moment. The render thread only ever reads snapshots — it never modifies them.

## Structure

```cpp
struct SceneSnapshot {
    uint64_t                               version;
    std::vector<std::pair<WidgetId, Rect>> geometry;    // resolved layout
    std::vector<DrawList>                  draw_lists;   // parallel to geometry
    std::vector<uint16_t>                  z_order;      // indices into geometry/draw_lists, back-to-front
};
```

All fields are plain data. No pointers to live objects, no callbacks, no vtables. Safe to read from any thread without synchronisation.

`geometry`, `draw_lists`, and `z_order` are parallel vectors — `z_order[i]` is an index into both `geometry` and `draw_lists`.

## Versioning

Each snapshot carries a monotonically increasing `version` number. This serves as:

- Frame correlation ID in traces (link app-side publish to render-side consume).
- Cache key for memoised layout (same version → same geometry → skip re-layout).
- Debugging aid — log which version is being rendered vs published.

## Full Replacement Model

The primary (and currently only) update path is full snapshot replacement via `atomic_cell`. The application builds a complete new snapshot and publishes it. The render thread picks up the latest.

Incremental diffing may be added later as an optimisation when profiling shows snapshot rebuilds are a bottleneck. For now, full replacement keeps the model simple and the invariants easy to verify.

## POC Status

`SceneSnapshot` is implemented and used end-to-end. In the POC, `Frame::take_snapshot()` builds a single-entry snapshot (one geometry rect covering the full viewport, one draw list, `z_order = {0}`). Per-widget geometry will be used when the layout engine (Phase 2) produces widget-level rects.

## Open Questions

- Should `SceneSnapshot` own its draw lists by value or by shared_ptr for cheaper copies?
- Snapshot arena/pool allocator to avoid per-frame heap churn.
- Clip hierarchy representation — flat list of clip push/pop in DrawList, or a separate clip tree?
