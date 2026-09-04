# Plot Decimation — Design

**Date:** 2026-09-04
**Context:** Perf-thread item #3, redirected by measurement. The perf lab
(`examples/perf_lab`, after the `54e5f93` pre-fill fix) shows the 100k-row virtualized
table costs ~1 ms/publish — the originally deferred table-row DrawList caching fails its
precondition and stays parked — while a 1M-point plot costs **~260 ms/publish**
(3.6 publishes/s, 7.7 MB snapshots), ~100× everything else. Root cause:
`draw_series` (`include/prism/widgets/plot_render.hpp:220-243`) maps every point through
`std::function` indirection into an 8 MB `std::vector<Point>` polyline per `record()`
(×2 with `fill`), and hands the render thread a 1M-segment polyline per present.

## Problem

A plot with many more points than pixel columns pays O(N) transform + O(N) allocation +
O(N) backend rendering for output that is visually indistinguishable from ~2 points per
pixel column. The unchanged case is already covered by the per-widget DrawList cache;
the pathological case is a *dirty* dense series (live telemetry), which re-records every
publish.

## Goals

- Dense monotonic-x series render at cost proportional to **pixel width**, not point
  count, with no visually observable difference (min-max envelope is exact).
- No API change; no behavior change below the threshold or for non-monotonic-x series
  (small plots bit-identical — golden snapshot tests and SVG screenshots must not move).
- The render thread benefits too: polyline segment count drops from N to ≤ 2×width.

## Non-goals

- `auto_fit_range` and cursor nearest-sample O(N) scans (`plot_render.hpp:101-121`,
  `:289-294`): allocation-free min/max loops, a few ms at 1M — re-measure after this
  lands; fix only if the lab says they dominate the residual.
- Incremental/cached decimation across frames — the DrawList cache already covers the
  unchanged case; the dirty case appends data and must rescan anyway.
- LTTB or other fancier downsampling — min-max per column preserves extremes exactly;
  LTTB buys nothing here.
- Table-row DrawList caching — measured non-issue, parked with the evidence above.

## Design

All changes in `include/prism/widgets/plot_render.hpp` (+ tests). Three pieces:

### 1. `series_is_monotonic_x(s)` — pure gate

Single O(N) pass over `s.x(i)`, non-decreasing check. Non-monotonic dense series
(phase plots, scatter-like) keep the existing full-fidelity path — per-column bucketing
would replace their back-and-forth line shape with an envelope, a silent visual change.

### 2. `decimated_series_points(s, map)` — pure min-max bucketing

- Bucket key: the sample's pixel column, `int(to_pixel(x(i), _).x)` relative to
  `map.left()`.
- Per column, track the pixel points with min and max y (screen space).
- Output: per non-empty column, both points, ordered by sample index; columns in
  ascending order. Output size ≤ 2 × pixel width.
- The full polyline at that density connects the same extremes the same way — the
  envelope is pixel-exact.

### 3. `draw_series` integration

Engage when `s.size() > 4 × plot_pixel_width` (K=4), `plot_pixel_width > 0`, and
`series_is_monotonic_x(s)`. Otherwise the existing path runs unchanged.

- **Polyline:** `dl.polyline(decimated_series_points(s, map), ...)`.
- **Fill:** same decimated points, each paired with its baseline counterpart in the
  strip's alternating data/baseline structure.

**Edge cases:** `s.size() < 2` — existing guard. Width 0 — full path (can't bucket).
NaN y — min/max comparisons skip NaN samples, so a NaN-only column vanishes from the
output (a gap) instead of drawing a garbage point; accepted, noted in code.

## Error handling

No new failure modes: pure functions over existing data; threshold/gate fall back to
the existing path in every doubtful case.

## Testing

- `tests/test_plot.cpp` gains cases for both pure helpers:
  - monotonic check: increasing, equal-adjacent (non-decreasing passes), decreasing,
    non-monotonic.
  - threshold: below/at threshold → existing path (point count == N); above → ≤ 2×width.
  - envelope exactness: on a sine + spike synthetic series, the decimated output
    contains the global min and max y, output columns are ascending, and every output
    point is an actual input sample's pixel point.
  - non-monotonic dense series → full path (N points).
- Existing plot tests, golden snapshot tests, and the SVG screenshot targets
  (`model_plot` N=500, `showcase_plot` N=200 — both below threshold at their widths)
  must pass **unchanged**, proving small-plot output is bit-identical.
- End-to-end verification (the whole point): re-run the perf lab —
  `perf_lab --rows 100000 --points 1000000 --rate 1000 --headless 4`. Success criteria:
  median build_time from ~260 ms to single-digit ms, publish rate from 3.6/s to
  >100/s, snapshot bytes from 7.7 MB to <500 KB. Table-heavy run must stay ~1 ms
  (no regression).

## Files touched

- `include/prism/widgets/plot_render.hpp` — the two helpers + `draw_series` branch.
- `tests/test_plot.cpp` — new TEST_CASEs.
- No other files. (Perf lab numbers are re-measured, not re-committed; a README-numbers
  update is a separate follow-up once run on real hardware.)
