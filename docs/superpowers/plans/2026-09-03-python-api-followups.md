# Plan: close the remaining items from the 2026-09-02 Python-API review

Spec: `doc/review-2026-09-02-python-api.md`, section "Status — 2026-09-03" (the deferred list),
plus the "Added 2026-09-03" backlog entries. Base: `75256da` (main, pushed).

## Global Constraints (binding for every task)

- Work directly on `main` in `/var/home/jeandet/Documents/prog/PRSIM`. No worktrees, no branches. Never push (the controller pushes).
- **One build/test invocation at a time, in the foreground, waited to completion.** Never background a build; never run two commands against any build dir concurrently, and never build `builddir` and another build dir (`builddir-tsan`, `builddir-asan`) at the same time. Full suite: `meson test -C builddir --print-errorlogs` — report the literal `Ok:`/`Fail:` lines.
- TDD: reproducer/regression test first, shown failing, then the fix, then full suite green. C++ tests: doctest in `tests/` (`headless_tests` in `tests/meson.build`). Python tests: `python/tests/test_prism_python.py` (pytest via the `python_bindings` meson test, `PYTHONPATH=builddir/python`). `ninja -C builddir` re-copies `python/prism/__init__.py` into the build dir.
- Commit per task; stage only the exact paths you changed (never `git add -A`/`-u`; `doc/` and `docs/` hold untracked files). Conventional prefix, body says why, and end with exactly:
  `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_018r8oBsKu86vKbRDVbWm6vw`
- Style: KISS, small functions, one abstraction level per function, no comment-decorated blocks, comments only for links or non-obvious "why", `simplify:` on deliberate corner cuts. Python stays Pythonic: real exceptions with actionable messages, no silent fallbacks, docstring first line = thread affinity.
- GIL rule (from a regression in the previous series): every `nb::gil_scoped_release` must be guarded by `PyGILState_Check()`; never construct one on the `run()` thread's GIL-released path (initial widget build, teardown).
- Do not touch files outside the task's scope. Do not spawn subagents. gdb/lldb/valgrind are NOT installed; use `python3 -X faulthandler`, temporary stderr traces, or `builddir-tsan` (one build dir at a time).

### Task 1: Standalone handle state outlives the handle during its own drain

Files: `python/src/prism_ext.cpp`, `python/tests/test_prism_python.py`.

