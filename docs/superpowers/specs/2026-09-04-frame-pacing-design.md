# Frame Pacing for the SDL Backend — Design

**Date:** 2026-09-04
**Context:** First item of the performance thread from `doc/review-2026-08-28.md` ("Frame
pacing is missing from the report as a work item. The render path is event-rate-bound, so
'60 FPS' currently means 'however fast SDL delivers events.'"). Prerequisite for the perf
lab (next item), whose FPS numbers are meaningless until presents have a defined cadence.

## Problem

`SoftwareBackend::run()` (`src/backends/software_backend.cpp:187-361`) blocks in
`SDL_WaitEvent`, drains the event batch, then re-renders **every** window's latest snapshot
— whether or not the snapshot changed since the last batch. Present cadence is the SDL
event delivery rate: there is no vsync, no frame clock, no upper bound under a high-rate
publish stream, and redundant presents of unchanged snapshots on every input event.

## Goals

- Present cadence decoupled from event/publish rate: at most one present per window per
  display refresh interval.
- No presents of unchanged snapshots.
- Input→logic-thread latency unchanged: event dispatch stays per-batch, unpaced. Only
  presents are paced.
- Zero CPU when idle (no polling timer when nothing is dirty).
- Default-on for all `SoftwareBackend` users, with a config opt-out.

## Non-goals

- FPS/present-time measurement surface (that is the perf lab's job, next work item).
- Any change to headless/test backends, the app/logic thread, or the publish path.
- Vsync-perfect tear elimination guarantees beyond what SDL's renderer vsync gives for free.

## Design

### Config (`include/prism/app/backend.hpp`, `RenderConfig`)

Two new fields:

```cpp
bool frame_pacing = true;   // false reproduces today's event-rate-bound behavior
float target_fps = 0;       // 0 = follow display refresh rate; >0 = fixed cap
```

### `FramePacer` — pure, SDL-free helper (`include/prism/backends/frame_pacer.hpp`)

Owns a frame interval and the next frame deadline. API:

- `FramePacer(std::chrono::duration<double> frame_interval)`
- `bool frame_due(time_point now) const`
- `milliseconds ms_until_next_frame(time_point now) const` — 0 when a frame is due
- `void mark_presented(time_point now)` — advances the deadline by one interval; if the
  loop stalled past the deadline (overrun, breakpoint, system sleep), clamps to
  `now + interval` instead of burst-catching-up on missed frames.

No SDL, no atomics, single-threaded (render thread only). Unit-testable per the project
convention of testing pure logic directly.

### Run loop (`src/backends/software_backend.cpp`)

- `SDL_WaitEvent` → `SDL_WaitEventTimeout(&ev, timeout)` where `timeout` is
  `ms_until_next_frame(now)` when any window has an unpresented snapshot, and infinite
  (`SDL_WaitEvent`) when nothing is dirty. Publishes still wake the loop via the existing
  `wake()` user event.
- After each event batch: dispatch to the logic thread as today (unpaced), then, if
  `frame_due(now)`, present dirty windows and `mark_presented(now)`.
- Frame interval resolved once at `run()` start: `target_fps > 0` → `1/target_fps`;
  otherwise the display rate of the first window's display via `SDL_GetDisplayForWindow` +
  `SDL_GetCurrentDisplayMode`; 60 Hz fallback if the query fails or returns 0.
- Per-window dirty tracking: remember the last-presented
  `std::shared_ptr<const SceneSnapshot>` per `WindowId`; present only when the slot's
  current pointer differs. This change applies even when `frame_pacing == false`
  (re-presenting an unchanged snapshot is never useful).
- `SDL_EVENT_WINDOW_EXPOSED` (un-minimize, de-occlusion) force-marks its window dirty —
  SDL needs a redraw there even with an unchanged snapshot.
- `SDL_SetRenderVSync(renderer, 1)` at renderer creation when pacing is on; failure is
  ignored (frame clock alone still paces).

### App thread

Unchanged. `publish_entry` still does `submit()` + `wake()` per publish. A 1 kHz publish
stream now collapses to ≤ refresh-rate presents because `submit()` overwrites the slot and
only frame boundaries present.

## Error handling

- Display refresh query failure or 0 Hz → 60 Hz fallback.
- `SDL_SetRenderVSync` failure → ignored, frame clock still applies.
- Frame overrun / stall → deadline clamp (no catch-up bursts), handled inside `FramePacer`.

## Testing

- New `tests/test_frame_pacer.cpp` (doctest, registered in `tests/meson.build`'s
  `headless_tests`): frame interval math, `frame_due`/`ms_until_next_frame` before/at/past
  the deadline, stall clamp, `target_fps` override resolution if factored into a pure
  function.
- Existing suite must stay green — in particular `tests/test_software_backend_*.cpp` (check
  none depend on redundant re-presents of unchanged snapshots) and
  `benchmarks/bench_stall_latency` (headless backend, unaffected).
- Real display-refresh behavior is verified by running an example (e.g.
  `model_system_monitor`) and, later, by the perf lab's FPS overlay.

## Files touched

- `include/prism/app/backend.hpp` — `RenderConfig` fields.
- `include/prism/backends/frame_pacer.hpp` — new.
- `src/backends/software_backend.cpp` — run loop, dirty tracking, vsync call.
- `include/prism/backends/software_backend.hpp` — new members (pacer, last-presented map).
- `src/backends/sdl_window.cpp` — `SDL_SetRenderVSync` right after the
  `SDL_CreateRenderer` call at line 99.
- `tests/test_frame_pacer.cpp` + `tests/meson.build` — new test.
- `doc/design/render-backend.md` — one paragraph on present cadence (it documents the
  backend contract; keep it current per project convention).
