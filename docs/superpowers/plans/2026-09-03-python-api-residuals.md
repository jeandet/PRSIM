# Plan: close the residual minors after the 2026-09-03 follow-up series

Spec: `doc/review-2026-09-02-python-api.md`, second "Status" section ("Still open (minor)"), plus the deferred minors in that series' ledger. Base: `fd2dec8` (main, pushed).

## Global Constraints (binding for every task)

- Work directly on `main` in `/var/home/jeandet/Documents/prog/PRSIM`. No worktrees/branches. Never push (the controller pushes).
- **One build/test invocation at a time, in the foreground, waited to completion. NEVER background a build or test** — for long commands pass the Bash tool's `timeout` parameter (up to 600000 ms). Never overlap `builddir` and `builddir-asan`. Build `ninja -C builddir`; fast loop `meson test -C builddir python_bindings --print-errorlogs`; full suite `meson test -C builddir --print-errorlogs` before each commit; then `ninja -C builddir-asan && meson test -C builddir-asan python_bindings --print-errorlogs`. Report literal `Ok:`/`Fail:` lines.
- TDD: test first, shown failing, then fix, then both dirs green. Python tests: `python/tests/test_prism_python.py` (use `prism.headless()` for app-driven tests; subprocess pattern for exit/GC-at-shutdown tests).
- Commit per task; stage only the exact paths you changed (never `git add -A`/`-u`). Conventional prefix, body says why, end with exactly:
  `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_018r8oBsKu86vKbRDVbWm6vw`
- Style: KISS, small functions, comments only for non-obvious "why". GIL rule: every `nb::gil_scoped_release` guarded by `PyGILState_Check()`; queued closures never capture `nb::object` (use the `PyHolder` pattern in prism_ext.cpp if a Python object must cross).
- No subagents. Do not touch files outside the task's scope.

### Task 1: Observer callbacks become GC-visible (a Model with a self-capturing observer is collectable)

Files: `python/src/prism_ext.cpp`, `python/prism/__init__.py` (comment near `_disconnect_keepalive`/`_atexit_clear` only), `python/tests/test_prism_python.py`.

Today `m.x.observe(lambda v: m.rebuild())` forms `Model → _prism_fields → handle → __dict__["_prism_keepalive"] → Connection → (C++) SenderHub::receivers_ → std::function → nb::callable → Model`, and the last hop is invisible to the cyclic GC, so the Model lives until `run()` returns or atexit. Fix at the binding: `keep_connection()` (the helper every `observe*` uses) also stores the Python callback next to its Connection in the handle's `__dict__` — e.g. `__dict__["_prism_keepalive"]` becomes a list of `(connection, callback)` tuples, or a parallel `__dict__["_prism_callbacks"]` list; pick whichever keeps `_disconnect_keepalive` simplest and update it. The handle has `nb::dynamic_attr()`, so nanobind's dict traversal makes the cycle Python-visible; when the GC clears the dict the Connection is destroyed → `disconnect()` → the receivers_ entry (and its `nb::callable`) is freed under the GIL. Also make the `@on_change` trampoline path consistent (it already goes through `observe`). Tests first: (a) `m = M(); m.x.observe(lambda v: m); w = weakref.ref(m); del m; gc.collect(); assert w() is None` — fails before, passes after; (b) a subprocess test: module-global Model with a self-capturing observer, never run, process exits with no "leaked" in stderr; (c) the observer still fires normally under `prism.headless()` and stops after `disconnect()`. Rewrite the "documented residual" comment in `__init__.py` (~:219-233) to say the cycle is now GC-visible. Both dirs green. Commit `fix(python): observer callbacks are GC-visible; self-capturing observers no longer pin the Model`.

### Task 2: Sliders — orientation and runtime range

Files: `python/src/prism_ext.cpp`, `python/prism/__init__.py`, `python/tests/test_prism_python.py`, `python/examples/02_mixer.py` (only if it wants a vertical slider to showcase).

Investigate `Slider<T, O>` in include/prism/ui/delegate.hpp:470 — are min/max/orientation runtime members or template options? Then: `prism.slider(default, min, max, *, orientation="horizontal")` (validate the string → `ValueError`), and a `set_range(min, max)` on the slider handle (dispatched to the logic thread, owning `keep`, one post) with `.range` reflecting the new values after the post is applied (read via `dispatch_sync_read`). If orientation is a compile-time template option, instantiate both orientations and pick at allocation — no new widget code. Tests first: vertical slider renders headless; `set_range` from a background thread under `prism.headless()` → `.range` updated and a value set beyond the old max is accepted (Field semantics unchanged: unclamped). Docstrings updated (first line thread affinity). Both dirs green. Commit `feat(python): slider orientation and set_range()`.

### Task 3: Small correctness leftovers (one commit)

Files: `python/src/prism_ext.cpp`, `python/prism/__init__.py`, `python/tests/test_prism_python.py`.

1. Dedicated `PlotHandle` del-during-flight test: 300× create standalone plot handle (`prism.plot_field` standalone equivalent — check what exists: `PlotHandle` via `_prism_ext`), call `replace_series` from a background thread, immediately `del` + `gc.collect()`, under `prism.headless()`; run in both dirs.
2. `field_add_dispatch`'s inline (already-on-logic-thread) branch: mirror `field_set_dispatch`'s `Py_IsInitialized()`/GIL handling for symmetry, or document in one line why it is unnecessary — pick the one that keeps the two functions readable side by side.
3. `apply_validator`'s error path: if `nb::repr(result)` itself raises, fall back to `"<unrepresentable>"` so the `TypeError` is still the one raised (test: a validator returning an object whose `__repr__` raises).
4. `_ListDescriptor` class-level `observe_insert/remove/update(model, cb)` forms: emit the same `DeprecationWarning` as the scalar descriptors (test with `pytest.warns`); ensure `m.items.observe_insert(cb)` is the documented spelling.
5. `App.wait_until` docstring: state that `quit()` from another thread (including `__exit__`) makes an in-flight `wait_until` raise `RuntimeError("app quit before condition was met")` shortly after.
Both dirs green. Commit `fix(python): plot del-in-flight test, validator repr guard, list observe deprecation, docs`.

### Task 4: Docs — worker vs transaction, list ops atomicity

Files: `python/prism/__init__.py` (docstrings only), `python/examples/README.md`.

1. `worker()` docstring: writes from a worker are not batched — wrap related writes in `with prism.transaction():` inside `fn` (one sentence + a 3-line example in the README threading section).
2. `list_field()` docstring + README: every list op (`push/erase/set/replace_all`) is one posted, atomic operation on the logic thread; there is no read-modify-write hazard for list ops, and `len(...)`/`to_list()` are dispatched reads — so no `add()` analogue is needed.
3. Verify every public function's docstring still starts with its thread-affinity line (grep); fix any that drifted in the last series.
4. Carry-over from Task 1's review: `observe()` semantics changed — the callback is owned by the handle (`__dict__["_prism_callbacks"]`) and referenced weakly by the hub, so `conn = s.observe(cb); del s` leaves `conn` alive but inert. Add an `observe` docstring (on the nanobind `.def`s via a shared doc string constant, or in the README's threading section) stating: the subscription lives as long as the handle (or its Model); keep the handle to keep the observer; `disconnect()` ends it early. Also note in a code comment near `WeakCallback` that the `strong_` fallback only triggers for non-weak-referenceable callables (none on CPython 3.15 — builtins, bound methods and partials are all weak-referenceable; version-dependent).
`ninja -C builddir` + `python_bindings` once. Commit `docs(python): worker+transaction idiom, list ops atomicity`.
