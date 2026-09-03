# PRISM whole-repo review — Python API, threading story, examples

Date: 2026-09-02. HEAD: 69f36df. Method: 4 scoped read-only review agents (Python layer,
nanobind layer, C++ core threading, examples/docs) + independent verification of every
HIGH/BLOCKER claim against the code by the lead reviewer. No builds were run beyond the
existing up-to-date builddir (74/74 tests green at 3f59866, re-verified).

## TL;DR

The core is sound: SenderHub emit/connect/disconnect reentrancy, mpsc_queue wake-up,
Shared/Channel drain, shutdown ordering and post-after-close are all verified correct and
tested. The Python API works and the examples are clean. What stands between the current
state and "a showcase of a truly noGIL/multi-thread-friendly UI SDK" is:

1. **The free-threaded build is exercised only by CI, and by no threading-specific test.**
   The nanobind meson wrap auto-defines `NB_FREE_THREADED` when `Py_GIL_DISABLED=1`
   (`subprojects/nanobind-2.15.0/meson.build:40-42`), and `.github/workflows/ci.yml:40-59`
   already builds and runs the suite on `3.14t` (correction: an earlier draft of this
   report said no such lane existed). But the local interpreter is GIL-enabled 3.15.0rc1,
   and the suite has no multi-thread stress test nor an assertion that the GIL is actually
   disabled after `import prism` on that lane — so the noGIL claim is built, not tested.
2. **Silent failure is the house style of the Python layer.** ~30 blanket
   `except Exception: pass` blocks in `python/prism/__init__.py`; C++ core drain has no
   try/catch and wedges the scheduler forever on a throw; `observe()` on standalone
   Shared/Channel handles is a silent no-op; unbound handles return an empty `Connection{}`.
   For a threading SDK, "errors disappear" is the opposite of what users need.
3. **Import-time global side effects** (monkey-patching nanobind classes, an atexit hook
   that deletes user globals from every module in `sys.modules`) exist only to keep
   nanobind's leak checker quiet. They leak implementation detail into user space.
4. The README promises "lock-free" `Shared<T>`; the implementation is
   `std::atomic<std::shared_ptr>` which is mutex-backed on libstdc++.

## Findings (verified)

### BLOCKER / HIGH

| # | Sev | Where | Finding | Fix |
|---|-----|-------|---------|-----|
| 1 | HIGH | `include/prism/app/model_app.hpp:117-134`, `python/src/prism_ext.cpp:45-58` | `do_drain` calls `(*f)()` with no try/catch. A throwing posted closure or C++ `on_change` callback unwinds past `sched_flag->store(false)`, leaving `scheduled_ == true` forever → every later `post()` CAS fails silently; app stops reacting, no log. `detail_in_mutation_batch` also stays `true`. Python callbacks are individually guarded so this mainly hits C++ users and `Derived` compute lambdas — but it is the foundation the Python story stands on. | Scope-guard the flag reset; catch, record, and surface the exception (error hub / `std::terminate` in debug). Add a test: throwing closure, then assert next post still drains. |
| 2 | HIGH | `python/src/prism_ext.cpp:527,562` vs `:304,311,1066` | Standalone `prism.SharedInt(...)` / `prism.ChannelInt()` (constructed via `nb::init<>`) are never drained — `drain_notifications()` is only called through `SlotShared/SlotChannel::drain` from `PyModel::drain`. `observe()` on a standalone handle never fires. Overlaps the "observe() silent no-op" item in `doc/review-2026-09-01-followup.md`. | Either register standalone handles with the running app's drain list, or make `observe()` raise `RuntimeError("standalone Shared/Channel handles are not drained; bind them to a Model")`. Silent is the wrong option. |
| 3 | HIGH | `python/prism/__init__.py:387-410` | `import prism` registers an atexit hook that iterates **every module in `sys.modules`** and `del`s any global bound to a `Model` instance, purely to dodge nanobind's leak check. This is a process-wide side effect of an import; it will also fire for user modules and for embedding hosts (Jupyter, plugin hosts). | Remove. Keep the `_all_models` WeakSet disconnect pass; accept nanobind's leak warning in the `__main__`-global case, or set `nb::set_leak_warnings(false)` explicitly for the shutdown path. Examples already use `_main()` scoping. |
| 4 | HIGH | `python/prism/__init__.py:614-670` and ~30 sites (`grep -n "except Exception" python/prism/__init__.py`) | `derived()` type probing swallows any exception from the user function and substitutes `0`; the same blanket-catch pattern is used throughout descriptor allocation, keepalive, atexit. A wrong-arity lambda or AttributeError silently produces an `int` derived field. | Catch only during the first probe when no `type_hint` was given, and re-raise with "pass type_hint=" context. Audit the other 29 sites; keep only the atexit ones. |
| 5 | HIGH (docs) | `README.md:373,586` vs `include/prism/core/atomic_cell.hpp:13` | "lock-free `Shared<T>`" — `std::atomic<std::shared_ptr<const T>>::is_lock_free()` is `false` on this toolchain (mutex/spinlock table). Not a correctness bug; a false guarantee in a real-time/noGIL pitch. | Reword to "atomic, wait-free for readers" only if true; otherwise "mutex-light". Or implement a seqlock/double-buffer cell if lock-freedom is load-bearing. |

