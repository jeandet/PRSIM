# Python SDK — Design Spec

**Status:** Design (replaces `doc/design/README.md` python-bindings placeholder).  
**Date:** 2026-08-31.  
**Decisions here are fully multi-threaded from Python** — any Python thread may mutate the model. The single-thread rule (option a) is rejected.

## 0. Ground truth — what breaks today

- `Field::set()` is unsynchronized: read-compare-write + emit (`core/field.hpp:21-25`). Two concurrent setters tear; setter races readers.
- `SenderHub` is plain `vector` + `emit_depth_` (`core/connection.hpp:55-76`). Concurrent `emit`/`connect`/`remove` is UB (realloc during iteration, corrupted depth).
- `~Connection()` disconnects (`connection.hpp:17,31-35`) — under Python GC can run on any thread (3.14t finalizers), `remove()` (`connection.hpp:78-83`) racing `emit()`.
- Widget wiring reads `Field` on logic thread without locks: `record()` captures `&field` (`ui/widget_node.hpp:151-169`), `handle_input` mutates (`benchmarks/stall_latency.cpp:89-92`). Off-thread writer races frame pipeline.
- `connect_dirty` fires `set_dirty(id)` on caller's thread (`app/widget_tree.hpp:522-534`) — off-thread emit dirties `WidgetNode::dirty` while logic thread walks it (`window_registry.hpp:53-57` / `widget_tree.hpp:64`).
- No idle wake: logic thread only runs on input (`app/model_app.hpp:191-253`) or animation tick. `model_system_monitor` keeps a permanent animation alive to drain `Shared` (`model_system_monitor.cpp:181-193`). Python mutation of idle app would never repaint.
- Transactions are `thread_local` (`core/transaction.hpp:20-23`, 64-wave assert `64-70`) — must stay on one thread.
- Cross-thread primitives already proven: `Shared<T>` = `atomic_cell` + pending flag (`core/shared.hpp:17-31`), `Channel<T>` = `mpsc_queue` (`core/channel.hpp:17-25`, `core/mpsc_queue.hpp:46-52`). Render/publish is thread-safe (`threading-model.md`).
- `AppContext::scheduler()` (`app/model_app.hpp:36`) + posting from foreign thread is proven (`model_app.hpp:194-196`).

## 1. Synchronization — hybrid: hub locks + posted mutations

Rejected **global binding-boundary lock**: frame reads `Field` without crossing binding (`record()` via whole `Field<T>&` `ui/widget_node.hpp:151`). Lock only on Python calls can't stop `record()` race unless held around entire `build_snapshot()` — would serialize producers behind layout+record (up to 500 ms in `stall_latency.cpp:147-163`) and on 3.14t `gil_scoped_acquire` is a no-op so it's the only serialization, held across callbacks. Also rejected **pure per-field locks**: still fires `set_dirty` off-thread and needs every delegate read site locked.

**Chosen:**

1. **Thread-safe `SenderHub`** — per-hub mutex guarding `receivers_`; `emit()` snapshots vector under lock, invokes outside lock; `remove()` erases under lock. `emit_depth_`/`pending_removes_` (`connection.hpp:60-64,78-83`) become dead code. Required for GC teardown on 3.14t; benefits C++ too.
2. **No lock held across user code** — snapshot-then-invoke makes A→B/B→A deadlock impossible.
3. **Field values single-writer**: off-thread mutations are posted to logic thread (§2); `record()`, `handle_input`, `Derived::recompute` (`core/derived.hpp:37-42`), transactions, dirty tracking unchanged.

Perf: lock+copy for 1–3 receivers ≈ tens of ns; `stall_latency` dominated by `record()`. `stall_latency` publish/latency/record-count assertions stay green (no off-thread producers in that binary). Add throughput bench (sets/s, 1 vs 8 producers) as gate.

**Free-threaded GIL**: PRISM never uses GIL for C++ state safety. Callbacks serialize on logic thread. `gil_scoped_acquire` at entry is required on GIL builds, no-op on 3.14t — identical behavior.

**Semantic delta to audit in P0:** receivers connected *during* an emit no longer fire same pass (today index loop can see them `connection.hpp:57`); receivers disconnected mid-emit still complete current pass (today deferred removal) — snapshot gives same. Also nested-emit disconnect: snapshot drops immediately vs today defers until outer `emit_depth_==0` (`connection.hpp:60-64,78-83`). `tests/test_connection.cpp:98-109` covers single-level; extend for nested.

## 2. Any-thread `field.set()` → logic thread

**Coalescing mutation queue + posted `run_loop` task** (not immediate-under-lock, not one-task-per-set):