Today `SharedHandle<T>`/`ChannelHandle<T>` own `Shared<T>`/`Channel<T>` by value and `drain_fn` (a `shared_ptr<std::function<void()>>`) captures `this`. If an observer callback drops the last Python reference to the handle *whose own* `drain_notifications()` is executing, the handle's members are freed under the running function (the sweep keeps the function alive, not the state).
1. Move the state into a `std::shared_ptr<Shared<T>>` / `std::shared_ptr<Channel<T>>` member (or a small `struct State { Shared<T> shared; }` behind one shared_ptr). `drain_fn` and every observer wrapper capture that shared_ptr, never `this`. `get/set/send/observe` go through it. `keep_alive<0,1>` on `observe` stays (Connection must not outlive the hub — the shared_ptr captured in the Connection's keepalive already covers Bound*; for standalone, attach the same `conn.keep_alive(state)` so the hub lives as long as the Connection regardless of the Python handle).
2. Test first (subprocess pattern like `test_derived_field_survives_run_headless_teardown`): standalone `s = SharedInt(0)`; `s.observe(cb)` where `cb` deletes the only reference to `s` (`holder.clear()`); set from a thread; `_run_headless`; returncode 0. Also: after the handle is gone, the Connection still disconnects cleanly at `_atexit_clear` (no crash at exit).
3. Full suite green; commit `fix(python): standalone handle state owned by shared_ptr; self-drop during drain is safe`.

### Task 2: Post-finalize and argument-validation hardening

Files: `python/src/prism_ext.cpp`, `python/prism/__init__.py`, `python/tests/test_prism_python.py`.

1. `logic_wrapper` (installed in `run`/`_run_headless`): when `!Py_IsInitialized()`, do NOT run `fn()` — return. Nothing that drains Python callbacks may run after finalization. Same for `drain_queue_loop`'s GIL branch if it has the same shape.
2. Deferred readers (`BoundTree::rows`, `ListHandle::to_list`, `BoundList::to_list`, any other closure that builds `nb::list`/`nb::object` inside a posted lambda): restructure so the posted closure fills a C++ container (`std::vector<T>` / vector of row structs) and the conversion to Python objects happens at the caller under the GIL. No `nb::` object construction may happen on a `!Py_IsInitialized()` path.
3. `prism.on_error(handler)`: raise `TypeError("on_error(): handler must be callable or None")` at call time if not callable and not None.
4. `apply_validator` (C++): if the validator returns `None` or a value that fails `nb::cast<T>`, raise `TypeError("validator for '<field>' must return a <T> (or raise); got <repr>")` — build the message in C++ with `nb::repr`. The field name is available from the descriptor: store it as `handle.__dict__["_prism_name"]` in `_allocate` (one line) and read it in the error path only.
5. `_DerivedDescriptor`: if the user function returns a value that is not the inferred/hinted type at recompute time, the C++ side casts — check what happens today and make it a clear `TypeError` routed through `on_error` (not a silent 0, not a crash).
6. Tests first for 3, 4, 5. Full suite green; commit `fix(python): no Python calls after finalization; clear errors for bad handlers/validators`.

### Task 3: Fix the `view()` + `derived` headless-teardown bug

Files: `python/src/prism_ext.cpp`, `python/prism/__init__.py` (if needed), `python/tests/test_prism_python.py`, `python/examples/02_mixer.py`, `python/examples/05_lists_and_derived.py`.

The docstrings of 02 and 05 describe an "Invalid argument at exit" (or similar) failure when a Model overrides `view()` and has a `derived` field, under `_run_headless`. Read both docstrings for the exact symptom and any repro hints. Steps: write the smallest subprocess reproducer (Model with `view(self, vb)` placing widgets + one `derived`; `_run_headless(delay_ms=200)`; assert returncode 0 and empty stderr); confirm it fails; root-cause with `-X faulthandler` and temporary traces (suspects: `SlotDerived` connections into dependency Fields destroyed in the wrong order relative to the widget tree; `nb::callable` compute lambda destroyed after the interpreter state changed; the `view()` callback object held by the C++ tree past `_atexit_clear`); fix the root cause; remove the workaround notes from 02/05 (and restore any `view()` they disabled — 05 has a commented-out `view()` for this reason); full suite green; commit `fix(python): view() + derived survives headless teardown`. If the reproducer does NOT fail on current main, prove it (3 runs, stderr empty), remove the stale docstring notes, and commit that as `docs(examples): drop obsolete view()+derived teardown note`.

### Task 4: `plot.replace_series(xs, ys, ...)` single-post primitive

Files: `python/src/prism_ext.cpp`, `python/tests/test_prism_python.py`, `python/examples/10_worker_pool_plot.py`, `python/examples/README.md`.

1. Read `BoundPlot`/`PlotHandle` `add_series`/`clear_series`/`notify` (all go through `list_op_dispatch`, one post each). Add `replace_series(xs, ys, color=..., thickness=..., fill=...)` with the same argument spelling as `add_series`, implemented as ONE dispatched closure doing clear + add + notify on the logic thread. Bind it on both Bound and standalone plot handles if both exist.
2. Test first: from a background thread call `replace_series` 100 times with different lengths while `_run_headless` runs; after convergence the series count is exactly 1 and its length equals the last call's. (Read how existing plot tests inspect series — if there is no accessor, add a read-only `series_count()`/`series_len(i)` dispatched via `dispatch_sync_read`.)
3. Example 10: pool threads call `m.plot.replace_series(freqs, mags)` directly — delete the JSON channel hop and the `json` import; keep the windows/sec status via a `channel(0)` tick or a `shared`; update the docstring to state the idiom ("one dispatched call, atomic on the logic thread"). README row.
4. Full suite green; commit `feat(python): plot.replace_series() single-post update; example 10 passes lists directly`.

### Task 5: `slider()` / `checkbox()` — wire or drop `kind`/`meta`

Files: `python/src/prism_ext.cpp`, `python/prism/__init__.py`, `python/tests/test_prism_python.py`, `python/examples/02_mixer.py` (docstring only if needed).

Investigate first: does the C++ side have distinct widget kinds for a float field (slider with min/max) and a bool field (checkbox), reachable from `py_widget_dispatch`/`ViewBuilder` (grep `slider`, `checkbox`, `Range`, `min`/`max` under include/prism/widgets and delegates)? Decide and record in the report:
- If a slider widget with min/max exists: add `_add_float_internal(value, min, max)`-style plumbing (or a `set_range(min, max)` on `BoundFloat`) so `slider(default, min, max)` actually produces a ranged slider; `checkbox` likewise if a distinct checkbox delegate exists. Test: the handle exposes the range (`m.volume.range == (0.0, 1.0)`) and a value set outside the range is clamped or rejected — pick what the C++ widget does and assert it.
- If no such widget exists: delete `kind`/`meta` and the `simplify:` comment; make `slider()`/`checkbox()` thin aliases with docstrings saying "float field; a ranged slider widget is not implemented yet" / "bool field".
Full suite green; commit accordingly (`feat(python): slider()/checkbox() carry range to the widget` or `refactor(python): slider()/checkbox() are plain aliases`).

### Task 6: ASan lane (CI) verified locally

Files: `.github/workflows/ci.yml`, `python/prism/meson.build` (only if the test needs an env var), `README.md` (one line under testing, if a testing section exists).

1. Locally: `meson setup builddir-asan -Db_sanitize=address,undefined -Dbuildtype=debugoptimized` (ONLY if `builddir-asan` does not exist), then `ninja -C builddir-asan`, then `meson test -C builddir-asan python_bindings --print-errorlogs`. Running a sanitized extension inside an unsanitized Python needs `LD_PRELOAD=$(gcc -print-file-name=libasan.so)` and usually `ASAN_OPTIONS=detect_leaks=0` (Python's own allocations leak-report) — set them through the meson test `env:` only for that build (guard with a meson option or `get_option('b_sanitize')` check so `builddir` is unaffected). Remember: one build dir at a time — do not touch `builddir` while `builddir-asan` builds/tests.
2. Prove the lane is meaningful: temporarily revert the weak_ptr registry to raw pointers (or comment out one `lock()`), rebuild `builddir-asan` once, run the `python_bindings` test, paste the ASan report showing `test_standalone_drain_callback_drops_sibling_handle` (or its actual name) failing; then restore the code and confirm green. Do not commit the temporary revert.
3. Add an `asan` job to `.github/workflows/ci.yml` mirroring the existing free-threaded job's setup steps, with the same options/env; runs `meson test -C builddir-asan --print-errorlogs` excluding the SDL/display suites the tsan job excludes.
4. Commit `ci: ASan+UBSan lane; python bindings run under sanitizers`. Delete `builddir-asan` at the end ONLY if the user's disk is a concern — otherwise leave it (say which in the report).

### Task 7: Docs and example polish

Files: `README.md`, `python/examples/README.md`, `python/examples/12_asyncio_bridge.py`, `python/prism/__init__.py` (docstrings only).

1. `README.md`: replace "readers never block writers" for `Shared<T>` with "readers and writers take a brief internal lock (`std::atomic<std::shared_ptr>`); no lock is held while user code runs".
2. `python/examples/README.md`: use `.value = x` (the idiom) instead of `.set()` in the threading-guarantees paragraph; add one sentence on `plot.replace_series` (Task 4) and one on `on_error` thread semantics if not already there.
3. `12_asyncio_bridge.py` `shutdown_loop`: `try/finally` around `loop.close()`; cancellation wait bounded with `asyncio.wait_for(..., timeout=2.0)` and a stderr line if it times out.
4. `Model.__init_subclass__` docstring (or the class docstring): state that a bare `Annotated[int, ...]` annotation without a default gets the type's zero value (`0`/`0.0`/`""`/`False`) — document, don't change behavior.
5. `slider()`/`checkbox()` docstrings consistent with Task 5's outcome.
6. No build needed except `ninja -C builddir` + `python_bindings` once for the docstring/example edits. Commit `docs(python): threading wording, replace_series, asyncio shutdown bound`.

## Part B — API ergonomics (from the 2026-09-03 examples audit; the examples' boilerplate is evidence of API gaps)

Ordering ruling: Tasks 8-12 run after Task 6 and BEFORE Task 7 (docs last). Task 4 is amended: `replace_series` also accepts the list-of-series form and a `set_labels(x=, y=)` companion (see Task 4 amendment below).

### Task 4 amendment
`replace_series(xs, ys, *, color=None, thickness=..., fill=...)` for one series, and `replace_series([(xs, ys, color), ...], *, thickness=...)` for many — ONE dispatched post either way (clear + add each + notify). Add `set_labels(x: str | None = None, y: str | None = None)` (one post). Example 10 and 06/08's clear/add/notify triples use them.

### Task 8: `prism.run()` no longer needs `_main()`

Files: `python/prism/__init__.py`, `python/tests/test_prism_python.py`, `python/examples/*.py` (only the `_main()` wrapper lines), `python/examples/README.md`.

Root cause of the `_main()` idiom: a Model left in `__main__` globals makes nanobind's leak check report leaked instances at exit (see the comment in `__init__.py` near `_atexit_clear`, ~:264). Fix at the source: in `run()`'s `finally` (next to `_stop_all_workers()`), call `_clear_model_observers(model)` and drop the model's `_prism_fields` cache — the app is over, the handles are dead weight. Test first (subprocess): a script with a module-global `m = Counter(); prism.run(m)` replaced by `prism._run_headless(m, delay_ms=50)` at module level; assert returncode 0 AND stderr does not contain "leaked". Then remove `_main()`/`if __name__` wrappers from every example where the body is a straight script (keep a `main()` only where the example is also imported by a test — 09/10/11/12 — and make those `main()` the real entry with no wrapper indirection). Update the README "Run" lines if they change. Commit `feat(python): run() releases the model; examples drop _main()`.

### Task 9: Public headless runner with quit-on-condition

Files: `python/src/prism_ext.cpp`, `python/prism/__init__.py`, `python/tests/test_prism_python.py`, `python/examples/09_headless_multithread_stress.py`, `10_worker_pool_plot.py`, `11_error_handling.py`, `12_asyncio_bridge.py`.

1. C++: `DelayHeadlessBackend` currently sleeps `delay_ms` then fires WindowClose (prism_ext.cpp ~:1677-1689). Replace the sleep with a condition-variable wait (timeout = `delay_ms`, or until `request_quit()` is called from any thread). Add `m.def("_request_quit")` that signals it. Before firing WindowClose, run one final drain of the mutation queue and `model.drain()` so late posts are delivered (the "posts after close are dropped" gap).
2. Python: `prism.headless(model, *, timeout: float = 10.0)` context manager: starts `_run_headless(model, delay_ms=int(timeout*1000))` on a thread, blocks until `_is_running()`; yields an `App` with `.wait_until(pred, timeout=None, poll=0.005)` (raises `TimeoutError("condition not met within X s")`), `.quit()`, `.is_running`. `__exit__` calls `quit()` and joins; exceptions propagate. `prism.run(model, *, headless_ms: int | None = None)` is NOT added — `headless()` is the one API. Keep `_run_headless` as the private primitive used by `headless()`.
3. Tests first: `with prism.headless(m) as app: m.x.value = 5; app.wait_until(lambda: seen == [5])` — passes; `wait_until` with an impossible predicate and `timeout=0.2` raises `TimeoutError`; after the block the app is not running; a post made just before `quit()` is delivered (final drain).
4. Rewrite 09-12 to `prism.headless(...)`; delete their `--headless` argv sniffing (a `--headless` flag stays only as `if "--headless" in sys.argv: with prism.headless(m, timeout=1.0) as app: ... else: prism.run(m)` — 3 lines, no duplicated `headless: bool` params). 09 loses the startup poll, the convergence poll, the ceiling prose and the ordering assertion.
5. Full suite green; commit `feat(python): prism.headless() context with wait_until/quit; final drain before close`.

### Task 10: Declarative behaviors — `@on_change`, descriptor deps for `derived`, one observe spelling

Files: `python/prism/__init__.py`, `python/tests/test_prism_python.py`, `python/examples/05_lists_and_derived.py`, `06_live_plot.py`, `08_dashboard.py`, `01_counter.py`, `02_mixer.py`, `03_validation_and_transaction.py`, `04_background_shared_channel.py`.

1. `@prism.on_change(*deps, immediate=False)` method decorator: `deps` are descriptors (`frequency`, `amplitude` — the class attributes) or strings; at `Model.__init__` the method is bound through the same weakref trampoline `view` uses and subscribed to each dep with fire-and-forget keepalive; `immediate=True` calls it once after construction (replaces the manual priming `m.rebuild()`). The callback receives no value argument (it reads `self.<field>.value`). Thread affinity: logic thread.
2. `derived(fn, *deps)` accepts descriptors as well as strings (`__init__.py` ~:479 already half-supports non-str deps — finish it); docstring shows the descriptor form first.
3. Deprecate the class-level `Class.field.observe(model, cb)` spelling with a `DeprecationWarning` pointing to `model.field.observe(cb)`; keep it working.
4. Rewrite the listed examples: `06`/`08` use `@on_change(..., immediate=True)` and lose the three `observe(lambda v: m.rebuild())` lines and the priming call; `05`/`06`/`09` use descriptor deps; all examples use `m.field.observe(cb)` and drop unused `conn = ...` assignments (`01:24`, `02:36`, `03:50-51`, `04:47-56`); `02:50` uses `prism.is_logic_thread`; `06:85-86` drops the single-write `transaction()`; dead `view()`s that reproduce the auto view are deleted (`03:31-32`, `07:32-34`, `10:78-80`).
5. Tests first for 1-3. Full suite green; commit `feat(python): @on_change decorator, descriptor deps for derived, one observe spelling`.

### Task 11: `worker(repeat=N)`, zero-arg fn, `field.add(n)`

Files: `python/prism/__init__.py`, `python/src/prism_ext.cpp`, `python/tests/test_prism_python.py`, `python/examples/02_mixer.py`, `04_background_shared_channel.py`, `06_live_plot.py`, `11_error_handling.py`, `12_asyncio_bridge.py`, `09_headless_multithread_stress.py`.

1. `worker(fn, *, interval=None, repeat=None, daemon=True, name=None)`: `repeat=N` stops after N calls; `fn` may take zero args or one (`stop`) — detect with `inspect.signature` once at creation.
2. `BoundField<int>/<double>` (and standalone Field) get `add(n)`: one dispatched post that does `field.set(field.get() + n)` ON THE LOGIC THREAD (atomic w.r.t. other adds). Bind as `.def("add", ...)`; docstring "Any thread; atomic increment applied on the logic thread." Python test: 8 threads × 1000 `m.counter.add(1)` under `prism.headless` → exactly 8000.
3. Rewrite: `02`, `06`, `11` use `repeat=`; `04` uses `prism.worker` instead of raw `threading.Thread/Event`; `09`'s `incr` channel becomes `m.counter.add(1)` and the docstring explains `add()` vs `+=`; the `[0]` mutable-cell counters become `nonlocal` or `add()`.
4. Full suite green; commit `feat(python): worker(repeat=), zero-arg workers, atomic field.add()`.

### Task 12: Public surface and final example pass

Files: `python/prism/__init__.py`, `python/tests/test_prism_python.py`, all `python/examples/*.py`, `python/examples/README.md`.

1. Remove the 20 type-suffixed handle classes (`FieldInt`, `BoundSharedFloat`, …) from `__all__`; keep them importable from `prism._prism_ext` and add a `__getattr__`-free note in the module docstring that handles are created by descriptors, never constructed directly. Test: `prism.__all__` contains no name matching `^(Field|Bound|Shared|Channel|List)(Shared|Channel|Derived|List)?(Int|Float|Str|Bool)$`.
2. Read every example top to bottom as a newcomer. Each must be the shortest honest program for its topic: one docstring (title / Demonstrates / Run), no wrapper functions unless imported by a test, no unused variables, no comments narrating what the code says, `.value` on both read and write, `m.field.observe(cb)` only, `prism.worker` only, `prism.headless` only. Target sizes from the audit: 06 ≈ 40 lines, 09 ≈ 45 lines. Where an example still needs a workaround, the docstring names the missing API in one line.
3. README table regenerated to match; the "Threading guarantees" paragraph mentions `field.add()`, `plot.replace_series()`, `prism.headless()`.
4. Carry-over from Task 10's review: `10_worker_pool_plot.py` lost its `view()` but the auto view now also renders `windows_done` (a field the old view hid). Decide deliberately: either keep the auto view and make `windows_done` a visible status the example wants, or restore a two-line `view()` — say which in the docstring.
5. Full suite green; run every headless-capable example once and paste outputs; `py_compile` the GUI ones. Commit `refactor(examples): shortest honest examples on the new API; trim __all__`.

### Task 13: Real sliders and checkboxes from Python (runs after Task 11, before Task 12)

Files: `python/src/prism_ext.cpp`, `python/prism/__init__.py`, `python/tests/test_prism_python.py`, `python/examples/02_mixer.py`, `06_live_plot.py`, `08_dashboard.py`.

Task 5 found that C++ already has `Slider<T, Options>` and `Checkbox` delegates (grep `struct Slider`/`Checkbox` under include/prism) but no Python plumbing, so `prism.slider(default, min, max)` renders as a plain float field. This task adds the plumbing — no new widget code:
1. Read how a C++ model uses `Slider<double, ...>`/`Checkbox` as a struct member and how `ViewBuilder::widget(...)` renders them (find a C++ example under `examples/` or `tests/`). Mirror the `Slot<T>` pattern: `SlotSlider` owning the slider type (whose inner `Field<double>` is what observers/`.value` bind to) and `SlotCheckbox`; `_add_slider_internal(value, min, max)` / `_add_checkbox_internal(value, label)` returning the existing `BoundFloat`/`BoundBool` (pointing at the inner Field) so the Python surface (`.value`, `observe`, validators, `add`) is unchanged; `build()` calls `vb.widget(slider)`.
2. `slider()`/`checkbox()` descriptors allocate through the new internals; `kind`/`meta` are used, so the `simplify:` comment goes; expose `m.volume.range` (read-only tuple) on the returned handle via `__dict__` set in `_allocate` — no new C++ property needed.
3. Behavior for out-of-range sets: whatever the C++ `Slider` does (clamp or accept) — assert it in a test and state it in the `slider()` docstring.
4. Tests first: `slider(0.5, min=0, max=1)` handle has `.range == (0.0, 1.0)`, `.value` round-trips, observers fire; `checkbox(True, label="x")` is a bool field; a golden/headless run with both renders without error (`_run_headless`).
5. Full suite green; run 02 `--headless` if it has one, else `py_compile`. Commit `feat(python): slider()/checkbox() render the C++ Slider/Checkbox delegates`.

### Task 14: Observed standalone handles must be collectable (makes the UAF tests real) — runs right after Task 6

Files: `python/src/prism_ext.cpp`, `python/tests/test_prism_python.py`.

Task 6 found that both standalone-handle UAF regression tests are vacuous: an `observe()`d standalone handle is immortal until `_atexit_clear()`, because `nb::keep_alive<0,1>()` on standalone `observe` makes the returned Connection keep the Python handle alive (nanobind side-table, invisible to the GC) while the handle's `__dict__["_prism_keepalive"]` holds the Connection — a cycle the GC cannot break. Since Task 1, the hub's lifetime is already covered by `conn.keep_alive(state)` for Shared/Channel, so `keep_alive<0,1>` is redundant there and harmful.
1. Give `FieldHandle<T>` and `ListHandle<T>` the same shared_ptr-owned state as Task 1 gave Shared/Channel, with `conn.keep_alive(state)` on their `observe*`.
2. Remove `nb::keep_alive<0,1>()` from ALL standalone `observe*` registrations in `bind_scalar`/`bind_list` (Bound* never had it). The Connection now keeps the *hub state* alive, not the Python wrapper.
3. Tests first: (a) `s = SharedInt(0); s.observe(cb); w = weakref.ref(s); del s; gc.collect(); assert w() is None` — must FAIL before (immortal) and pass after; same for `FieldInt`, `ChannelInt`, `ListInt`; (b) after the handle is collected, a later app tick must not crash and must not call `cb` (hub alive via the Connection? — no: the Connection lived in the handle's `__dict__` and died with it, so the hub state's last owner is `drain_fn`'s registry weak_ptr → state freed, entry pruned; assert `_standalone_state_alive_count()` returns to baseline); (c) the two existing UAF tests are rewritten so the sibling/self handle is really freed (drop the artificial `.observe()` that kept it alive, or `del` the keepalive) — and Task 6's ASan lane must show them as meaningful: temporarily break the registry as Task 6 did, run `meson test -C builddir-asan python_bindings`, paste the ASan report, restore, re-run green (one build dir at a time; never overlap with `builddir`).
4. Full suite green in `builddir`; `python_bindings` green in `builddir-asan`. Commit `fix(python): observed standalone handles are GC-collectable; UAF tests exercise a real free`.

### Task 15: Posted mutation closures must own their target (runs right after Task 8)

Files: `python/src/prism_ext.cpp`, `python/tests/test_prism_python.py`.

Task 14's review found: `field_set_dispatch` (~prism_ext.cpp:292-309) and `list_op_dispatch` (~:694-705) post closures to the logic thread that capture a RAW pointer to the target `Field<T>`/`List<T>`. If the last reference to the owning handle is dropped after `.set()`/`.push()`/`.erase()`/`.replace_all()` and before the logic thread runs the closure, the state is freed under the closure → UAF. Now reachable for standalone handles (they became collectable in Task 14); latent for Bound* too (Model destroyed with a posted set in flight → `Slot` freed, since `PyModel::slots` dies with the Model).
1. Change the posted closures to capture an owning `std::shared_ptr` alongside the pointer: standalone handles pass their `state` shared_ptr; Bound* pass `owner` (`shared_ptr<SlotBase>`). Signature idea: `field_set_dispatch(std::shared_ptr<void> keep, Field<T>* field, T v)` / `list_op_dispatch(std::shared_ptr<void> keep, std::function<void()> fn)`; the closure captures `keep` by value. Same for `BoundPlot`/`PlotHandle` mutators (`replace_series`, `add_series`, …) and `BoundTree` posts if they post raw pointers — grep every `try_post_via_handle`/`list_op_dispatch`/`field_set_dispatch` call site and make each one own its target. The on-logic-thread inline path is unchanged.
2. Tests first, run under BOTH `builddir` and `builddir-asan` (one dir at a time): with a headless app running, loop 500×: create a standalone `FieldInt`, `.set(i)` from a background thread, immediately `del` it (and `gc.collect()`); same for `ListInt.push`; and for a Bound* case: create a Model, `m.x.value = 1` from a background thread, `del m`. ASan must be clean; also paste the ASan report from a deliberate temporary revert (raw pointer only) to prove the test discriminates — do not commit the revert.
3. Full suite green in `builddir`, `python_bindings` green in `builddir-asan`. Commit `fix(python): posted mutations keep their target alive`.

### Task 16: PyModel participates in Python GC (runs after Task 15, before Task 9)

Files: `python/src/prism_ext.cpp`, `python/prism/__init__.py`, `python/tests/test_prism_python.py`, `python/examples/*.py` (drop the remaining `_main()` wrappers), `python/examples/README.md`.

Task 8 found that a Model with a `view()` override or a `derived()` field is reported as leaked at exit when it lives in `__main__` globals, because `PyModel` holds `nb::object`s (the view callback `py_view_cb`, derived compute callables inside `SlotDerived`, `SlotTree::py_src_holder`) that reference the Model → a cycle invisible to the cyclic GC. Fix the root cause: make the `PyModel` nanobind type GC-aware.
1. Register `nb::class_<PyModel>` with `nb::type_slots(slots)` where `slots` provides `Py_tp_traverse` and `Py_tp_clear` (see nanobind docs "Reference cycles"/`type_slots`, and `subprojects/nanobind-2.15.0/tests/test_classes.cpp` for a working example). `tp_traverse` must `Py_VISIT` every `nb::object` reachable from the PyModel: `py_view_cb`, each slot's Python callables (give `SlotBase` a virtual `void traverse(visitproc, void*)` and `void clear()` implemented by `SlotDerived`/`SlotTree`; others no-op), and anything in `PyModel` that holds `nb::object`. `tp_clear` resets them. Also `nb::type_slots` requires the instance to be tracked — nanobind handles `Py_TPFLAGS_HAVE_GC` when traverse slots are given.
2. Guard against the classic pitfall: `tp_traverse` runs without a guarantee the GIL matters — it's called by the GC with the GIL held; but `tp_clear` may drop the last reference to a callable whose destructor runs C++ code — fine. Ensure `SlotDerived`'s connections are disconnected in `clear()` before the callable is released so a recompute cannot fire into a cleared callable.
3. Tests first (subprocess, assert "leaked" not in stderr and returncode 0): module-global Model with (a) a `view()` override, (b) a `derived()`, (c) a `tree_field(source)`, each run via `_run_headless`; convert the two "pinned leak" tests from Task 8 into these passing tests. Also: `gc.collect()` after `del m` collects a Model with a self-referencing observer (`weakref` goes None) — this is the honest GC test.
4. Drop `_main()` from every remaining example (02-08 and 09-12 keep a `main()` only where a test imports it); README "Run" lines; remove the `_main()` recommendation comment in `__init__.py` entirely.
5. Full suite green in `builddir`; `python_bindings` green in `builddir-asan` (one dir at a time). Commit `fix(python): Model participates in cyclic GC; examples are plain scripts`.