### MEDIUM

| # | Where | Finding | Fix |
|---|-------|---------|-----|
| 6 | `prism_ext.cpp:65-68,128-131` | Startup spin-wait (up to 1000×1ms) runs with the caller's GIL held → freezes all Python threads up to ~1s during the run-setup race. | Wrap the spin in `nb::gil_scoped_release`. |
| 7 | `prism_ext.cpp:455-469,676-679` | Deferred closures call `nb::gil_scoped_acquire` without re-checking `Py_IsInitialized()` at execution time (only at schedule time). Narrow shutdown window. | Re-check immediately before acquire. |
| 8 | `include/prism/core/derived.hpp` | `Derived<T>` captures `this` in its source subscriptions but has an implicitly-generated move ctor/assignment → UAF if ever moved. Latent (not moved anywhere today); mirror of the documented SenderHub hazard, undocumented here. | `Derived(Derived&&) = delete;` |
| 9 | `python/prism/__init__.py:191-270` | `import prism` monkey-patches `observe*` onto the nanobind classes process-wide. Bypassing `prism` (`from prism._prism_ext import ...`) gets the unpatched, leak-cycle-prone methods. | Move keepalive into the C++ `observe()` (the per-handle `__dict__` list from 420630f can be created C++-side via `nb::dict`), or document the patch as a permanent invariant at the top of the file. |
| 10 | `python/prism/__init__.py:433-442,506-515,554-564,747-757` | bool/int/float/str dispatch cascade duplicated 4× (Field/Shared/Channel/List descriptors). | One `_kind_of(value)` helper + `getattr(instance, f"_add_{prefix}{kind}_internal")`. |
| 11 | `prism_ext.cpp:1080-1394` | ~320 lines of copy-pasted `nb::class_<X<int/double/string/bool>>` registrations. | One templated `register_scalar_handles<T>(m, "Int")` helper; halves the file. |
| 12 | `python/prism/__init__.py:826` (from 4572767) | `tree_field(source: TreeSource \| dict \| None)` omits the supported zero-arg callable factory (`_TreeDescriptor._allocate:804-808`). | Add `Callable[[], TreeSource \| dict]`. |
| 13 | `python/prism/__init__.py:62,99-100` | Docstrings say the six TreeSource methods are "required" (C++ hasattr-falls-back on all six, silently) and that `TableSource.header` "falls back" in C++ (no Python table binding exists; `ColumnStorage` concept at `table.hpp:35-40` requires `header`). | Match docs to behavior, or make C++ raise on a missing required method. |
| 14 | `python/prism/__init__.py:427-444` | Descriptor `_allocate` is check-then-act without a lock; safe today only because `Model.__init__` eagerly allocates before the instance escapes. Latent under free-threading for any lazy path. | Document the invariant next to the eager loop; or `setdefault` a sentinel. |

### LOW

- `prism_ext.cpp:507,546,580,636`: `observe()` on an unbound handle returns `Connection{}` silently → raise instead.
- `__init__.py:1007-1013`: kwargs loop `if/else` arms are identical (dead branch).
- `__init__.py:1048-1053,471-491`: `transaction()`, `run()`, `field()`, `slider()`, `checkbox()`, `shared()`, `channel()` have no docstrings; thread-affinity is documented only in `python/examples/README.md`.
- `field([1,2])` fails with `int() argument must be...` instead of "use `list_field()`".
- `@runtime_checkable` on `TreeSource`/`TableSource` is unused (no `isinstance` anywhere).
- `transaction()` is not rollback; the name invites that expectation → one doc sentence.
- `08_dashboard.py:24-34` imports `07_file_tree.py` via `importlib.util.spec_from_file_location` because module names start with digits.
- Zero tests for: `Annotated` auto-fields (`__init__.py:934-982`), constructor kwargs, `derived()` raising, validator rejection end-to-end, unsupported `field()` defaults, `tree_field`/`TreeSource`/`TableSource`.
- `mutation_queue` is destroyed while `run()` holds the GIL released (`prism_ext.cpp:1444-1499`). Safe today (no `nb::object` is captured in queued closures) but unenforced → comment at the queue declaration.

### False positives / retractions

