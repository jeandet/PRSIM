# Plan: address the 2026-09-02 Python-API/threading review, then its proposals

Spec: `doc/review-2026-09-02-python-api.md` (the review). Where this plan and the review
disagree, the review's *finding* is authoritative; the *fix* suggested there is advisory.

Correction to the review (found during planning): a free-threaded CI lane already exists
(`.github/workflows/ci.yml:40-59`, `python-version: '3.14t'`, `builddir-ft`). So the
"add a CI lane" proposal collapses to: add a real multi-thread stress test to the suite and
assert `sys._is_gil_enabled() is False` when the interpreter is free-threaded.

## Global Constraints (binding for every task)

- Work directly on `main` in `/var/home/jeandet/Documents/prog/PRSIM`. No worktrees, no
  branches. Never push.
- **One build/test invocation at a time, in the foreground, waited to completion.** Never
  run `ninja`/`meson test`/`pytest` in the background, never run two against `builddir`
  concurrently. The build dir is `builddir` (already configured). Full suite:
  `meson test -C builddir --print-errorlogs`. Read the literal `Ok:`/`Fail:` lines and
  report them verbatim.
- TDD: write the reproducer/regression test first, show it failing, then fix, then full
  suite green. C++ tests are doctest files in `tests/` registered in `tests/meson.build`
  (`headless_tests` dict for backend-free tests). Python tests live in
  `python/tests/test_prism_python.py` (pytest, run by the `python_bindings` meson test with
  `PYTHONPATH=builddir/python`).
- Commit per task. Stage only the exact file paths you changed (`git add <path> ...`),
  never `git add -A`/`-u`. `doc/` and `docs/superpowers/` contain untracked files that must
  not be staged. Commit message: conventional prefix (`fix(core):`, `fix(python):`,
  `feat(python):`, `refactor(python):`, `docs:`, `test(python):`), body explaining why, and
  end with exactly these two trailer lines:
  `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_018r8oBsKu86vKbRDVbWm6vw`
