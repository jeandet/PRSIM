# Perf Lab — Design

**Date:** 2026-09-04
**Context:** Item #2 of the performance thread from `doc/review-2026-08-28.md`, its
top-ranked demo ("one app with a 100k-row table, a 1M-point plot, a synthetic 1 kHz
telemetry source, and an on-screen overlay showing snapshot age/build time/FPS… it
produces the README numbers the project lacks. Highest value by far."). Frame pacing
(item #1) shipped in `f2f7f54..dc657c5`; this builds on it. The lab is also the forcing
function for the deferred DrawList-sharing question (do scrolled/table-row rebuilds
dominate snapshot cost?) — every publish rebinds all visible table rows
(`widget_tree.hpp:1129-1164`) and re-records the full 1M-point polyline
(`plot_render.hpp:220-243`) by design, and the lab puts numbers on both.

## Problem

PRISM's README thesis — "frames must be guaranteed independent of business logic" — has
no public, reproducible artifact demonstrating it. Snapshot stats exist but are only
visible inside the debug inspector; there is no FPS measurement anywhere (no
backend→app present feedback); and no example stresses the expensive paths
(1M-point plot `record()`, 100k-row virtualized table) at a high publish rate.

## Goals

- A standalone `perf_lab` example: 100k-row table + 1M-point plot + 1 kHz synthetic
  telemetry + live stats bar (present FPS, present duration, snapshot build time,
  dirty count, draw commands, bytes, snapshot age).
- `--headless N` mode: run N seconds with no window, print a stats summary table —
  reproducible numbers for the README and CI.
- Minimal, general framework additions that other apps/demos can reuse.

## Non-goals

- README update with measured numbers — follow-up after a real-hardware run.
- Plot decimation/downsampling or table-cell diffing — the lab exists to *measure*
  these costs, not fix them. Fixes are later work items informed by the lab's numbers.
- A true overlay-draw API — the stats bar is a normal widget row (YAGNI).
- Python bindings for the new APIs (C++-only lab; bindings can follow if wanted).

## Design

### Framework addition 1: present statistics (`BackendBase::present_stats`)

New struct in `include/prism/app/backend.hpp`:

```cpp
struct PresentStats {
    uint64_t present_count;                               // since run() start
    std::chrono::steady_clock::time_point last_present_at;
    double last_present_ms;                               // duration of last render+present
};
```

`BackendBase` gains `virtual std::optional<PresentStats> present_stats(WindowId) const
{ return std::nullopt; }`; `Backend` forwards it. Pull-based (no cross-thread callback
hazards); backends that don't present (Null/Test/Capturing) keep the default `nullopt`.

`SoftwareBackend` records three atomics per window in its `WindowSnapshot` slot
(`present_count`, `last_present_ns` as `atomic<int64_t>`, `last_present_ms` as
`atomic<double>`), written in the `run()` present block around
`win->render_snapshot(...)` (`src/backends/software_backend.cpp` present loop).
`present_stats(id)` assembles `PresentStats` from them under `windows_mutex_`.

### Framework addition 2: `SceneSnapshot::built_at`

`std::chrono::steady_clock::time_point built_at` on `SceneSnapshot`
(`include/prism/render/scene_snapshot.hpp`), set at the end of
`WidgetTree::build_snapshot()` next to the existing stats. Enables snapshot age
(`now - built_at`) in the overlay and publish-interval math in headless mode.

### The example: `examples/perf_lab/`

Follows the `model_system_monitor` pattern (own `meson.build`, registered in
`examples/meson.build`, pure logic in a separate header with its own test file).

**Pure core — `examples/perf_lab/lab_model.hpp`** (no PRISM app deps, unit-tested):

- `RingBuffer` — fixed-capacity (default 1M) `double` ring with cursor; `push(v)`,
  `size()`, `operator[](i)` oldest-first. Backs the plot's `PlotSource`.
- `TelemetryGenerator` — deterministic synthetic waveform (seeded: sum of sines +
  pseudo-random component), `next(t)` per tick; also owns the 100k-row table model
  (id, name, value, rate, status columns as `std::vector`s — Soa shape) and mutates a
  rotating slice (~1000 rows) per tick so the table visibly changes.
- `LabStats` aggregator — windowed FPS from `PresentStats.present_count` deltas over
  wall time; summary computation (count, min/median/p95/max) over collected
  build-time samples for the headless report.

**App — `examples/perf_lab/main.cpp`:**

- Model: `Shared<TelemetrySample> telemetry` (coalescing cross-thread ingest — the
  documented semantics), `PlotModel plot`, SOA table storage, stats-bar fields.
- Producer: one `std::jthread` at the configured rate (default 1 kHz),
  `telemetry.set(...)` per tick (same pattern as `proc_metrics.hpp:313-329`).
- Logic thread: `observe(telemetry)` → push to ring buffer, mutate table slice, bump
  plot revision → dirty → publish. This is the deliberately expensive path.
- Layout: table (left, `Scroll`+`Table` via `wrap_soa_columns`) | plot canvas (right),
  stats bar (bottom row of read-only fields).
- Stats bar: a ~2 Hz tick (perpetual `AnimationClock`, system-monitor pattern) reads
  `ctx.backend().present_stats(ctx.window().id())` (AppContext already exposes both,
  `model_app.hpp:65-66`) and the registry's `current_snap` stats, formats
  one line: `FPS 59.9 · present 2.1ms · build 34.2ms · dirty 87 · cmds 1.2k · 18.4 MB
  · age 41ms`. 2 Hz is fast enough to read, slow enough not to dominate the dirty
  counts it reports.
- CLI: `--rows N` (100000), `--points N` (1000000), `--rate HZ` (1000),
  `--headless SECONDS` (absent = interactive). No library; hand-rolled argv scan.

**Headless mode — `examples/perf_lab/headless_lab_backend.hpp`:**

Small example-local `BackendBase` (precedent: `tests/late_event_backend.hpp`,
python's `DelayHeadlessBackend`): `run()` blocks until the deadline or a close
request; `submit()` counts publishes and samples each snapshot's stats
(`build_time_ms`, `dirty_widget_count`, `draw_command_count`, `approx_bytes`,
`built_at`) into a vector. `present_stats` stays `nullopt` — headless reports publish
rate, not FPS. After `model_app` returns, `main` prints the summary: wall time,
publish count/rate, build-time min/median/p95/max, and last-frame
dirty/cmds/bytes — computed by the pure `LabStats` summary functions.

### Data flow (interactive)

```
producer jthread --Shared<TelemetrySample>(1 kHz, coalescing)--> logic thread
logic thread: drain -> observe -> ring push + table slice + plot revision -> publish
  (expensive: 1M-point record() + visible-row table rebind — the measured workload)
render thread: paced presents (display rate) -> present_stats atomics
stats tick (2 Hz): present_stats() + current_snap -> stats-bar fields -> publish
```

## Error handling

- `present_stats` on an unknown/headless window → `nullopt`; stats bar shows `—` for
  FPS/present fields.
- Invalid CLI values (non-numeric, negative) → usage message, exit 2.
- Producer thread uses `stop_token` + 1 ms sleep; shutdown lingers at most one tick
  (same accepted caveat as system monitor).

## Testing

- `tests/test_perf_lab.cpp` (registered in `headless_tests` next to `proc_metrics`):
  ring-buffer wrap/ordering, generator determinism (same seed → same sequence),
  windowed FPS math, summary percentiles on known inputs.
- `tests/test_software_backend_present_stats.cpp` (new SDL-backed test, registered
  like `test_software_backend_concurrent`): submit a snapshot, wait, assert
  `present_count >= 1` and sane `last_present_ms >= 0`; also asserts `nullopt` for an
  unknown WindowId.
- `tests/test_snapshot_stats.cpp`: extend with a `built_at` assertion (set, and
  <= now).
- Full suite green; `bench_stall_latency` unaffected. Interactive lab verified by
  running it; headless mode verified by `--headless 3` printing a sane table.

## Files touched

- New: `examples/perf_lab/{main.cpp, lab_model.hpp, headless_lab_backend.hpp,
  meson.build}`, `tests/test_perf_lab.cpp`,
  `tests/test_software_backend_present_stats.cpp`.
- Modified: `include/prism/app/backend.hpp` (PresentStats + virtual + forwarder),
  `include/prism/backends/software_backend.hpp` + `src/backends/software_backend.cpp`
  (atomics + recording + `present_stats`), `include/prism/render/scene_snapshot.hpp`
  (`built_at`), `include/prism/app/widget_tree.hpp` (set `built_at`),
  `examples/meson.build` (subdir), `examples/README.md` (one line),
  `tests/meson.build` (two tests), `tests/test_snapshot_stats.cpp` (one case).
- Doc: `doc/design/render-backend.md` Present Cadence section gains one sentence on
  `present_stats` (it documents the backend contract).
