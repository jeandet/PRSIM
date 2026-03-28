# Threading Model

## Overview

PRISM uses a strict separation between application threads and the render thread. They never share mutable state. Communication flows through lock-free mechanisms:

- **Atomic cell** — publishes a complete immutable `SceneSnapshot` (latest wins, intermediates dropped)
- **stdexec `run_loop`** — the application thread's event loop and scheduler. The render thread schedules input events onto the app thread via `exec::start_detached(schedule(sched) | then(...))`.

Both threads are **event-driven** — they block at OS level when idle and wake only when there is work to do.

> **Note:** The original `mpsc_queue` + `atomic_wait` mechanism for input forwarding was replaced by stdexec `run_loop` scheduling. See [stdexec-integration.md](stdexec-integration.md) for details.

## Thread Roles

```
Application thread(s)              Render thread
──────────────────────              ──────────────
  wait for input (futex)            SDL_Init + create window
  drain input queue                 SDL_WaitEvent (blocks)
  react to events                   forward events → mpsc_queue
  mutate model                      wake app thread (atomic notify)
  build SceneSnapshot               load latest SceneSnapshot
  publish via atomic_cell           rasterise
  wake render (SDL_PushEvent USER)  present
```

- **Application threads**: any number, any cadence. Own all model/business logic. Never touch rendering state.
- **Render thread**: single thread that owns the SDL window, pumps OS events, drives the frame loop. Reads the latest snapshot after handling events. Never calls into application code. Initialises SDL (`SDL_Init` must be on this thread — SDL3 requirement).
- **Thread pool** (future): render thread may dispatch tile rasterisation to a pool. Tiles are independent — no shared mutable state between jobs.

## Wake Mechanisms

| Direction | Mechanism | When |
|-----------|-----------|------|
| Render → App | `input_pending_.store(true) + notify_one()` (C++20 atomic wait/notify, kernel futex) | After pushing any InputEvent |
| App → Render | `SDL_PushEvent(SDL_EVENT_USER)` | After publishing a new snapshot |
| Shutdown | Both mechanisms + `running_ = false` | On `quit()` or `WindowClose` |

## Startup Handshake

The render thread signals `sdl_ready_` (atomic bool + notify) after `SDL_Init` and window creation. The app thread waits on it before producing the first frame. This ensures `SDL_PushEvent` is safe to call from the app thread.

## Snapshot Handoff

Via `atomic_cell<SceneSnapshot>`:

- Application builds a new snapshot, calls `cell.store(snapshot)`.
- Render thread calls `cell.load()` after handling events — gets a `shared_ptr<const SceneSnapshot>`.
- If the application publishes faster than rendering, intermediate snapshots are silently dropped.
- If the application hasn't published, the render thread re-uses the previous snapshot.

The handoff is a single atomic `shared_ptr` swap. No mutex, no condition variable.

## Input Event Flow

OS events are captured by the render thread (which owns the SDL window) and forwarded to application threads via `mpsc_queue<InputEvent>`:

- Render thread handles SDL events via `SDL_WaitEvent`, maps them to `InputEvent` values, pushes to queue, wakes app thread.
- Application thread drains the queue, performs hit testing, dispatches to handlers.
- The render thread never interprets input — it only forwards.

## Shutdown

`std::atomic<bool> running_` is shared between threads. Either side can set it to `false`:
- **App-initiated** (`quit()`): sets `running_ = false`, wakes both threads.
- **OS-initiated** (`SDL_EVENT_QUIT`): render thread sets `running_ = false`, pushes `WindowClose`, wakes app thread.

After the app loop exits, it pushes an `SDL_EVENT_USER` to ensure the render thread exits `SDL_WaitEvent`, then joins.

## Invariants

- The render thread **never blocks** on application threads.
- Application threads **never block** on the render thread (except startup handshake).
- No mutex is held across a frame boundary.
- All data crossing the thread boundary is immutable once published.
- The render thread never calls into application code.
- Both threads sleep at OS level when idle — zero CPU usage.

## Open Questions

- Snapshot pooling/recycling strategy (avoid allocation per frame).
- Thread pool integration — stdexec `static_thread_pool` or custom scheduler for parallel tile rasterisation.
- `request_redraw()` for animation-driven updates without input events.