- Per-app `mpsc_queue<std::function<void()>>` + `atomic<bool> scheduled_` (reuse `Shared::pending_` pattern `core/shared.hpp:22-25`). First enqueue posts one `exec::start_detached(schedule(sched) | then(...))` — same as backend input path (`model_app.hpp:194-196`); `run_loop` wakes on work.
- **Dispatch check:** binding checks thread-id before choosing path. If already on logic thread (callbacks, `post` tail), call `field.set(v)` directly — synchronous nested emit per §3. Otherwise enqueue + wake. One sentence, enforced in P2 binding layer.
- Posted task on logic thread: pop all closures FIFO, run each `field.set` (→ emit → `connect_dirty` → `set_dirty` on logic thread), then tail `drain_shared()` → `for_each_dirty(publish)` → `schedule_tick()`. Factor tail from `model_app.hpp:128-130` (tick path, all-windows drain) and `244-250` (input path, single-window — don't copy `244-250` verbatim or secondary windows starve; M3 fix) into shared callable.
- Ordering: per-producer FIFO via mpsc; publish after whole batch, no interleaving mid-batch. `with prism.transaction():` enqueues single closure doing all sets under one `TransactionGuard` on logic thread (`transaction.hpp:20-23` stays thread_local).
- **Read-your-writes & read staleness:** `set()` returns before value lands (next loop turn). Binding keeps per-field Python last-set cache for immediate `m.count.value` after assignment. **UI-mutated fields go stale**: after a slider drag (logic-thread write), worker-thread `.value` get still sees cached last Python set until next cache update. Document: "Python sees latest-set you made; UI-driven changes converge next publish; for latest-wins cross-thread reads use `Shared<T>`." `Field::get()` from worker thread races writer — binding `.value` getter checks thread-id: on logic thread reads `field.get()` directly; off logic thread returns cache (or optionally posts a blocking fetch — not in v1).
- **Shutdown protocol (deep B1 fix):** `run_loop` is a stack local (`model_app.hpp:61`) and the mutation queue is a heap `shared_ptr<mpsc_queue>` held locally (so `weak_ptr` is possible); posting after `loop.finish()` (`model_app.hpp:200`) / destroy is UAF (`__run_loop.hpp:43-52,144-148` assert). Binding ingress holds `weak_ptr` to that queue plus a closed flag; `post()` after close is rejected (no-op or `RuntimeError`); `prism.run()` quiesces queue before destroying `run_loop`. Also guard interpreter-exit-before-app: trampoline checks `Py_IsInitialized()` before `gil_scoped_acquire`.
- **Pre-`run()` sets:** handles exist from construction, queue only after `model_app` starts. `m.count.value = x` before `prism.run()` does direct `set()` (single-threaded startup, documented).
- New capability: idle app + off-thread set ⇒ publish with zero input (today impossible; see `model_system_monitor` workaround).

## 3. Callbacks, GIL, lifecycle

- **Firing thread:** all PRISM callbacks (Field emit, Shared/Channel drain via `drain_callbacks_` `app/widget_tree.hpp:66-69,456-463` collected at `700,713`) fire on logic thread. Document: "callback runs on logic thread; you may `set()` synchronously inside it; don't block it."
- **GIL:** every trampoline `gil_scoped_acquire`. Never use GIL as C++ state lock.
- **Exceptions:** catch `nb::python_error` at boundary, print traceback to stderr (P3: mirror into debug Channel). Never unwind through `emit`/`run_loop` (terminates).
- **Connection teardown:** `Connection` detach captures raw `this` (`connection.hpp:49-51`) — lock fixes races, not lifetime. Python `Connection` wrapper must keep `PyModel` owner alive (`nb::keep_alive` / `shared_ptr`), same as `FieldHandle` (§5). `__del__` is GIL-free pure C++ detach; GC from any thread is safe once hub is locked.
- **Convenience `observe()`:** `ObservableValue::observe` / `Shared::observe` / `Channel::observe` mutate plain `observers_` vector (`field.hpp:27`, `shared.hpp:35`, `channel.hpp:29`) not covered by hub lock. Binding must call `on_change().connect()` / `on_receive().connect()` (hub-locked after P0) and own `Connection` Python-side; never expose convenience path directly.
- **Reentrant set:** logic-thread emit → nested snapshot-emit; recursion follows C++ including 64-wave guard (`transaction.hpp:64-70`).
- **Descriptor `__set__` branching:** on logic thread → direct `set()` + cache update; off-thread → enqueue + cache update. One place, both paths update cache.

## 4. `Shared` / `Channel`

- `Shared.set()` any thread: direct, value path unchanged (`shared.hpp:22-25`); `get()` anywhere safe (`atomic_cell` load).
- `Channel.send()` any thread: direct `mpsc_queue::push` (`channel.hpp:17`, `mpsc_queue.hpp:46`), lossless/ordered via logic-thread drain.
- Only gap is idle wake: append bare drain+publish closure to same §2 queue (value already stored). Covers Field/Shared/Channel uniformly.
- `observe()` on them: connects under hub lock; fires in drain.

## 5. Python model API

Constraints: Python can't use P2996 reflection → must use `view()` path (`widget_tree.hpp:647-684`); `node_leaf` captures `&field` long-lived (`ui/widget_node.hpp:151-187`), so addresses must be stable and outlive `WidgetTree` (`window_registry.hpp:29`).

- C++ trampoline `PyModel` owns fields in **per-type stable storage**: one `std::deque` per concrete `Field<T>` (or individually heap-allocated slots + type-erased registry) — `deque::push_back` never invalidates references, single `deque<variant>` can't hold heterogeneous `Field<int>` vs `Field<Slider>` without erasure. Exposes `view(ViewBuilder&)` that re-enters Python if `view` defined, else auto-stacks fields in declaration order (mirrors reflection walk `widget_tree.hpp:686-719`). Runs once at `WidgetTree` construction (on calling thread before `logic_thread` starts `model_app.hpp:77,143`; GIL held on main thread so safe).
- Python descriptors allocate slots in `__init__` and return handles:

```python
class Mixer(prism.Model):
    volume = prism.slider(0.75, min=0.0, max=1.0)
    mute   = prism.checkbox(False, label="Mute")
    count  = prism.field(42)

    def view(self, vb):
        vb.hstack(self.volume, self.mute)
        vb.widget(self.count)

m = Mixer()
prism.run(m, title="Mixer")          # blocks main thread, releases GIL around SDL pump
m.count.value = 43                   # any thread, via §2
conn = m.count.observe(lambda v: ...)  # fires on logic thread; keep conn alive
```

- `m.count` → typed `FieldHandle` (`FieldInt/Float/Bool/Str` + sentinels `Slider/Checkbox/Button` — one nanobind class per `Field<T>`); `.value` get/set, `.observe()`, `.on_change` pipe. `__set__` (`m.count = 5`) routes to slot so rebinding can't orphan address. Handles keep `PyModel` alive (`keep_alive`/`shared_ptr`) — GC while tree lives impossible.
- `view()` runs GIL-acquired once at startup. `prism.run()` releases GIL around `backend.run()` on GIL builds so workers progress; must be main thread on macOS (`model_app.hpp:138-142`).

## 6. Phases and tests

**P0 — thread-safe `SenderHub` (C++ only).** Snapshot-emit + locked remove; drop `emit_depth_`/`pending_removes_`. Add `-Db_sanitize=thread` build option / CI job — "TSan-clean" is unenforceable without it. Tests: `tests/test_connection.cpp` N-thread hammer + nested-emit audit + all semantics tests green (`test_field`, `test_derived`, `test_transaction*`).

**P1 — logic-thread ingress (C++ only).** Mutation/wake queue + factored tail + closed-flag shutdown protocol + thread-id dispatch check. Tests (headless `TestBackend`/`CapturingBackend`): (a) N-thread concurrent sets, TSan-clean, final snapshot = legal last write; (b) idle-wake (no input, off-thread set ⇒ publish); (c) per-producer FIFO; (d) `stall_latency` unmodified; (e) throughput bench 1 vs 8 producers; (f) unbounded-queue growth under GIL contention — also add bounded/drop-oldest policy note from `channel.hpp:34-36`.

**P2 — Python MVP.** nanobind wrap, `PyModel` + per-type stable slots + descriptors/handles for scalar/string + `Slider/Checkbox/Button`, auto-stack view, `prism.run` GIL release, §2/§3 trampolines. Tests: pytest multi-thread storm on headless app, read-your-writes cache, stale-read doc check, observer values, `Connection` GC from workers, clean shutdown with live app + post-after-close rejection. CI both GIL (3.13/3.14) and free-threaded (3.14t) — verify nanobind free-threaded support per docs (local is 3.15.0rc1).

**P3 — completeness.** `Shared`/`Channel` wrappers (+ wake), custom Python `view()`, `prism.transaction()` (`with` block buffers per-Python-thread batch → single closure; parallel to C++ `thread_local`), `Derived` with Python compute, `List<T>`/table sources, quit/lifecycle, `doc/design/python-sdk.md` polish, exception sink beyond stderr.

Deferred: GPU backend interplay, full widget catalog, asyncio, free-threaded perf tuning beyond correctness.

## Open questions

- nanobind free-threaded maturity on 3.15t — pin CI to minor nanobind cleanly supports.
- Python exception sink beyond stderr (debug Channel) — P3 nicety.
- Pre-`run()` direct-set vs queued — documented as direct for startup single-thread.

## Conventions

- After any-thread mutation, Python `.value` shows latest Python set; rendered value converges next frame. Use `Shared<T>` for latest-wins cross-thread reads, `Channel<T>` for every-event delivery — per `AGENTS.md` taxonomy.
- `Node` addresses stable via per-type deques; `view()` mirrors reflection auto-stack.
- Core stays SDL-free; SDL types never cross binding boundary.