- Style (from the user's global rules): KISS, small functions, one abstraction level per
  function, no comment-decorated blocks (extract a named function instead), comments only
  for links or non-obvious "why". A deliberate corner cut gets a `simplify:` comment.
- Do not touch files outside the task's listed scope. Do not spawn subagents.
- Python API must stay Pythonic: raise real exceptions with actionable messages; no
  silent fallbacks; public functions get a docstring whose first line states thread
  affinity ("Safe from any thread" / "Logic thread only").

## Part A — review findings

### Task 1: Drain guard — a throwing posted closure must not wedge `AppContext::post`

Files: `include/prism/app/model_app.hpp` (do_drain, ~lines 112-134),
`python/src/prism_ext.cpp` (`drain_queue_loop`, ~lines 45-58), new
`include/prism/core/error_hub.hpp`, `tests/test_mutation_queue.cpp`, `tests/meson.build`
only if a new test file is needed (prefer adding cases to `test_mutation_queue.cpp`).

1. Add `include/prism/core/error_hub.hpp` in namespace `prism::core`:
   ```cpp
   using ErrorHandler = std::function<void(std::exception_ptr)>;
   void set_unhandled_error_handler(ErrorHandler h);   // any thread; stores under a mutex
   void report_unhandled_error(std::exception_ptr e);  // calls handler, or default
   ```
   Default handler: rethrow, catch `std::exception&` and print
   `"[prism] unhandled exception in posted callback: " << e.what()` to stderr (`"<non-std exception>"` otherwise).
   Header-only with `inline` functions and an `inline` mutex/handler pair (this codebase is
   header-only).
2. In `do_drain` (model_app.hpp) wrap each `(*f)()` in try/catch → `report_unhandled_error(std::current_exception())`. Reset `detail_in_mutation_batch = false` and `sched_flag->store(false, release)` on every path (a small RAII scope guard struct or explicit catch — pick the smaller diff that is exception-correct). The loop must continue draining remaining closures after one throws.
3. Same treatment in `drain_queue_loop` in `prism_ext.cpp`.
4. Tests (doctest, `tests/test_mutation_queue.cpp`), written first and shown failing:
   - `post` a closure that throws `std::runtime_error("boom")`, then `post` a second closure that sets a flag; drain; the flag is set and the handler received exactly one exception whose `what()` is `"boom"`.
   - After the throwing drain, `post` again from the same thread → it is executed (proves `scheduled_` was reset).
   - `set_unhandled_error_handler(nullptr)` restores the default (no crash, prints).
5. Full suite green, commit `fix(core): guard mutation drain against throwing closures`.

### Task 2: Standalone `Shared*`/`Channel*` handles must be drained (observe() no longer a silent no-op)

Files: `python/src/prism_ext.cpp` (`SharedHandle`, `ChannelHandle`, `PyModel::drain` ~line 1066), `python/tests/test_prism_python.py`.

Context: `WidgetTree` calls `model.drain()` every tick (`include/prism/app/widget_tree.hpp:683-684`) → `PyModel::drain` → each slot's `drain()`. Standalone handles built with `nb::init<>` are in no `slots` vector, so their `drain_notifications()` never runs.

1. Add a process-global registry in `prism_ext.cpp`: `struct StandaloneDrainers { std::mutex m; std::vector<std::function<void()>*> fns; }` with `register_/unregister_` helpers. `SharedHandle`/`ChannelHandle` (all four T) own a `std::function<void()> drain_fn` member, register in the constructor, unregister in the destructor; delete copy, keep move only if trivial to keep registration correct — simplest correct option: delete both copy and move (nanobind holds the object in place).
2. `PyModel::drain` also calls `drain_standalone()` (copy the pointer list under the lock, then call outside it).
3. Python test, written first and shown failing: model with one field; standalone `prism.SharedInt(0)` with `observe(cb)`; background thread sets `.value = 5`; `prism._run_headless(model, delay_ms=50)`; assert `cb` was called with `5`. Same for `prism.ChannelInt()` with `send(7)`.
4. Docstring on the Python `SharedInt`/`ChannelInt` classes is not editable from C++ `.def` easily — instead add one sentence to `python/prism/__init__.py`'s `shared()`/`channel()` docstrings (Task 7 owns full docstrings; here just add: "Standalone handles are drained on every app tick while an app runs.").
5. Full suite green, commit `fix(python): drain standalone Shared/Channel handles on app tick`.

### Task 3: Remove the atexit `sys.modules` sweep

Files: `python/prism/__init__.py` (`_atexit_clear`, ~lines 380-410), `python/examples/07_file_tree.py` only if its comment references the sweep, `python/tests/test_prism_python.py`.

1. Delete the block that iterates `sys.modules` and deletes globals bound to Model instances (`__init__.py:387-410`). Keep the `_all_models` WeakSet disconnect pass and the keepalive clearing.
2. Replace with: nothing. A Model left in a module global at interpreter exit may make nanobind print a leak warning; that is acceptable and honest. Update the comment above `_atexit_clear` to say so in two lines.
3. Test (written first): `test_import_has_no_sys_modules_sweep` — `inspect.getsource(prism._atexit_clear)` does not contain `sys.modules` or `modules.values`. (Behavioral atexit tests are not practical; this pins the contract.)
4. Full suite green, commit `fix(python): stop deleting user globals at exit`.

### Task 4: No silent fallbacks in the Python layer

Files: `python/prism/__init__.py`, `python/tests/test_prism_python.py`.

1. `_DerivedDescriptor._allocate` (~lines 600-680): the user function is probed to infer the value type. Rule: if `type_hint` is given, never call the probe. If the probe raises, raise `TypeError(f"derived '{self.name}': probe call raised {exc!r}; pass type_hint=int|float|str|bool to skip probing")` chained with `from exc`. Remove the `probe = 0` fallbacks.
2. `field()`/`shared()`/`channel()` with an unsupported default (list, dict, None, object): raise `TypeError("prism.field(): unsupported default type list; use list_field() for lists")` (message names the actual type and, for list, points to `list_field()`). Do this in one shared helper `_kind_of(value) -> str` returning `"bool"|"int"|"float"|"str"` (bool checked before int), used by `_FieldDescriptor`, `_SharedDescriptor`, `_ChannelDescriptor`, `_ListDescriptor` in place of the four duplicated isinstance cascades (`__init__.py:433-442, 506-515, 554-564, 747-757`). Allocation becomes `getattr(instance, f"_add_{prefix}{kind}_internal")(...)`.
3. Audit every remaining `except Exception` in `__init__.py` (there are ~30). Keep only those inside `_atexit_clear`/`_clear_model_observers` (shutdown must not raise) and the pydantic import guard in `validator_for`. Every other one is either removed or narrowed to the specific exception with a comment naming why it is expected. List each decision in the report.
4. `Model.__init__` kwargs loop (~lines 1007-1013): both branches call `setattr`; collapse to one loop.
5. Tests written first: derived probe raising → `TypeError` with the `type_hint=` hint; derived with `type_hint=float` and a probe that would raise → allocates without calling the probe; `field([1,2])` → `TypeError` mentioning `list_field()`; `field(None)` → `TypeError`; `M(a=5)` kwargs override sets the field.
6. Full suite green, commit `fix(python): raise instead of silently falling back`.

### Task 5: Docs — true statements about locking and transactions

Files: `README.md`, `doc/design/python-sdk.md` (if it makes lock-free/rollback claims), `python/examples/README.md`.

No build required; run nothing.

1. `README.md:373` and `:586` (grep `lock-free`): `Shared<T>` uses `std::atomic<std::shared_ptr>` which is mutex-backed on libstdc++. Reword every occurrence to "atomic (readers never block writers; implementation is `std::atomic<std::shared_ptr>`, not guaranteed lock-free)". Keep the sentence short.
2. Add a "Threading guarantees" subsection to `README.md` (near the existing threading/Shared section) with this text, verbatim:
   > One logic thread owns the widget tree and every `Field<T>`; `Field<T>` is not thread-safe. Any thread may call `Shared<T>::set()`, `Channel<T>::send()`, or post a closure; these are safe under arbitrary concurrency. `Shared<T>` publishes only the latest value — intermediate values are dropped by design. `Channel<T>` is lossless and per-producer FIFO. A posted closure wakes an idle logic thread exactly once per burst. Exceptions thrown by posted or observed callbacks are routed to `prism::core::set_unhandled_error_handler` (default: printed to stderr) and never stop the drain. `transaction()` batches and coalesces notifications on the calling thread; it does not roll back on exception.
3. `python/examples/README.md`: add the same paragraph in Python terms (`prism.shared`, `prism.channel`, `prism.transaction()`, `prism.on_error()` — the last one lands in Task 11; write "`prism.on_error()`" now).
4. Commit `docs: correct lock-free claim, state threading guarantees`.

### Task 6: Binding-layer hardening

Files: `python/src/prism_ext.cpp`, `include/prism/core/derived.hpp`, `tests/test_derived.cpp`, `python/tests/test_prism_python.py`, `include/prism/app/model_app.hpp` (one comment).

1. `try_post_via_handle_impl` (~lines 65-68) and `dispatch_sync_read` (~lines 128-131): the up-to-1000×1ms spin-wait runs with the GIL held. Wrap the spin loop in `nb::gil_scoped_release` (only when `Py_IsInitialized()`; never release a GIL you do not hold — these are called from Python-entered code, so it is held).
2. Deferred reader closures that do `nb::gil_scoped_acquire` (`BoundTree::rows`, `BoundList::to_list`, ~lines 455-469, 676-679): re-check `Py_IsInitialized()` immediately before acquiring; if false, return the default value.
3. `include/prism/core/derived.hpp`: `Derived<T>` subscribes with `[this]` captures; add `Derived(Derived&&) = delete; Derived& operator=(Derived&&) = delete;` with a one-line "why" comment referencing the SenderHub move hazard comment in `connection.hpp`. Test first in `tests/test_derived.cpp`: `static_assert(!std::is_move_constructible_v<Derived<int>>)`. Build must still pass — if any in-tree code moves a `Derived`, fix that call site by constructing in place (report it).
4. `observe*()` on an unbound handle (`field == nullptr` etc., ~lines 507, 546, 580, 636, 718-727) currently returns `Connection{}`; throw `nb::value_error("observe(): handle is not bound to a Model")` instead. Python test: constructing such a state from Python is not possible via public API — so cover it with a C++-side check only if reachable; otherwise document in the report that it is unreachable from Python and keep the throw as defense.
5. `model_app.hpp` at the `mutation_queue` declaration (~line 177): one comment line: `// Destroyed by run() with the GIL released — queued closures must never capture nb::object.`
6. Full suite green, commit `fix(python): release GIL in startup spin, re-check interpreter, forbid Derived moves`.

### Task 7: Python surface cleanup and docstrings

Files: `python/prism/__init__.py`, `python/tests/test_prism_python.py`.

1. `tree_field` signature (~line 826): `source: "TreeSource | dict | Callable[[], TreeSource | dict] | None" = None`; import `Callable` from `collections.abc`. Remove the `# type: ignore[name-defined]` if it is no longer needed (verify by reasoning; mypy is not installed locally).
2. `TreeSource` docstring (~line 62): replace "Only the six methods below are required" with "The six methods below define the contract; note the C++ side currently substitutes defaults (0 / str(id) / False) for a missing method rather than raising — implement all six." `TableSource` docstring (~lines 99-100): replace the "C++ falls back to" sentence with "`header` is optional in the C++ `TableSource` struct (the consumer substitutes `\"\"`); the C++ `ColumnStorage` concept itself requires it. No Python binding exists yet."
3. Docstrings (first line = thread affinity) for: `field`, `slider`, `checkbox`, `shared`, `channel`, `list_field`, `derived` (keep existing body, prepend affinity line), `plot_field`, `transaction`, `run`, `_run_headless`, `Model.observe`. Affinity facts: `field/slider/checkbox` values may be set from any thread (posted to the logic thread); `shared`/`channel` any thread; `derived` read-only, logic thread; `transaction()` buffers per calling thread and flushes on exit, no rollback; `run()` blocks the calling thread until the window closes and releases the GIL.
4. `_allocate` in every descriptor: add one comment above the eager-allocation loop in `Model.__init__` (~line 990): `# Eager allocation here is what makes the check-then-act in _allocate safe: no other thread can see the instance before __init__ returns.`
5. `@runtime_checkable`: keep it (cheap, harmless) but add one test each that `isinstance(DictLikeSource(), prism.TreeSource)` is True for a duck-typed object and False for one missing `child_at`.
6. Tests written first for the `Annotated` auto-field path (`x: Annotated[int, Field(ge=0)] = 0` without `prism.field()`, ~lines 934-982) and for validator rejection end-to-end (`.value = -1` raises).
7. Full suite green, commit `refactor(python): docstrings with thread affinity, accurate signatures, tests`.

### Task 8: Collapse the nanobind registration duplication

Files: `python/src/prism_ext.cpp` (~lines 1080-1394).

1. Introduce one templated helper per family, e.g. `template <typename T> void bind_scalar(nb::module_& m, const char* suffix)` registering `FieldHandle<T>`, `BoundField<T>`, `SharedHandle<T>`, `BoundShared<T>`, `ChannelHandle<T>`, `BoundChannel<T>`, `BoundDerived<T>` with names `("Field" + suffix)` etc., and `bind_list<T>` for `ListHandle<T>`/`BoundList<T>`. Keep exact Python class names, method names, `nb::arg` names/defaults, `nb::dynamic_attr()`, `nb::is_weak_referenceable()`, and keep_alive annotations identical to today — this is a pure move.
2. Prove no API change: before refactoring, dump `sorted(dir(prism._prism_ext))` plus, for each class, `sorted(dir(cls))` to a file in `.superpowers/sdd/2026-09-02-python-api-review-fixes/api-before.txt` (via a one-off `python -c` with `PYTHONPATH=builddir/python`, after a build). After refactoring and rebuilding, dump again to `api-after.txt` and `diff` them — must be empty. Include the diff command output in the report.
3. Full suite green, commit `refactor(python): template the scalar handle bindings`.

### Task 9: Move observe keepalive into C++ and delete the monkey-patch

Files: `python/src/prism_ext.cpp`, `python/prism/__init__.py` (`_patch_bound_observe`, `_keepalive_by_handle`, and their uses in `_clear_model_observers`/`_atexit_clear`), `python/tests/test_prism_python.py`.

Context: since 420630f all handle classes have `nb::dynamic_attr()` and the Python wrapper stores each `Connection` in `handle.__dict__["_prism_keepalive"]` (a list). Read `_patch_bound_observe` and `_clear_model_observers` first to see the exact attribute name and how atexit disconnects them; keep that attribute name so atexit keeps working.

1. In `prism_ext.cpp`, for every `observe`/`observe_insert`/`observe_remove`/`observe_update` `.def`, replace the member-pointer binding with a lambda taking `nb::object self` (plus the callback), casting `self` to the handle type, calling the member, then appending the returned `Connection` to `self.attr("__dict__")["_prism_keepalive"]` (create the list if missing). Return the Connection. A tiny helper `nb::object keep(nb::object self, Connection c)` avoids repeating this. After Task 8 there is one place per family to change.
2. Delete `_patch_bound_observe` and `_keepalive_by_handle` from `__init__.py`; keep `_all_models`/atexit disconnect logic operating on `handle.__dict__["_prism_keepalive"]`.
3. Tests written first: `from prism._prism_ext import FieldInt` (bypassing the package) — `FieldInt(1).observe(cb)` then set value → `cb` fires (proves keepalive is C++-side); `handle.__dict__["_prism_keepalive"]` has length 1 after one observe; `conn.disconnect()` then set → not fired.
4. Full suite green, commit `refactor(python): keepalive in C++ observe(), remove monkey-patch`.

### Task 10: Remaining tests from the review's coverage list

Files: `python/tests/test_prism_python.py`, `python/tests/` helper module if needed.

Add tests (that are not already covered by Tasks 2-9): `tree_field` with a duck-typed source object (six methods) — `model.tree.rows()` returns the root count after allocation; `tree_field(lambda: {...})` callable factory works; `TreeSource` object missing a method → document current behavior in the test name (`test_tree_source_missing_method_yields_empty` — assert whatever the C++ does today, so a future change is deliberate). Full suite green, commit `test(python): tree_field, TreeSource conformance`.

## Part B — proposals

### Task 11: `prism.on_error()` hub

Files: `python/src/prism_ext.cpp`, `python/prism/__init__.py`, `python/tests/test_prism_python.py`, `python/examples/README.md` (one paragraph).

1. C++: expose `m.def("_set_error_handler", ...)` taking `nb::object` (callable or None). Store it in a global guarded by a mutex. Install a `prism::core::set_unhandled_error_handler` (from Task 1) that, if the Python handler is set and `Py_IsInitialized()`, acquires the GIL and calls it with a Python exception object built from the `std::exception_ptr` (`nb::python_error` → its `.value()`; std::exception → `RuntimeError(what())`); otherwise falls back to the default stderr print.
2. Every Python callback wrapper in `prism_ext.cpp` that currently does `catch (nb::python_error&) { PyErr_Print(); } catch (...) {}` (grep `PyErr_Print`) now routes to the same hub instead of printing (if no handler: print as before). Never swallow `catch (...)` silently — report it.
3. Python: `prism.on_error(handler: Callable[[BaseException], None] | None) -> None` with docstring "Any thread. Called on the logic thread with the exception raised by an observer/derived/worker callback. None restores the default (traceback to stderr)." Export in `__all__`.
4. Tests written first: observer callback raising `ValueError("x")` → `on_error` handler receives a `ValueError` with args `("x",)`, and the app keeps running (a subsequent set still fires a second observer); `on_error(None)` restores default (no crash).
5. Full suite green, commit `feat(python): prism.on_error() error hub`.

### Task 12: `prism.worker()` helper and example de-duplication

Files: `python/prism/__init__.py`, `python/tests/test_prism_python.py`, `python/examples/02_mixer.py`, `06_live_plot.py`, `08_dashboard.py`.

1. Read the three examples' background-thread + weakref pattern first; the helper must replace it without changing behavior.
2. `prism.worker(fn, *, interval: float | None = None, daemon: bool = True, name: str | None = None) -> Worker`: starts a thread running `fn(stop: threading.Event)` once, or every `interval` seconds until `stop` is set. `Worker` has `.stop()` (sets the event, joins with timeout 1s) and is a context manager. `run()`/`_run_headless()` stop all live workers on exit (keep a module-level `WeakSet` of workers). Exceptions in `fn` go to `prism.on_error` (Task 11).
3. Rewrite 02/06/08 to use it; each example gets shorter. Keep their observable behavior.
4. Tests written first: worker with `interval=0.01` increments a counter, `.stop()` joins, counter stops increasing; worker fn raising → `on_error` receives it and the worker thread exits.
5. Full suite green, commit `feat(python): prism.worker() background helper; examples use it`.

### Task 13: Example set — shared helper module, headless stress, error handling

Files: new `python/examples/tree_sources.py`, `python/examples/07_file_tree.py`, `08_dashboard.py`, new `09_headless_multithread_stress.py`, new `11_error_handling.py`, `python/examples/README.md`, `python/tests/test_prism_python.py`.

1. Move `DictTreeSource`, `FsTreeSource`, `TREE_DATA` from 07 into `python/examples/tree_sources.py`; 07 and 08 `import tree_sources` (the script directory is on `sys.path` when run as a script). Delete the `importlib` block in 08.
2. `09_headless_multithread_stress.py`: N=8 threads (`concurrent.futures.ThreadPoolExecutor`) each doing 1000 `shared.value = i` sets, 1000 `channel.send(i)` sends, and 100 `with prism.transaction(): field.value += 1` — on a Model with one `shared`, one `channel`, one `field`, one `derived`; run via `prism._run_headless(model, delay_ms=300)`; at the end assert channel count == 8000, field == 800, and print `sys._is_gil_enabled()` when available. Must run to completion in < 10 s without a display. Add a pytest test that imports it via `importlib` and runs its `main()`; mark it with the existing headless mechanics; also assert `sys._is_gil_enabled() is False` when `sysconfig.get_config_var("Py_GIL_DISABLED")` is truthy (this is what the CI `3.14t` lane will check).
3. `11_error_handling.py`: observer that raises on odd values, worker that raises once; `prism.on_error` logs and counts; app keeps running (headless-capable: if `--headless` is passed use `_run_headless`).
4. Update `python/examples/README.md` table and the "Run" lines. Docstring template for every example: one-line title, "Demonstrates:" bullets, "Run:" line.
5. Full suite green, commit `feat(examples): shared tree sources, headless stress, error handling`.

### Task 14: Examples — worker-pool plot and asyncio bridge

Files: new `python/examples/10_worker_pool_plot.py`, new `12_asyncio_bridge.py`, `python/examples/README.md`.

1. `10_worker_pool_plot.py`: `ThreadPoolExecutor(max_workers=4)` computing a spectrum (pure-Python FFT via `cmath`, no numpy) per window; results via `prism.channel` into a `plot_field`; status shows windows/sec so the free-threaded speedup is visible. Uses `prism.worker` for the producer.
2. `12_asyncio_bridge.py`: an asyncio loop in a `prism.worker`; `asyncio.run_coroutine_threadsafe` from an observer; a coroutine feeding a `prism.channel`.
3. Both GUI examples cannot be run headlessly here; verify they import cleanly (`python -c "import importlib.util,..."` with `PYTHONPATH=builddir/python`) and that a `--headless` flag runs them for 1 s via `_run_headless`.
4. README table rows. Commit `feat(examples): worker-pool plot, asyncio bridge`.

### Task 15: Validators must apply on the `.value =` path too

Files: `python/src/prism_ext.cpp`, `python/prism/__init__.py`, `python/tests/test_prism_python.py`.

Found during Task 7: `prism.field(0, validator=...)` validates only through the descriptor's
`__set__` (`m.count = -1`), not through the handle (`m.count.value = -1`) — and `.value =` is
the idiomatic path every example uses. Investigate first: where does the descriptor call the
validator, and what does the C++ `value` setter do. Pick the smallest correct design:
preferred — the Python `_FieldDescriptor._allocate` installs the validator on the handle
(e.g. `handle.__dict__["_prism_validator"]`; handles have `dynamic_attr`) and the C++ `value`
setter / `set()` call it (GIL held) before `field_set_dispatch`, raising the validator's
exception unchanged. Whatever the design, `m.x = v` and `m.x.value = v` and `m.x.set(v)` must
behave identically, from any thread. Tests first: all three paths reject `-1` with the same
exception type/message; all three accept `1`; a rejected set leaves the value unchanged.
Full suite green, commit `fix(python): validate on handle .value/.set as well as descriptor`.

### Task 16: Fix the `derived` + `_run_headless()` teardown segfault

Files: `python/src/prism_ext.cpp`, `python/prism/__init__.py` (only if the fix is there), `python/tests/test_prism_python.py`, `python/examples/09_headless_multithread_stress.py` (restore the `derived` field once fixed), `python/examples/README.md` (drop the workaround note).

Found during Task 13. Reproducer (no threads, no custom `view()`):
```python
class M(prism.Model):
    counter = prism.field(0)
    doubled = prism.derived(lambda self: self.counter.value * 2, "counter")
m = M()
prism._run_headless(m, delay_ms=200)   # segfaults at teardown
```
Steps: (1) add this as a pytest test first and confirm it crashes the test process (run it in a subprocess via `subprocess.run([sys.executable, "-c", ...])` and assert returncode == 0 so the suite survives the failing state); (2) find the root cause — likely `SlotDerived<T>`/`BoundDerived` lifetime vs the headless app teardown (project history notes a "SlotDerived cross-thread UAF"): use `gdb -batch -ex run -ex bt --args python3 -c "..."` on the repro to get the crashing frame, and read `SlotDerived`, its `Connection`s to its dependency fields, and the order in which `PyModel::slots` and the app's widget tree are destroyed; (3) fix the root cause (not the symptom — e.g. disconnect/destroy order or holding the dependency Slot alive via `shared_ptr` in `SlotDerived`), (4) restore `derived` in the stress example and README, full suite green, commit `fix(python): derived field survives headless app teardown`.
