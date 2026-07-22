# System Monitor Example — Design

## Purpose

PRISM has a rich widget catalog (Tree, Table, Plot, Tabs, split panes, Inspector) but its
examples are single-widget demos. Nothing exercises them together under realistic pressure:
multiple widgets sharing data, continuous cross-thread updates, real (not synthetic) I/O.

This example is a live Linux system monitor — a "fancy htop" — built to do two things at once:

1. **Be a convincing PRISM showcase**: composes Plot + Table + Tree + TabBar + resizable
   split panes + the animation system, fed by real `/proc` data across multiple threads.
2. **Find what's broken, missing, or awkward.** Building it already surfaced one real bug
   (below) and several unsupported-but-workable gaps (noted at the end). That's the point —
   this app is a stress test, not just a demo.

Scope: read-only (no kill/renice), Linux-only (`/proc`), single window.

## Framework fix required first

`model_app`'s tick loop drives animations every frame but never calls `drain_shared()` —
that only happens from inside input-event dispatch (`model_app.hpp:184`). A `Shared<T>`
updated purely by a background thread, with no mouse/keyboard activity, will not repaint.
This contradicts the state-taxonomy design intent ("event loop calls them each tick") and
is untested end-to-end (no example or test currently drives `Shared<T>` from a real thread).

Fix: call `drain_shared()` from the same per-frame tick path that already drives animations,
not only from input dispatch. TDD per project workflow rules:

- Reproducer test first: push to a `Shared<T>` from a background thread, tick the event loop
  with **zero** synthetic input, assert the observer fires. This must fail before the fix.
- Fix the tick loop, confirm the test passes, run full suite.

This lands before any app code — the app cannot demonstrate live updates without it.

## Data model — one struct per concern, fetch to view

The guiding rule: the struct a poller thread produces is the same struct the view reads.
No parallel "raw" vs "parsed" vs "view model" types. The only place a struct changes shape
is the mechanical, 1:1 boundary where a widget requires `Field<>`/`List<>` wrapping for
reflection — never a redesign of the data.

```cpp
// Plain data, built directly by parsing /proc — no intermediate representation.
struct SystemSample {
    float cpu_percent;                    // aggregate; per-core kept but not plotted in v1
    std::vector<float> per_core_percent;
    double mem_used_mb;
    double mem_total_mb;
    double net_rx_kbps;
    double net_tx_kbps;
};

struct ProcessInfo {
    int pid;
    int ppid;
    std::string name;
    float cpu_percent;
    float mem_percent;
    long rss_kb;
    char state;
};
```

Parsing is pure functions over text, independently testable against fixture strings — no
live-system dependency in tests:

```cpp
SystemSample parse_system_sample(std::string_view stat, std::string_view meminfo,
                                  std::string_view net_dev, const SystemSample& prev);
std::vector<ProcessInfo> parse_process_list(const std::vector<ProcessInfo>& prev, double dt_seconds);
```

Both take the previous sample: per-pid and system-aggregate CPU% (and network rate) are
deltas over the poll interval, not absolute values — `prev` supplies the last cumulative
counters to diff against. Each poller loop owns its own `prev` as a local across iterations
(the imperative "keep last state, sleep, push" shell stays in the small loop function; the
diff-and-build math stays in the pure `parse_*` function).

### Two independent worker threads — the "easy multi-threading" story

No shared mutable state between threads, no locks in app code — `Shared<T>` owns the
cross-thread handoff:

```cpp
void poll_system_loop(prism::Shared<SystemSample>& out, std::stop_token stop);     // ~1s
void poll_processes_loop(prism::Shared<std::vector<ProcessInfo>>& out, std::stop_token stop); // ~1.5-2s, pricier
```