- **Retracted**: the lead reviewer's mid-review statement that the module "carries no
  free-threading declaration so importing on 3.13t re-enables the GIL" was wrong. nanobind's
  meson wrap adds `-DNB_FREE_THREADED` automatically when the target Python has
  `Py_GIL_DISABLED`. The real gap is that no free-threaded build/test exists (TL;DR #1).
- `TreeNodeId = int` vs C++ `uint64_t`: intentional; `root_at`/`child_at` cast through
  `int64_t` for negative Python hashes (`prism_ext.cpp:381-384`).
- `dispatch_sync_read` future wait is correctly wrapped in `gil_scoped_release`
  (`prism_ext.cpp:169-171`) — no GIL deadlock.
- Global state table (binding layer): `g_post_handle` (mutex), `g_has_handle`/`g_run_guard`/
  `g_app_closed` (atomics), `txn_*` (thread_local), `PyModel::slots` (mutex) — all protected.

## Actual threading guarantees (proposed doc text)

One logic thread owns the widget tree and every `Field<T>`; `Field<T>` is not thread-safe.
Any thread may call `Shared<T>.set()`, `Channel<T>.send()`, or post a closure; these are safe
under arbitrary concurrency. `Shared<T>` publishes only the latest value — intermediate
values are dropped by design. `Channel<T>` is lossless and per-producer FIFO. A posted
closure wakes an idle logic thread exactly once per burst (verified, no missed-wakeup
window). Posted and observed callbacks must not throw (finding 1). `transaction()` batches
and coalesces notifications on the calling thread; it does not roll back on exception.

## Python API design proposals

1. **Errors are data, not silence.** Replace blanket catches with (a) narrow catches at the
   derived probe, (b) a `prism.on_error(callback)` hub fed by both the C++ drain guard and the
   Python callback wrappers, defaulting to `traceback.print_exception` + `logging`. This is
   the single change that most improves the "trust it with threads" feel.
2. **Make thread-affinity explicit on the surface.** One-line docstrings on every public
   function stating "any thread" / "logic thread only" / "buffered per-thread". Consider a
   `prism.logic_thread_only` decorator for user methods so misuse raises instead of racing.
3. **Kill import-time magic.** Remove the `sys.modules` sweep (3); move keepalive into C++
   `observe()` (9) so the binding boundary is clean and `from prism._prism_ext import X` is
   not a footgun.
4. **Standalone handles either work or refuse** (2). Prefer: standalone Shared/Channel
   register with the app on `observe()` and get drained; raise before `run()` if none.
5. **Collapse duplication** (10, 11) — removes ~400 lines across the two files with zero
   behavior change and makes adding a new scalar type a one-liner.
6. **Add a free-threaded CI lane**: build against `python3.14t`, run the test suite with a
   stress test that mutates from N threads while the logic thread drains, and assert
   `sys._is_gil_enabled() is False` after `import prism`. Until this exists the noGIL claim
   should be worded as "designed for" not "supports".
7. **Background worker helper.** Examples 02/06/08 repeat the same weakref-to-Model daemon
   thread boilerplate. A `prism.worker(fn, *, interval=None, daemon=True)` that hands the
   function a weak-safe model proxy and stops on app close would make the threading story
   read as first-class.

## Example set proposals (prioritized)

1. `09_headless_multithread_stress.py` — `run_headless`, N producer threads hammering
   `Shared`/`Channel`/`transaction`, assert final state; the CI-able proof of the pitch.
2. `10_worker_pool_plot.py` — `concurrent.futures.ThreadPoolExecutor` computing spectra in
   parallel, results via `Channel` into `plot_field`; show wall-clock speedup on a `t` build.
3. `11_error_handling.py` — observers and workers that raise; demonstrate `on_error` hub and
   that the app keeps running (pairs with finding 1).
4. `12_asyncio_bridge.py` — asyncio loop in a thread, `run_coroutine_threadsafe` ↔ `Channel`.
5. `13_table.py` — once `table_field` lands (TableSource is already typed).
6. `14_graceful_shutdown.py` — stop flag + join with timeout + app close ordering.
7. Rename examples to importable names (`counter.py` … or a package with `__main__`) and
   drop the `importlib` hack in 08; one `_main()` + docstring template across all.

## Suggested order of work

1. Finding 1 (drain guard + test) — small, foundational.
2. Findings 3, 4, 9 — remove import-time magic and blanket catches; add `on_error`.
3. Finding 2 — standalone handle drain/refuse.
4. Free-threaded CI lane + example 09.
5. Duplication collapse (10, 11) as a mechanical refactor with the suite green before/after.
6. Docs: README lock-free wording (5), threading-guarantees paragraph, docstrings.

## Status — 2026-09-03, series 8e5f70d..75256da (23 commits, 74/74 at every commit)

Landed (plan: `docs/superpowers/plans/2026-09-02-python-api-review-fixes.md`):
drain guard + `prism::core::error_hub` (HIGH 1) · standalone Shared/Channel drained via a weak_ptr registry (HIGH 2) · atexit `sys.modules` sweep removed (HIGH 3) · no silent fallbacks, `_kind_of` helper, 30→4 blanket catches (HIGH 4) · README lock-free wording + threading-guarantees text (HIGH 5) · GIL hardening (MEDIUM 6/7; one regression from it — a segfault on the GIL-released initial build — found by the new stress example and fixed with `PyGILState_Check`) · `Derived` moves deleted (8) · keepalive in C++ `observe()` + `_observed_handles` WeakSet, monkey-patch gone (9) · descriptor dedup (10) · templated bindings, net −66 lines (11) · docstrings with thread affinity, `tree_field` signature (12/13/14) · validators now apply on `.value`/`.set()` too (new, found in Task 7) · `prism.on_error()` · `prism.worker()` · examples: `tree_sources.py`, 09 headless stress (CI-able; asserts GIL disabled on 3.14t), 10 worker-pool plot, 11 error handling, 12 asyncio bridge.

Still open (deferred, all Minor unless noted):
- A standalone handle that drops *itself* inside its own `drain_notifications()` callback would still UAF (pre-existing).
- `view()` + `derived` headless-teardown "Invalid argument at exit" noted in 02/05 docstrings — a different, still-open bug.
- No `plot.replace_series(xs, ys)` single-post primitive; example 10 JSON-encodes spectra over a `channel("")` as a workaround.
- The standalone-drain UAF regression test only crashes under ASan — add an ASan lane or `b_sanitize=address` run to CI.
- `nb::list()` fallback in post-finalize readers; logic_wrapper `Py_IsInitialized()==false` path runs drains without the GIL; `shutdown_loop` in example 12 has no cancellation timeout / try-finally; README "readers never block writers" slightly overstates `atomic<shared_ptr>`; standalone handles have no validator; derived probe wrong-return-type gives a cast error.

## Status — 2026-09-03 afternoon, follow-up series 75256da..16d9989 (25 commits, 74/74 at every commit, ASan lane green)

Every item from the previous "Still open" list is closed, plus the examples-audit API work:
standalone handle state behind shared_ptr (self-drop safe) · no Python calls after finalization; clear TypeErrors for bad handlers/validators/derived returns · `plot.replace_series()`/`set_labels()` · real `slider()`/`checkbox()` (C++ delegates plumbed; `.range`) · ASan+UBSan CI lane (proven with deliberate breakage) · observed standalone handles GC-collectable (`keep_alive<0,1>` dropped) · posted mutation closures own their target (found a latent UAF) · `PyModel` GC-aware (`tp_traverse`/`tp_clear`) — examples are plain scripts · `prism.headless()` with `wait_until`/`quit` + final drain · `@prism.on_change`, descriptor deps for `derived`, one observe spelling (`Model.observe` removed) · `worker(repeat=)`, zero-arg workers, atomic `field.add()` · `prism.run(model, headless=...)` · examples 841→669 lines, `__all__` trimmed of 22 handle classes · docs.

Bugs found along the way (all fixed): `SlotDerived` member-destruction-order UAF; `PyModel::drain` holding `slots_mutex` across Python callbacks (self-deadlock with `tp_traverse`); `derived()` rejecting slider/checkbox deps; `field.add()` queuing an `nb::object`; run guards leaking when `model_app()` throws; `headless()` swallowing the runner's exception; `_request_quit` lost-wakeup.

Still open (minor): a user observer strongly capturing its model is a GC-invisible cycle until `run()` returns or atexit (documented in `__init__.py`); `Worker` does not participate in `transaction()`; slider min/max/label fixed at construction, orientation horizontal only; `PlotHandle` del-during-flight has no dedicated repro; `list_field` has no atomic counter analogue to `field.add()`.

## Status — 2026-09-03 evening, residuals series fd2dec8..886a938 (5 commits, 74/74, ASan lane green)

Closed: observer callbacks are GC-visible (hubs hold a weakref; the handle owns the callback — a Model with a self-capturing observer is now collectable, and `conn = h.observe(cb); del h` leaves `conn` inert, documented in `kObserveDoc` and the README) · `slider(orientation=)` + `set_range()` (runtime min/max; `.value =` and `set_range` serialize as read-modify-write closures on the logic thread) · PlotHandle del-in-flight regression test · validator `__repr__`-raises guard · list class-level observe deprecation · `wait_until`/quit docs · worker+transaction idiom and list-op atomicity documented · one guarded Python-ref release helper shared by `PyHolder`, `~SlotDerived`, `~SlotTree`, `~WeakCallback`.

Remaining (minor, untested rather than broken): no `derived()`+vertical-slider test; the `WeakCallback` GIL-guard reproducer is a behavioral guard (no pre-fix crash could be forced on this allocator — the ASan lane is the discriminator).
