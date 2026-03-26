# Threading Model

## Overview

PRISM uses a strict separation between application threads and the render thread. They never share mutable state. Communication flows through two lock-free mechanisms:

1. **Atomic cell** — publishes a complete immutable `SceneSnapshot` (latest wins, intermediates dropped)
2. **MPSC queue** — streams lightweight `SceneDiff` entries for incremental updates between full snapshots

## Thread Roles

```
Application thread(s)          Render thread
──────────────────────          ──────────────
  mutate model                  consume latest SceneSnapshot
  build SceneSnapshot           drain diff queue
  publish via atomic_cell       rasterise tiles (may fan out to thread pool)
                                composite
                                present
```

- **Application threads**: any number, any cadence. Own all model/business logic. Never touch rendering state.
- **Render thread**: single thread that drives the frame loop. Reads the latest snapshot at frame start. Never calls into application code.
- **Thread pool** (future): render thread may dispatch tile rasterisation to a pool. Tiles are independent — no shared mutable state between jobs.

## Snapshot Handoff

Double-buffered via `atomic_cell<SceneSnapshot>`:

- Application builds a new snapshot, calls `cell.store(snapshot)`.
- Render thread calls `cell.load()` at frame start — gets a `shared_ptr<const SceneSnapshot>`.
- If the application publishes faster than vsync, intermediate snapshots are silently dropped.
- If the application hasn't published, the render thread re-uses the previous snapshot.

The handoff is a single atomic `shared_ptr` swap. No mutex, no condition variable.

## Diff Queue

For lightweight incremental updates (mouse hover, focus, opacity animation) that don't warrant a full snapshot rebuild:

- Producers push `SceneDiff` entries to the MPSC queue from any thread.
- The render thread drains the queue before each frame and patches the current snapshot.

## Invariants

- The render thread **never blocks** on application threads.
- Application threads **never block** on the render thread.
- No mutex is held across a frame boundary.
- All data crossing the thread boundary is immutable once published.

## Open Questions

- Snapshot pooling/recycling strategy (avoid allocation per frame).
- Diff queue backpressure — should there be a bounded queue size?
- Thread pool integration — `std::execution` scheduler vs manual pool.
