# Python Examples

Standalone runnable demos for the PRISM Python SDK (`python/prism/`). They mirror the C++ `examples/showcase/` snippets but are pure Python. All use the same MVB architecture: `prism.Model` + `ViewBuilder` trampoline, fully multi-threaded (any thread may `m.value = x`).

## Prerequisites

Built extension at `build/python/prism/_prism_ext.cpython-*.so`:

```bash
CC=clang CXX=/opt/homebrew/bin/g++-16 meson setup build --wipe \
  --force-fallback-for=fmt -Dsdl3:werror=false -Dsdl3:warning_level=0
ninja -C build
# or editable install:
# CXX=/opt/homebrew/bin/g++-16 pip install -e . --no-build-isolation \
#   --config-settings=setup-args="--force-fallback-for=fmt"
```

Needs `pydantic` only for `03_*` (validation). `pytest` for the test suite.

## Running

```bash
PYTHONPATH=build/python python python/examples/01_counter.py
PYTHONPATH=build/python python python/examples/02_mixer.py
PYTHONPATH=build/python python python/examples/03_validation_and_transaction.py
PYTHONPATH=build/python python python/examples/04_background_shared_channel.py
PYTHONPATH=build/python python python/examples/05_lists_and_derived.py
# Fancy
PYTHONPATH=build/python python python/examples/06_live_plot.py
PYTHONPATH=build/python python python/examples/07_file_tree.py
PYTHONPATH=build/python python python/examples/08_dashboard.py
```

Each opens a window (`prism.run` blocks, releases GIL). Close the window to exit. Headless smoke test (no window):

```bash
PYTHONPATH=build/python python -c "import prism; from python.examples import 01_counter as m; prism._run_headless(m.Counter())"
```

## Index

| Example | What it shows | C++ equivalent |
|---|---|---|
| `01_counter.py` | `field(int/str)` auto-stacked view, `observe` | `showcase_counter.cpp` |
| `02_mixer.py` | `slider`/`checkbox`, manual `view()` with `hstack`/`widget`, background thread mutation | `showcase_slider.cpp`, `README Mixer` |
| `03_validation_and_transaction.py` | `Annotated` + `validator_for` (pydantic), `transaction()` coalescing | `Field` + `transaction.hpp` |
| `04_background_shared_channel.py` | `Shared<T>` latest-value + `Channel<T>` lossless stream + `list_field`, any-thread set/post | `model_system_monitor` (Shared/Channel) |
| `05_lists_and_derived.py` | `list_field` `push/erase/observe_*`, `derived` over scalars | `List<T>` + `Derived<T>` |
| `06_live_plot.py` | `plot_field()` + `canvas(plot)`, live `add_series`/`notify`, sliders + thread jitter | `model_plot` / `showcase_plot` (`widgets/plot.hpp:282`) |
| `07_file_tree.py` | `tree_field(source)` + `tree(ctrl)`, `TreeStorage` Python object, lazy expansion, detail panel | `model_tree_browser` / `showcase_tree` (`ui/tree.hpp:182`) |
| `08_dashboard.py` | Plot + Tree + Shared ticker in one `vstack`/`hstack` | `model_dashboard` / `model_system_monitor` |

## Notes

- `ViewBuilder` Python now exposes `widget`, `list`, `canvas` (`BoundPlot`), `tree` (`BoundTree`), `hstack`, `vstack` (`python/src/prism_ext.cpp:1242`). Plot uses the canvas escape hatch `vb.canvas(plot).depends_on(...)` (`app/view_builder.hpp:292`); Tree uses `vb.tree(ctrl)` (`app/view_builder.hpp:375`). See `python/prism/__init__.py:plot_field`/`tree_field`.
- Tree source is any Python object implementing `root_count/root_at/child_count/child_at/label/has_children` (and optional `attributes`/`icon`) — mirrors `TreeStorage` tier 2 (`ui/tree.hpp:40`). See `07_file_tree.py:DictTreeSource` and `FsTreeSource`.
- `m.count.value` get/set uses the binding cache + posted queue (`doc/design/python-sdk.md` §2). `Shared`/`Channel` are the cross-thread latest/ordered primitives per `AGENTS.md`.
- `prism.transaction()` buffers per-Python-thread and flushes as one closure on the logic thread.
