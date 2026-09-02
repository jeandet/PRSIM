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
PYTHONPATH=build/python python python/examples/10_worker_pool_plot.py
PYTHONPATH=build/python python python/examples/11_error_handling.py
PYTHONPATH=build/python python python/examples/12_asyncio_bridge.py
```

Each opens a window (`prism.run` blocks, releases GIL). Close the window to exit.

`09_headless_multithread_stress.py` never opens a window — it drives
`prism._run_headless()` directly and asserts exact counts, so it doubles as
the pytest/CI check the `3.14t` free-threaded lane runs:

```bash
PYTHONPATH=build/python python python/examples/09_headless_multithread_stress.py
```

`11_error_handling.py`, `10_worker_pool_plot.py` and `12_asyncio_bridge.py` also run without a display via `--headless`:

```bash
PYTHONPATH=build/python python python/examples/11_error_handling.py --headless
PYTHONPATH=build/python python python/examples/10_worker_pool_plot.py --headless
PYTHONPATH=build/python python python/examples/12_asyncio_bridge.py --headless
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
| `09_headless_multithread_stress.py` | 8-thread `ThreadPoolExecutor` storm over `shared`/`channel`/`field`/`derived`, no display; also the `3.14t` free-threaded CI check | — |
| `10_worker_pool_plot.py` | `ThreadPoolExecutor(max_workers=4)` computing a pure-Python (`cmath`) FFT spectrum per window inside a `prism.worker`, results crossing to the plot via a JSON `channel(str)`, windows/sec status, `--headless` mode | — |
| `11_error_handling.py` | `prism.on_error(handler)`, an observer + a worker that raise, `--headless` mode | — |
| `12_asyncio_bridge.py` | asyncio event loop inside a `prism.worker`, `asyncio.run_coroutine_threadsafe` from an observer, a coroutine feeding a `channel` back to the logic thread, `--headless` mode | — |

## Notes

- `ViewBuilder` Python now exposes `widget`, `list`, `canvas` (`BoundPlot`), `tree` (`BoundTree`), `hstack`, `vstack` (`python/src/prism_ext.cpp:1242`). Plot uses the canvas escape hatch `vb.canvas(plot).depends_on(...)` (`app/view_builder.hpp:292`); Tree uses `vb.tree(ctrl)` (`app/view_builder.hpp:375`). See `python/prism/__init__.py:plot_field`/`tree_field`.
- Tree source is any Python object implementing `root_count/root_at/child_count/child_at/label/has_children` (and optional `attributes`/`icon`) — mirrors `TreeStorage` tier 2 (`ui/tree.hpp:40`). See `tree_sources.py:DictTreeSource` and `FsTreeSource` (shared by `07_file_tree.py` and `08_dashboard.py`; the script directory is on `sys.path` when either is run directly, so `import tree_sources` just works).
- `m.count.value` get/set uses the binding cache + posted queue (`doc/design/python-sdk.md` §2). `Shared`/`Channel` are the cross-thread latest/ordered primitives per `AGENTS.md`.
- `prism.transaction()` buffers per-Python-thread and flushes as one closure on the logic thread.
- `prism.on_error(handler)` installs a single process-wide hook for exceptions raised inside an `observe`/`derived` callback or a `prism.worker()` fn — `handler(exc)` receives the original Python exception (or a `RuntimeError` wrapping a non-Python one). Observer/derived exceptions route through the logic thread; a `worker()` exception is instead caught and reported directly on that worker's own background thread (see `11_error_handling.py`) — write handlers that don't assume a single calling thread. `prism.on_error(None)` restores the default, which prints a traceback to stderr; a raising handler itself falls back to that same default. A failing callback never stops the drain — the next event still fires.

## Threading guarantees

One logic thread owns the widget tree and every field; fields are not thread-safe. Any thread may call `prism.shared(...).set()`, `prism.channel(...).send()`, or post a closure; these are safe under arbitrary concurrency. `prism.shared()` publishes only the latest value — intermediate values are dropped by design. `prism.channel()` is lossless and per-producer FIFO. A posted closure wakes an idle logic thread exactly once per burst. Exceptions thrown by posted or observed callbacks are routed to `prism.on_error()` (default: printed to stderr) and never stop the drain. `prism.transaction()` batches and coalesces notifications on the calling thread; it does not roll back on exception.
