# Python Examples

Standalone runnable demos for the PRISM Python SDK (`python/prism/`). Each is the shortest honest program for its topic on `prism.Model` + `ViewBuilder` — fully multi-threaded (any thread may `m.field.value = x`).

## Prerequisites

Built extension at `builddir/python/prism/_prism_ext.cpython-*.so`:

```bash
meson setup builddir
ninja -C builddir
```

Needs `pydantic` only for `03_*` (validation). `pytest` for the test suite.

## Running

```bash
PYTHONPATH=builddir/python python3 python/examples/01_counter.py
PYTHONPATH=builddir/python python3 python/examples/02_mixer.py
PYTHONPATH=builddir/python python3 python/examples/03_validation_and_transaction.py
PYTHONPATH=builddir/python python3 python/examples/04_background_shared_channel.py
PYTHONPATH=builddir/python python3 python/examples/05_lists_and_derived.py
# Fancy
PYTHONPATH=builddir/python python3 python/examples/06_live_plot.py
PYTHONPATH=builddir/python python3 python/examples/07_file_tree.py
PYTHONPATH=builddir/python python3 python/examples/08_dashboard.py
PYTHONPATH=builddir/python python3 python/examples/10_worker_pool_plot.py
PYTHONPATH=builddir/python python3 python/examples/11_error_handling.py
PYTHONPATH=builddir/python python3 python/examples/12_asyncio_bridge.py
```

Each opens a window (`prism.run` blocks, releases the GIL). Close the window to exit.

`09_headless_multithread_stress.py` never opens a window — it drives `prism.headless()` and asserts exact counts, so it doubles as the pytest/CI check the `3.14t` free-threaded lane runs:

```bash
PYTHONPATH=builddir/python python3 python/examples/09_headless_multithread_stress.py
```

`10_worker_pool_plot.py`, `11_error_handling.py` and `12_asyncio_bridge.py` also run without a display via `--headless`:

```bash
PYTHONPATH=builddir/python python3 python/examples/10_worker_pool_plot.py --headless
PYTHONPATH=builddir/python python3 python/examples/11_error_handling.py --headless
PYTHONPATH=builddir/python python3 python/examples/12_asyncio_bridge.py --headless
```

## Index

| Example | What it shows |
|---|---|
| `01_counter.py` | `field()` auto-stacked view, `observe` |
| `02_mixer.py` | `slider`/`checkbox`, manual `view()` with `hstack`/`widget`, `field.add()` from a background `worker()` |
| `03_validation_and_transaction.py` | `Annotated` + `validator_for` (pydantic), `transaction()` coalescing |
| `04_background_shared_channel.py` | `shared()` latest-value + `channel()` lossless stream + `list_field()`, any-thread writes |
| `05_lists_and_derived.py` | `list_field()` `push`/`erase`/`observe_*`, `derived()` over scalars |
| `06_live_plot.py` | `plot_field()` + `canvas(plot)`, `replace_series()`/`set_labels()`, `@prism.on_change` rebuilding from slider deps, sliders + a jittering worker |
| `07_file_tree.py` | `tree_field(source)` + `tree(ctrl)`, a `TreeSource` Python object, lazy expansion |
| `08_dashboard.py` | Plot + Tree + slider controls composed via one custom `view()` |
| `09_headless_multithread_stress.py` | 8-thread `ThreadPoolExecutor` storm over `shared`/`channel`/`field`/`derived`, no display; also the `3.14t` free-threaded CI check |
| `10_worker_pool_plot.py` | `ThreadPoolExecutor(max_workers=4)` computing a pure-Python (`cmath`) FFT spectrum per window inside a `prism.worker`, each window posted via `plot.replace_series()`, `--headless` mode |
| `11_error_handling.py` | `prism.on_error(handler)`, an observer + a worker that raise, `--headless` mode |
| `12_asyncio_bridge.py` | an asyncio loop pumped from a single `prism.worker` (no raw thread), `run_coroutine_threadsafe` from an observer, a coroutine feeding a `channel` back, `--headless` mode |

