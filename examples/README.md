# Examples

Each subfolder is a standalone executable built alongside the library. They're ordered here
roughly by how much of PRISM they exercise.

| Example | Entry point | Demonstrates |
|---|---|---|
| [`hello_rect/`](hello_rect/) | Retained layout (`prism::app<State>`) | Manual `row()`/`column()`/`spacer()` composition, keyboard-driven state |
| [`model_plot/`](model_plot/) | Model-driven (`prism::model_app`) | `PlotModel`, sliders driving a live chart via `.observe()` |
| [`model_tree_browser/`](model_tree_browser/) | Model-driven | Tree widget over a hand-written `TreeStorage` filesystem adapter |
| [`model_dashboard/`](model_dashboard/) | Model-driven | Tabs, table, canvas escape hatch, animation, SVG export — the fullest tour of the widget set |
| [`model_system_monitor/`](model_system_monitor/) | Model-driven | Background-thread data ingestion via `Shared<T>`, live plots, sortable table/tree over real `/proc` data |

`showcase/` is a separate, internal set of minimal snippets used only to auto-generate the
screenshots embedded in the top-level README — not meant to be read as usage examples.

## Building and running

```bash
meson setup builddir
ninja -C builddir
./builddir/examples/model_dashboard/model_dashboard
```

Swap in any example's directory name to run a different one. See the root
[README](../README.md#building) for full build prerequisites.