Two `std::jthread`s run these at construction, join on destruction via `stop_token`.
Different cadences are deliberate: process enumeration (stat'ing every `/proc/[pid]`) is
the expensive one and shouldn't throttle the cheap stats poll.

### UI-thread ingest — the only place structs adapt shape

On each `Shared<T>::observe()` firing (during drain):

- `SystemSample` → pushed into three small bounded histories (`History` = a capped
  `std::deque<float>`, ~120 points ≈ 2 minutes at 1s cadence). Plot panels rebuild their
  `XYData` from these each poll (matches the existing `clear_series`/`add_series`/`notify`
  pattern in `model_plot.cpp` — no incremental-append API exists yet, noted below).
- `vector<ProcessInfo>` → `sort_by(std::vector<ProcessInfo>, SortKey) -> std::vector<ProcessInfo>`
  (pure — takes and returns by value, active key from a `Dropdown`) → mirrored 1:1 into `List<ProcessRow>` (Table's reflection-friendly
  `Field<>`-wrapped twin of `ProcessInfo`, same field names/order) and into
  `build_process_tree(span<const ProcessInfo>) -> vector<TreeRow>` (pure function building
  a ppid→children index, root = pid without a live parent) for the Tree tab.

The view layer never touches `Shared<T>` or raw `/proc` text — only these UI-thread-owned,
already-shaped structures.

## UI layout

```
vstack:
  Plot: CPU %  history        (~120px)
  Plot: Memory used/total     (~120px)
  Plot: Network rx/tx         (~120px)
  vb.handle()                 — resizable divider
  Dropdown: sort by [CPU% | Mem% | PID | Name]
  TabBar:
    "Table" — flat, sorted List<ProcessRow>, no per-row detail
              (TableState::selected_row exists internally but isn't exposed — noted gap,
              not blocking; Table tab is list-only in v1)
    "Tree"  — parent/child hierarchy via a hand-written FlatProcessTreeSource
              (mirrors FileTreeSource's shape); gets its detail panel for free from the
              existing TreeController wiring
  heartbeat indicator (corner, always animating via existing Animation<T>/easing)
```

The heartbeat is not decorative — it's the responsiveness proof. Two background threads
push samples every 1-2s, and the main thread does real synchronous work each poll (sort +
tree rebuild, both plain data transforms over a few hundred elements, bounded and cheap).
If any of that ever stalled the frame loop, the heartbeat is the widget that would visibly
stutter — direct visual evidence during dogfooding, not "trust me."

## Code style

- Poller bodies are small free functions, not classes (`poll_system_loop`,
  `poll_processes_loop` above).
- Parsing, sorting, tree-building are pure functions over plain data — each independently
  unit-testable, no OOP ceremony.
- The model struct stays flat/declarative: `Field<>`/`List<>`/`Shared<>` members wired via
  reflection, minimal imperative glue in `view()`.
- Target size similar to `model_dashboard.cpp` (~330 lines), split into small focused files
  rather than one large one:
  - `examples/model_system_monitor.cpp` — model struct + `view()` + thread lifecycle
  - `examples/proc_metrics.hpp` — `SystemSample`/`ProcessInfo`, parsing, pollers, `History`
  - `examples/process_tree_source.hpp` — `FlatProcessTreeSource`, `build_process_tree`

## Testing

- Core fix: `Shared<T>` drains on the animation tick path with zero input — regression test
  in `tests/` alongside `test_shared.cpp` (must fail before the fix, per TDD workflow rule).
- `proc_metrics` parsing: fixture-string tests for `/proc/stat`, `/proc/meminfo`,
  `/proc/net/dev`, and a sample `/proc/[pid]/stat`+`status` pair — no live-system dependency.
- `sort_by`: pure-function test, all four keys.
- `build_process_tree`: hierarchy correctness including orphaned-parent handling (ppid not
  present in the current sample → reparent to a synthetic root).
- Visual sanity: one static SVG snapshot via the existing `CapturingBackend`/`to_svg()` path
  (same mechanism as the README showcases) — checks layout without needing interactive input.
- Final verification: no GUI screenshot/input tooling is available in this environment: you
  run the real SDL binary and confirm live updates, sort, tab switching, and the resize
  handle behave correctly, and that the heartbeat never stutters.

## Deferred / follow-ups (real gaps found, not solved here)

- `TableBuilder` has no header-click sort or exposed row-selection hook
  (`TableState::selected_row` is internal-only). Worked around here with an app-level
  sort `Dropdown`; a real selection hook is a future Table API improvement.
- `Plot` has no incremental "push point" API — every poll is `clear_series` + full rebuild
  + `notify()`. Fine at this data volume, but an append + auto-window API would be a real
  improvement for any live-plotting use case.
- No generic "flat list keyed by parent id → tree" helper exists; `FlatProcessTreeSource` is
  written from scratch here. Worth generalizing into the Tree adapter tier if a second
  use case shows up.
- Per-core CPU breakdown is captured in `SystemSample` but not plotted in v1 (aggregate only,
  to keep scope small) — a natural follow-up visualization (stacked/heatmap).
