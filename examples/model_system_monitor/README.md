# model_system_monitor

A fancy-htop: two background `std::jthread`s poll `/proc` (`proc_metrics.hpp` — CPU/mem/net
totals and the process list) and publish into `Shared<SystemSample>` /
`Shared<std::vector<ProcessInfo>>`. The UI thread drains those on every tick and re-derives three
live plots (CPU/mem/net history), a sortable process table, and a process tree
(`process_tree_source.hpp`'s `FlatProcessTreeSource`, built from the same `ProcessInfo` list via
`build_process_tree_index`).

Demonstrates: `Shared<T>` as the cross-thread ingest point with an explicit `drain()` opt-in
(fields fed only through `.observe()`, never placed by `view()`), a reflected-struct table row
tier (`List<ProcessInfo>` + `replace_all`) instead of a hand-rolled diff loop, variadic
`depends_on(...)` for the plot canvases, and a `canvas()`-driven heartbeat animation clocked off
`AppContext::clock()`.

Requires reflection (`__cpp_impl_reflection`); without it, `main()` degrades to writing a stub
SVG so the build still succeeds.

## Run

```bash
ninja -C builddir examples/model_system_monitor/model_system_monitor
./builddir/examples/model_system_monitor/model_system_monitor
```

Pass an output path as `argv[1]` (as the `svg_system_monitor` custom target does) to run
headlessly and dump a single-frame SVG snapshot instead of opening a window — this is how the
screenshot in the top-level README gets regenerated.
