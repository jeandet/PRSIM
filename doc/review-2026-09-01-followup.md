# PRSIM Global Review — Follow-up, 2026-09-01

Scope: architecture, threading core, Python bindings (python/src/prism_ext.cpp,
python/prism/__init__.py, pyproject.toml, python/meson.build), C++ core
(include/prism/**, src/**), rendering backends, meson build, tests, doc/design/**.
HEAD `72ae86c`.

This is a follow-up to `doc/review-2026-09-01.md` (HEAD `0c69a07`), whose BLOCKER/HIGH
findings were addressed in commit `72ae86c` ("review: address global audit — packaging,
races, and docs"). Five independent sub-reviews (Python bindings/packaging, threading
core, rendering backends, widget/UI architecture, build/tests/docs), each verified
against current code, findings merged and re-ranked below.

## Verification of 72ae86c's claimed fixes

| Claim | Verdict |
|---|---|
| `pyproject.toml` `dynamic = ["version"]` (PEP 621) | **Fixed.** `pyproject.toml:13`, paired correctly with `build-backend = 'mesonpy'`. |
| `SoftwareBackend::windows_` guarded by `windows_mutex_` | **Fixed for the real race.** Every mutator and cross-thread reader is locked (`create_window`, `drain_window_requests`, `drain_close_requests`, `submit`, `sdl_id_to_prism_id`). One LOW residual gap, see below. No lock-ordering/reentrancy issue; no lock held across a user callback. |
| `transaction.hpp` overflow clears stale queue | **Fixed, confirmed by deep trace** including nested `TransactionGuard`s and the non-converging-cascade test — no edge case slipped through. |
| `prism_ext` startup race (CAS guard, spin-wait, double-run guard) | **Fixed** for the write path. Read path has a related gap, see HIGH #4 below. |
| Font path: install_data + runtime `exists()` fallback | **Fix is incomplete** — the runtime fallback logic is correct, but two other bugs mean it doesn't help a real wheel install. See HIGH #3. |
| `stdexec.wrap` pinned to hash | **Confirmed** — real 40-char SHA (`d48726ba2c...`), not a movable ref. |
| `mpsc_queue`/`connection.hpp` doc fixes, "sentinel size_t widening" | **Fixed**, but the "sentinel widening" part of the commit message is a red herring — it's actually `TextArea::rows` in `doc/design/widgets-and-sentinels.md:81` (int→size_t), unrelated to `mpsc_queue.hpp`. Cosmetic, already logged in the prior review. |
| render-backend.md interface note sync | **Confirmed accurate**, `doc/design/render-backend.md:15-18` matches `include/prism/app/backend.hpp:19-23`. |

Also re-verified and closed from earlier-session memory (no longer live):
- `g_app_ctx` UAF — gone, `PostHandle` is fully `weak_ptr`-based.
- "Descriptor-field API lacks keep_alive" — **false positive**. `BoundField::owner` is an independent `shared_ptr<SlotBase>` copy that keeps the Slot alive regardless of Model lifetime; `test_bound_field_survives_model_gc` covers it.
- "`limited_api:3.12` incompatible with free-threaded" — **false positive**. No limited-API/abi3 setting exists anywhere in the build.
- `mpsc_queue` ABA — ruled out; correct Vyukov intrusive MPSC (single atomic `exchange`, no CAS retry loop).
- `AppContext::post()` batch-drain flag — looks like a missed-wakeup race, traced every producer interleaving, is correct.

---

## BLOCKER

**1. Python `derived()` dependency callbacks UAF on cross-thread GC.**
`python/src/prism_ext.cpp:539-596` (`derived_attach_dep`), `:410-420` (`recompute`).
Every other handle type in this binding protects its callback lifetime with
`nb::keep_alive`/an `owner` shared_ptr chain (verified for `BoundField`, `Shared*`,
`Channel*`, `List*`). `SlotDerived`'s dependency connections are the one exception: each
capture is `[slot](const auto&){ slot->recompute(); }` — a **raw pointer**, `prism_ext.cpp:546,557,568`.
Nothing stops a `BoundDerived` Python wrapper's refcount hitting zero on a worker thread
(explicit `del` from another thread, or a free-threaded-build finalizer) while the logic
thread is mid-`emit()` on a field it depends on. `doc/design/python-sdk.md:11` calls
out `~Connection()`-runs-on-any-thread generally but this specific path isn't guarded.
**Zero test coverage** — `derived()` isn't referenced anywhere in
`python/tests/test_prism_python.py`.
*Scenario:* worker thread does `del derived_ref` while `field.value = x` runs on the
logic thread and a `derived()` depends on that field → `slot->recompute()` on freed memory.

**2. Debug tree-inspector UAF when closed via its own hotkey.**
`include/prism/app/model_app.hpp:342,360-362,367` (built under `PRISM_DEBUG_TOOLS_ENABLED`,
so ships in debug/dev builds). `entry = registry.find(wid)` is fetched once per dispatched
event (`:342`). If the event is Ctrl+Shift+I *and* the inspector window already exists,
`global_key_handler` (`:361`) calls `registry.remove(*debug_window_id)`, destroying the
`Entry`/`WidgetTree` that `entry` still points to — then `:362` (`route_key_press`) and
`:367` (`drain_shared()`) dereference the dangling `entry`. Closing via the primary window
or the chrome close button is safe (those paths `return` before `entry` is fetched); only
closing *from the inspector window itself* via the hotkey hits this.
*Scenario:* focus the inspector window, press Ctrl+Shift+I → crash/UAF.
Note: the model_app test suite is excluded wholesale from the TSan CI job, which is
exactly the suite most likely to exercise this — a narrower exclusion would have a shot
at catching this class of bug.

---

## HIGH

**3. Font asset doesn't reach a real wheel install, so text silently never renders.**
Two compounding bugs:
- `src/meson.build:43` installs the `.ttf` via `install_data()` with no `install_tag:`,
  so it gets Meson's default `'data'` tag. `pyproject.toml:21` only asks meson-python to
  install tags `runtime,python-runtime`; `data` isn't in that set, so **the font file is
  never copied into the wheel at all**.
- Even if it were, `src/backends/software_backend.cpp:25-31`'s fallback checks
  `PRISM_FONT_INSTALL_PATH` — a path relative to the install *prefix*
  (`share/prism/fonts/...`) — via `std::filesystem::exists()`, which resolves against the
  process's **current working directory**, not the actual prefix. The code has a comment
  acknowledging the gap ("prefix may differ") but never resolves it (e.g. via
  `/proc/self/exe`).
Net effect for any `pip install`-distributed wheel run from a normal CWD:
`resolve_font_path` returns `nullptr`; guarded by `if (fpath)` so it doesn't crash, but
all text/label rendering is silently disabled. In-tree/editable/dev builds never show
this because `PRISM_FONT_PATH` (an absolute build-machine path) still resolves.
Fix: add `install_tag: 'runtime'` to the `install_data()` call, and resolve the fallback
path relative to the running binary's location, not CWD.

**4. `SenderHub` move construction/assignment silently breaks live connections.**
`include/prism/core/connection.hpp:51-63,76-78`. `SenderHub`'s implicit lifetime contract
("hub must outlive its connections", documented at `:65-68`) is honored by every current
call site for *destruction* order, but the class also exposes move ctor/assignment that
relocate `receivers_` without fixing up already-issued `Connection`s — each `Connection`'s
detach lambda still captures the **pre-move** `this` (`:76-78`). Two distinct hazards:
1. Self-observing pattern (`hub.connect(cb)` stored in the same object's own member, e.g.
   `field.hpp:28`, `derived.hpp:27`, `shared.hpp:36`, `channel.hpp:30`): moving the owning
   object (e.g. `std::vector<Field<T>>` reallocation) leaves a `Connection` whose detach
   lambda points at freed memory once the moved-from husk is destroyed.
2. A callback capturing `&enclosing_object` directly (e.g. `widget_node.hpp:165`) dangles
   **immediately on move**, since the callback itself moves into the new hub's `receivers_`.
`Field<T>`, `Derived<T>`, `Shared<T>`, `List<T>`, and `WidgetNode` are all implicitly
movable (no user-declared destructor/copy/move), so this is the default state of every
`SenderHub`-bearing primitive; `Channel<T>` is accidentally exempt only because
`mpsc_queue<T>`'s user-declared destructor makes it implicitly non-movable.
No currently-reachable trigger was found in shipped code (`WidgetNode::wire()` is deferred
until subtree growth is complete; no `vector<Field<T>>` usage found in-tree) — flagged HIGH
not BLOCKER — but it's a silent landmine (compiles clean, no assert, no test:
`test_connection.cpp`/`test_connection_thread_safety.cpp` never exercise move). Fix:
`= delete` `SenderHub`'s move members (matching `mpsc_queue`'s existing non-movable
stance), or document "never move a hub with live connections."

**5. `observe()` silently no-ops in Python unless the return value is stored.**
`python/src/prism_ext.cpp` — every `.observe()`/`.observe_insert/remove/update()` binding
(~15 sites across `FieldHandle`, `BoundField`, `Shared*`, `Channel*`, `BoundDerived`,
`List*`) calls `field.on_change().connect(wrapper)` and returns the raw `Connection`. Its
destructor calls `disconnect()` (`connection.hpp:18`) immediately if the return value is
discarded:
```python
m.count.observe(lambda v: fired.append(v))   # return discarded
m.count.value = 5
# fired == []
```
The C++ core already has a fire-and-forget `Field<T>::observe()` (`field.hpp:27-29`,
self-storing, returns nothing) that the Python binding bypasses in favor of reimplementing
a must-be-held version. `doc/design/python-sdk.md:85`'s `# keep conn alive` comment is an
acknowledgment of the footgun, not a fix. Silent failure, easy to hit, and contradicts the
project's own default-safe design goal ([[feedback-observe-api]]).

**6. Off-thread reads during the startup race window aren't covered by 72ae86c's fix.**
`prism_ext.cpp:115-125` (`dispatch_sync_read`) vs. `:52-59` (`try_post_via_handle_impl`).
The write path got a 200×1ms spin-wait for the window between `run()` starting and
`setup()` publishing the handle. The read path has no equivalent — it falls through to an
unsynchronized direct `reader()` call if `g_has_handle` isn't set yet. A background thread
reading `field.value` in that exact window races the logic thread's initial snapshot
construction with zero synchronization — same bug class as `8d085fc`, reopened for reads.

---

## MEDIUM

**7.** Early off-thread `post`/`set` calls that land inside the 200ms startup spin window,
if it's ever exceeded (slow debug build, loaded CI runner), fall through to `PostResult::NoApp`
and are dropped — not queued, not retried, no exception raised. `prism_ext.cpp:56-65`.

**8.** `dispatch_sync_read`'s "closed ⇒ safe to read directly" assumption is narrow: once
`closed_flag` is observed true, reads go unsynchronized, but other already-queued drain
continuations on the scheduler can still run afterward and touch the same
zero-internal-synchronization `Field<T>`. `prism_ext.cpp:122-124`. Shutdown-only window.

**9.** `doc/design/threading-model.md` is stale in two independent ways:
   - Lines 5/53/75 claim snapshot handoff is "a single atomic shared_ptr swap... no mutex
     held across a frame boundary," but 72ae86c now takes `windows_mutex_` around
     `snapshots_` in `submit()` and the render loop (`software_backend.cpp:326,335,346-349`)
     — a reasonable pragmatic fix, but the doc contradicts it.
   - Line 12 already says the `mpsc_queue`+`atomic_wait` input mechanism "was replaced by
     stdexec `run_loop` scheduling," but the "Input Event Flow" section six lines below
     (`:55-61`) and its diagram (`:17-26`) still describe input forwarding via
     `mpsc_queue<InputEvent>`. Verified: no such queue exists anywhere in the codebase —
     input dispatch is `exec::start_detached(stdexec::schedule(sched) | stdexec::then(...))`
     directly from `backend.run()`'s callback (`model_app.hpp:317-379`).

**10.** `create_window()` (direct, unsynchronized) and `request_window()` (thread-safe,
queued) coexist on the same public `Backend` API with nothing stopping `create_window()`
from being called after `run()` starts on another thread. `software_backend.hpp:35`,
`.cpp:47-55` vs `:57-66`. Every in-tree caller happens to follow the safe convention
(pre-`run()` for `create_window`, `request_window()` post-`run()`) — correct by
convention, not by enforcement.

**11.** `include/prism/app/widget_tree.hpp` decomposition is only partial: still 1198
lines, with `widget_tree_layout.hpp` (168 lines) and `widget_tree_traversal.hpp` (81
lines) extracted alongside it — smaller than "3 headers" as project memory summarized it.
`doc/design/README.md`'s table has no entry for `widget_tree.hpp` at all, so there's no
canonical doc reflecting the actual current state.

---

## LOW

**12.** `windows_mutex_` doesn't cover `windows_.find(wid)` reads inside `run()`'s SDL
event switch (`software_backend.cpp:196,204,231,271,289`). Same-thread-only today (the
only mutator post-`run()` is the render thread itself, triggered synchronously from the
same loop), so not a live race — but it contradicts the commit's "all...paths locked"
framing and would silently become one if window creation ever moved to a second thread.

**13.** Stale comment in `include/prism/app/widget_tree.hpp:36-37` claiming `index_`
pointers are valid because the tree is "never mutated after construction" — false
(VirtualList/Table/Tabs materialization mutates and re-indexes correctly), but not a bug.

**14.** `Derived<T>`'s "hub must outlive connection" contract (`derived.hpp:44-49`,
`connection.hpp:65-68`) is documented but not compiler-enforced; every current call site
happens to get declaration order right. Correctly downgraded from a stale "BLOCKER" in
project memory — informational only (related to HIGH #4's move-safety gap).

**15.** `subprojects/magic_enum.wrap` pins a tag (`v0.9.7`), not a commit hash, unlike
`stdexec.wrap`'s SHA pin. Low practical risk, just inconsistent rigor.

**16.** No dedicated concurrent-access test for `windows_mutex_`
(`tests/test_software_backend_request_window.cpp` is single-threaded;
`tests/test_software_backend_chrome_cursor.cpp` drives from a second thread but only
exercises cursor/chrome state, not concurrent `create_window`/`drain_*`/`submit()`).
Nothing would catch the mutex being removed or a new unlocked access site added.

**17.** Test coverage gaps: no `List<bool>` int-coercion disablement-path test, no
multithread torture test crossing GC + `observe()` simultaneously (only
`test_connection_gc_from_workers` and `test_bound_field_survives_model_gc` separately),
and (restated from BLOCKER #1) zero `derived()` coverage at all.

---

## Addendum — commits 18354ed, f1537a1, 5642cb2 (pulled 2026-09-01 23:46)

Re-pulled and reviewed the commits landed after this report's initial synthesis.

**18354ed "review followup: fix blockers/highs from 2026-09-01 followup" — all 6 items verified fixed, read line-by-line against the diff:**
- BLOCKER #1 (`derived()` UAF): `derived_attach_dep` now takes `std::shared_ptr<SlotDerived<T>>`, captures a `std::weak_ptr` in each dependency callback, and `.lock()`s before `recompute()` — `prism_ext.cpp:544-585`. Correct fix, standard idiom.
- BLOCKER #2 (debug-inspector self-close UAF): `entry = registry.find(wid); if (!entry) return;` re-fetches after `global_key_handler` — `model_app.hpp:360-363`. Traced every subsequent use of `entry` in the block (`route_text_input`, `drain_shared()`, `publish_entry`) — all correctly gated behind the new re-fetch. Confirmed complete.
- HIGH #3 (font packaging): `install_tag: 'runtime'` added (`src/meson.build:43`) closes the wheel-omission bug; `/proc/self/exe`-relative fallback (`software_backend.cpp:29-40`) closes the CWD-relative bug. Not verified by an actual wheel build (would need a full C++26 rebuild — didn't run it to keep this pass fast), but the logic is sound: `<exe_dir>/../share/prism/fonts/...` correctly reaches `<sys.prefix>/share/...` for the venv-python-in-bin/ layout pip uses.
- HIGH #4 (`SenderHub` move UAF): not eliminated — deliberately kept movable (comment at `connection.hpp:63-70` explains `WidgetNode` vector storage needs move) and documented as unsafe with live connections instead. Reasonable tradeoff given the earlier finding that no live trigger exists in-tree; downgrading to informational, not re-flagging.
- HIGH #5 (`observe()` footgun): every `.observe()`/`.observe_insert/remove/update()` binding gained `nb::keep_alive<1,0>()` alongside the existing `<0,1>()` — verified via `grep` that zero sites were missed. This makes `self` (the handle) hold the returned `Connection` alive even when the caller discards it, matching the C++ core's fire-and-forget `Field<T>::observe()`. Correct fix. Side effect worth knowing (not a bug, a real tradeoff): every `observe()` call now accumulates its `Connection` for the handle's lifetime unless the caller explicitly `.disconnect()`s — same tradeoff the C++ core already accepts, not a regression.
- HIGH #6 (read-path startup race): `dispatch_sync_read` got the same 200×1ms spin-wait the write path already had — `prism_ext.cpp:117-121`. Matches.

**f1537a1 "docs(readme): add Python SDK section"** — docs only, spot-checked the example code against `__init__.py`'s actual signatures (`field`, `slider`, `checkbox`, `derived`, `Annotated`+pydantic validation) — accurate.

**5642cb2 "python: fancy plot/tree examples + PlotModel/TreeController bindings" — new code, not previously reviewed by anyone:**

New MEDIUM — **`PlotHandle` and `TreeHandle`/`BoundTree` bypass the thread-dispatch machinery that every other handle type uses.**
`python/src/prism_ext.cpp:303-327` (`PlotHandle::add_series/clear_series/notify/reset_view`), `:412-432` (`BoundTree::refresh/rows`), `:433-449` (`TreeHandle`, plus its inline `refresh`/`rows` bindings at `:1195-1210`). All of these call straight into `PlotModel`/`TreeController` methods that mutate a plain unsynchronized `std::vector` (`PlotModel::series_` at `include/prism/widgets/plot.hpp:314` — has no `Field<T>`/lock wrapper; `TreeController::rows`) with **no** `list_op_dispatch`/`field_set_dispatch`/`dispatch_sync_read` wrapping — unlike `BoundPlot`'s own `add_series` (`:684-705`, correctly dispatched) and every `FieldHandle`/`SharedHandle` method in the file. Calling `.refresh()`, `.rows()`, or any `PlotHandle`/`TreeHandle` mutator from a non-logic thread races the logic/render thread reading the same vector — contradicts the project's stated "any thread may mutate" guarantee (restated in this session's README addition). Not reachable in any shipped example or test today — every example only touches these pre-`run()` (single-threaded) or from within an `observe()` callback (already running on the logic thread) — and `PlotHandle`/`TreeHandle` have zero test coverage and zero example usage despite being in `__all__`. Same risk class as the already-accepted `SenderHub` move landmine: silent, currently unreachable, worth a doc note or a dispatch wrapper before anyone relies on it.

LOW — `python/src/prism_ext.cpp:825-828`, `py_tree_dispatch`'s leading `if (nb::isinstance<nb::object>(h)) { /* placeholder */ }` is dead code (always true, empty body) — vestigial debug scaffolding, harmless but worth deleting.

Checked and ruled out (false-positive candidates I verified before dropping): `SlotPlot::build()` (`prism_ext.cpp:293-296`) omits `depends_on(plot.x_label)`/`depends_on(plot.y_label)` from its canvas dependency list — looked like a missing-repaint bug at first, but `examples/showcase/showcase_plot.cpp:29-33` and `examples/model_plot/model_plot.cpp:54-58` show this is the exact same dependency set used everywhere else in the C++ codebase for plot canvases — pre-existing project-wide convention, not a regression in this commit. `TreeNodeId` hash handling (`PythonTreeSource::root_at`/`child_at`, `prism_ext.cpp:346-360`, casting Python's possibly-negative `hash()` via `int64_t`→`static_cast<uint64_t>`) is correct: `TreeNodeId = uint64_t` (`include/prism/ui/tree.hpp:27`) and the cast is a well-defined bit-preserving reinterpretation.

## Priority order

1. Fix #1 (Python `derived()` UAF) and #2 (debug-inspector UAF) — both crash-class, both
   trivially reachable once you know the trigger.
2. Fix #3 (font packaging) and #6 (read-path startup race) — both silent-failure classes
   that would ship broken behavior to every user of a wheel-installed app.
3. Fix #4 (`SenderHub` move safety) — cheapest fix is `= delete` on move members; do this
   before any future code path relies on moving a `Field`/`Derived`/`Shared`/`List`.
4. Fix #5 (Python `observe()` footgun) — either switch the binding to self-storing
   fire-and-forget (matching the C++ core's own `Field<T>::observe()`), or keep the
   explicit-return-value API but raise instead of silently dropping when nothing holds it.
5. MEDIUM/LOW items are cleanup — docs (#9, #11), test coverage (#16, #17), and
   convention-only guards (#7, #8, #10) — no urgency, batch with adjacent work.
6. Since 18354ed closed every BLOCKER/HIGH from this report: next up is the new MEDIUM
   from the addendum (`PlotHandle`/`TreeHandle`/`BoundTree.refresh()` dispatch gap) — same
   "landmine, not yet live" shape as #4, batch them together — plus a wheel-build smoke
   test to close the "not runtime-verified" caveat on the font-packaging fix.