## Notes

- `ViewBuilder` exposes `widget`, `list`, `canvas` (`BoundPlot`), `tree` (`BoundTree`), `hstack`, `vstack`. Plot uses the canvas escape hatch `vb.canvas(plot)`; Tree uses `vb.tree(ctrl)`. See `python/prism/__init__.py:plot_field`/`tree_field`.
- Tree source is any Python object implementing `root_count/root_at/child_count/child_at/label/has_children` (and optional `attributes`/`icon`) — see `tree_sources.py:DictTreeSource`/`FsTreeSource` (shared by `07_file_tree.py` and `08_dashboard.py`; the script directory is on `sys.path` when either runs directly, so `import tree_sources` just works).
- `m.field.value` get/set goes through the binding cache and posted queue. `shared()`/`channel()` are the cross-thread latest-value/ordered primitives.
- `prism.transaction()` buffers writes per Python thread and flushes as one closure on the logic thread. `prism.is_logic_thread()` reports whether the calling thread is the logic thread (no example calls it directly).
- `@prism.on_change(*deps, immediate=False)` decorates a `Model` method to run on the logic thread whenever any dep field changes (see `06_live_plot.py`, `08_dashboard.py`).
- `prism.on_error(handler)` installs a single process-wide hook for exceptions raised inside an `observe`/`derived` callback or a `prism.worker()` fn — `handler(exc)` receives the original exception. Observer/derived exceptions route through the logic thread; a `worker()` exception is caught and reported on that worker's own thread (see `11_error_handling.py`). `prism.on_error(None)` restores the default (traceback to stderr). A failing callback never stops the drain.

## Threading guarantees

One logic thread owns the widget tree and every field. Any thread may read or write a field handle — writes are posted to the logic thread, reads are dispatched to it and block. Never read-modify-write (`f.value += 1`) off the logic thread; use the atomic `field.add(n)`, or send through a `channel` and increment in its observer (see `09_headless_multithread_stress.py`). Any thread may call `shared().value = x`, `channel().send(x)`, or `field.add(n)`; these are safe under arbitrary concurrency. `shared()` publishes only the latest value — intermediate values are dropped by design. `channel()` is lossless and per-producer FIFO. `plot.replace_series()`/`set_labels()` are each a single dispatched post (clear+add+notify), so concurrent callers never see a torn plot. `prism.headless()` runs an app with no display and returns an `App` handle whose `wait_until()` is the convergence signal for tests/CI. A posted closure wakes an idle logic thread exactly once per burst. Exceptions thrown by posted or observed callbacks are routed to `prism.on_error()` (default: printed to stderr) and never stop the drain. `prism.transaction()` batches and coalesces notifications on the calling thread; it does not roll back on exception.

`prism.worker(fn)` runs `fn` on its own thread; writes made inside `fn` are *not* batched by default — each is its own posted closure. Wrap related writes in `with prism.transaction():` inside `fn` to send them as one:

```python
def fn(stop):
    with prism.transaction():
        m.x.value = 1
        m.y.value = 2
```

`list_field()`'s `push`/`erase`/`set`/`replace_all` are each one posted, atomic operation on the logic thread — there's no read-modify-write hazard the way there is for a plain field, so there's no `add()`-style analogue for lists. `size()`/`to_list()` are dispatched reads, same as `field.value`.

`handle.observe(cb)` (and `observe_insert`/`observe_remove`/`observe_update` on lists) is owned by the handle it's called on: the subscription lives exactly as long as that handle — or its owning `Model` — is reachable. Keep a reference to the handle (or hold the `Model`) to keep the observer firing; letting both go silently ends it. `conn = s.observe(cb); del s` leaves `conn` alive but inert — the callback was already released with `s`. Call `conn.disconnect()` to end a subscription early, on purpose.
