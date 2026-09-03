# Python bindings — fully multi-threaded SDK (plan fragment)

Date: 2026-08-31. Scope: the threading/synchronization design only (replaces the
`doc/design/README.md:79` placeholder "nanobind wrapping, GIL-free Python 3.14+, callback
threading"). Rule rejected up front: no "everything on one Python thread" (option a).

## 0. Verified ground truth (what breaks today, with citations)

- `Field::set()` is unsynchronized: read-compare-write of `value` + emit
  (`include/prism/core/field.hpp:21-25`). Two concurrent setters tear/lose writes; a setter
  races every reader of `field.get()`.
- `SenderHub` is a plain `std::vector` with a non-atomic `int emit_depth_`
  (`include/prism/core/connection.hpp:55-76`). Concurrent `emit`/`connect`/`remove` is UB
  (reallocation during iteration, `erase_if` during iteration, corrupted depth counter).
- `~Connection()` disconnects (`connection.hpp:17,31-35`) — under Python GC this can run on
  *any* thread (3.14t finalizers, `gc.collect()` from workers), i.e. `remove()`
  (`connection.hpp:78-83`) races `emit()` today.
- All widget wiring reads `Field` values on the logic thread without locks: `record()`
  closures capture `&field` (`include/prism/ui/widget_node.hpp:151-169`), `handle_input`
  mutates (`benchmarks/stall_latency.cpp:89-92`). Any off-thread writer races the frame
  pipeline directly.
- `connect_dirty` fires `set_dirty(id)` synchronously on whichever thread called `set()`
  (`include/prism/app/widget_tree.hpp:522-534`) — an off-thread emit writes `WidgetNode::dirty`
  while the logic thread walks it (`any_dirty`/`clear_dirty`, `window_registry.hpp:53-57`).
- There is **no idle wake-up**: the logic thread only runs when input arrives
  (`include/prism/app/model_app.hpp:191-253`) or an animation tick reschedules itself. The
  system monitor deliberately keeps a never-removed animation alive just so `Shared<T>` drains
  keep happening with zero input (`examples/model_system_monitor/model_system_monitor.cpp:181-195`).
  A Python thread mutating the model of an idle app would never get a repaint.
- Transactions are `thread_local` (`include/prism/core/transaction.hpp:20-23`) with a hard
  64-wave assert (`transaction.hpp:64-70`) — fine as-is as long as emits keep happening on one
  thread; a constraint the routing below preserves rather than fights.
- Cross-thread primitives already exist and are proven: `Shared<T>` = atomic_cell + pending
  flag (`shared.hpp:17-31`), `Channel<T>` = lock-free `mpsc_queue` (`channel.hpp:17-25`,
  `mpsc_queue.hpp:46-52`). The render/publish side is already thread-safe
  (`doc/design/threading-model.md`).
- `AppContext::scheduler()` (`model_app.hpp:36`) exposes the run_loop scheduler, and posting
  to it from a foreign thread is already done in-repo by the backend thread for every input
  event (`model_app.hpp:194-196`). That is the existing, proven ingress mechanism.

## 1. Synchronization choice: hybrid — hub locks in core + posted mutations; no global lock

**Rejected (b) global binding-boundary lock — fatal hole:** the frame pipeline reads field
values *without ever crossing the binding layer* (`record()` via `field.get()`,
`widget_node.hpp:158-162`). A lock held only by Python-facing calls cannot stop a Python
writer racing `build_snapshot()`. Closing that hole means holding the global lock around the
entire frame build, which serializes every Python producer behind full layout+record passes —
degrading exactly the "frames independent of application logic" contract the
`benchmarks/stall_latency.cpp:164-192` assertions pin down. Also, on 3.14t
`gil_scoped_acquire` is a no-op, so that one lock would be the *only* serialization left,
held across ms-scale work while recursive Python callbacks need it too. Wrong shape.

**Rejected pure (c) per-field locks everywhere:** fixes hub integrity but not the
writer-vs-`record()` race; fixing that requires locking inside every `Widget<T>::record`
read site (every delegate), with per-frame copies under lock. Invasive, and it still fires
`set_dirty` on arbitrary Python threads (dirty-flag race, `widget_tree.hpp:522-534`) and
spreads `thread_local` transaction flushes across threads.

**Chosen: hybrid.**
1. Core gets one narrow upgrade: **thread-safe `SenderHub`** — per-hub mutex guarding
   `receivers_`; `emit()` snapshots the receiver vector under the lock and invokes callbacks
   **outside** the lock; `remove()` erases under the lock. `emit_depth_`/`pending_removes_`
   (`connection.hpp:60-64,78-83`) become unnecessary — snapshot-then-invoke gives the same
   semantics today's deferred removal has (a callback disconnected mid-emit still completes
   the current pass) while making cross-thread connect/disconnect/emit safe by construction.
   This is required regardless of binding design (GC teardown on 3.14t) and benefits pure-C++
   multithreaded users too.
2. **No lock is ever held while calling user code** (Python or C++). Snapshot-then-invoke
   makes multi-lock A→B/B→A deadlocks structurally impossible.
3. Field *values* stay single-writer: off-thread mutations are **posted to the logic thread**
   (§2), so `record()`, `handle_input`, `Derived::recompute` (`derived.hpp:37-42`),
   transactions and dirty tracking all keep running exactly as today. Zero read-path changes.

**Perf justification:** hot path becomes lock + small vector copy + N calls (N is typically
1–3 receivers: one dirty-flag closure + maybe one observer). Uncontended futex-backed mutex
is ~tens of ns; `stall_latency` overhead is dominated by `record()` at 10–500 ms scale
(`stall_latency.cpp:147-163`) — lock cost is noise there, and its exact assertions
(one record call, latency ≥ stall, publish count) are untouched by design since the frame
path takes no new locks. A set-throughput micro-benchmark (sets/s, 1 vs 8 producer threads)
is added in P1 as the regression gate.

**3.14t GIL implication:** because PRISM never relies on the GIL for C++ state safety
(callbacks fire serialized by the logic thread, not by the GIL), the SDK behaves identically
on GIL builds and free-threaded builds. `gil_scoped_acquire` at callback entry is still
required for correctness of the Python runtime on GIL builds and is a cheap no-op on 3.14t.

## 2. How any-thread `field.set()` reaches the logic thread

**Chosen: coalescing mutation queue + posted run_loop task** (not immediate emit under lock,
not one-task-per-set):

- One per-app `mpsc_queue<std::function<void()>>` (reuse the existing lock-free queue —
  `core/mpsc_queue.hpp`, already battle-tested by `Channel`) + one `std::atomic<bool>
  scheduled_` coalescing flag (the exact `Shared::pending_` pattern, `shared.hpp:22-25`).
  First enqueued mutation in a batch posts one task via
  `exec::start_detached(schedule(sched) | then(...))` — same mechanism `model_app` already
  uses from the backend thread (`model_app.hpp:194-196`); `run_loop` wakes on pushed work.
- The posted task, on the logic thread: pop **all** queued closures and run them in FIFO
  order (each is a real `field.set(v)` → synchronous emit → `connect_dirty` → `set_dirty`,
  all on the logic thread — dirty tracking untouched), then run the same tail the input path
  already runs: `drain_shared()` → `for_each_dirty(publish)` → `schedule_tick()`
  (`model_app.hpp:244-250`). Factor that tail out of `model_app` into a shared callable used
  by both paths.
- **Ordering:** per-producer FIFO is preserved by the mpsc queue; snapshot publish happens
  strictly after all mutations of the batch, in the same task — no publish can interleave
  mid-batch. `TransactionGuard` stays usable: Python `with prism.transaction():` enqueues a
  *single* closure that performs all its sets under one guard on the logic thread → one
  coalesced flush, matching today's C++ semantics (thread_local state,
  `transaction.hpp:20-23`, never crosses threads).
- **Read-your-writes:** `set()` returns before the value lands (sub-ms, next loop turn).
  The binding keeps a per-field Python-side last-set cache so `m.count.value` right after
  assignment returns the assigned value; the *rendered* value converges next frame. Document:
  "Python sees latest-set; the screen catches up next publish." Users who need latest-wins
  cross-thread state with `.get()` visibility use `Shared<T>` (already built for it);
  users who need every event use `Channel<T>`. This keeps the existing Shared/Channel/Field
  taxonomy (`AGENTS.md` conventions) intact instead of blurring Field into a fourth thing.
- **Why not immediate emit under lock:** writes the value under a per-field lock but then
  `record()` (lockless) still races the write unless every delegate read site locks too; and
  it fires `set_dirty` on the Python thread (dirty-flag race). Posting costs one extra
  wake and buys a single-writer invariant for the whole view layer.
- **New capability this unlocks (test it):** idle app + off-thread set ⇒ publish happens
  with zero input events. Today that is literally impossible (no wake path; see
  `model_system_monitor.cpp:181-195` workaround).

## 3. observe/on_change callback safety

- **Firing thread:** all PRISM-originated callbacks (Field emit, Shared drain, Channel drain)
  fire on the logic thread — Field by construction of §2, Shared/Channel because their
  `drain_notifications()` runs in `drain_callbacks_` on the logic thread
  (`widget_tree.hpp:66-69,456-463`; drains collected at `widget_tree.hpp:700,713`).
  One rule to document: *your callback runs on PRISM's logic thread; you may `set()` fields
  synchronously inside it; don't block it.*
- **GIL:** every trampoline wraps the Python call in `nb::gil_scoped_acquire`. On GIL builds
  this is required; on 3.14t it degrades to thread-state ensure (no-op serialization) — safe
  either way because single-threaded emission already serializes callbacks. Never use the GIL
  as a C++-state lock.
- **Exceptions:** catch `nb::python_error` at the trampoline boundary, print traceback to
  stderr (P3: also mirror into a debug Channel for the inspector). A Python exception must
  never unwind through `SenderHub::emit`/run_loop — that path ends in `std::terminate`.
- **Connection teardown:** the Python `Connection` wrapper holds the existing
  `shared_ptr<function>` detach (`connection.hpp:14-15,38`); `__del__` calls `disconnect()`
  directly — safe from any GC thread once the hub is lock-guarded (§1), and deliberately
  GIL-free (pure C++ detach body, no Python objects touched at finalization — avoids
  shutdown-time GIL deadlocks). Semantics note to document: a callback may fire once more
  in an emit pass that snapshotted before the disconnect (identical to today's deferred
  removal behavior, `connection.hpp:79-80`).
- **Reentrant set:** callbacks setting fields (same or other) run on the logic thread →
  synchronous emit → nested snapshot-emit on the target hub. Recursion/cascade behavior is
  exactly today's C++ behavior, including the 64-wave transaction cycle guard
  (`transaction.hpp:64-70`). No new machinery.

## 4. Shared/Channel fit — already cross-thread; they only need the wake

- `Shared.set()` from any Python thread: direct call, value path unchanged
  (`shared.hpp:22-25`). `Shared.get()` anywhere: safe (`atomic_cell` load).
- `Channel.send()` from any Python thread: **direct enqueue, no post** — `mpsc_queue::push`
  is already any-thread (`channel.hpp:17-18`, `mpsc_queue.hpp:46-52`); delivery stays
  lossless/ordered through the logic-thread drain.
- The one real gap is the idle wake: today a `Shared.set()` on an idle app is invisible
  until input/animation happens (the system monitor masks this with a permanent animation,
  `model_system_monitor.cpp:189-195`). Fix: binding `Shared.set()`/`Channel.send()` append a
  bare "drain + publish" closure to the same §2 queue (value itself is already stored
  natively). One mechanism, one wake per batch, covers Fields, Shared and Channel uniformly.
- `observe()` on Shared/Channel: connects under the §1 hub lock; callbacks fire during the
  logic-thread drain → same §3 rules.

## 5. Python model-definition API shape

Constraints from the code: Python classes can't use the P2996 reflection branch, so the
binding must use the `view()` path (`widget_tree.hpp:647-684`); `node_leaf` captures `&field`
into long-lived record/wire/on_change closures (`widget_node.hpp:151-187`), so **field
addresses must be stable and outlive the WidgetTree** (`window_registry.hpp:29` builds the
tree from `Model&`).

Design:
- C++ trampoline type `PyModel`: owns a `std::deque<...>`/individually heap-allocated slots
  for the fields (addresses stable, never reallocated), exposes `view(ViewBuilder&)` that
  re-enters Python if the class defines `view`, else auto-stacks all fields in declaration
  order (mirroring the reflection branch's all-members walk, `widget_tree.hpp:686-719`).
  `view()` runs during `WidgetTree` construction on the logic thread — GIL acquired, once,
  at startup.
- Python side, descriptors allocate slots in `__init__` and return handles:

  ```python
  class Mixer(prism.Model):
      volume = prism.slider(0.75, min=0.0, max=1.0)   # Field<Slider<>>
      mute   = prism.checkbox(False, label="Mute")    # Field<Checkbox>
      count  = prism.field(42)                        # Field<int>

      def view(self, vb):
          vb.hstack(self.volume, self.mute)           # optional; else auto-vstack
          vb.widget(self.count)

  m = Mixer()
  prism.run(m, title="Mixer")          # blocks calling (main) thread, like model_app
  # any other thread, any time:
  m.count.value = 43                   # posts via §2 queue (descriptor __set__ sugar)
  conn = m.count.observe(lambda v: ...)  # fires on logic thread; keep conn alive
  ```
- `m.count` returns a `FieldHandle` (typed wrappers: `FieldInt/Float/Bool/Str` + sentinel
  types Slider/Checkbox/Button — one nanobind class per concrete `Field<T>`); `.value`
  get/set, `.observe()`, `.on_change` pipe. Descriptor `__set__` routes `m.count = 5` to the
  slot so attribute rebinding can't orphan the C++ address. Handles hold the `PyModel`
  alive (`nb::keep_alive` / shared_ptr) — GC of the model while a tree lives is impossible.
- Multi-thread visibility of the model object itself: slots are written once at
  construction; all later traffic goes through the thread-safe paths above. No Python-side
  locking needed for the supported operations.
- `prism.run()` must release the GIL around `backend.run()` (on GIL builds) so worker
  threads keep running while the main thread pumps SDL; on macOS the call must come from the
  main thread (AppKit constraint already documented at `model_app.hpp:138-142`).

## 6. Phases, P1 scope, and the tests that prove it

**P0 — thread-safe SenderHub (core, C++ only).** Snapshot-emit + locked remove; drop
`emit_depth_`/`pending_removes_`. Tests: extend `tests/test_connection.cpp` with an
N-thread emit/connect/disconnect hammer (TSan-clean); all existing semantics tests must stay
green (`test_field`, `test_derived`, `test_transaction`, `test_transaction_cycle_guard` —
transaction flush behavior is the semantic-risk area). *P1, not later, because everything
above depends on it and it is small, isolated, and doctest-provable without Python.*

**P1 — logic-thread ingress (C++ only).** Mutation/wake queue + factored drain/publish tail
in `model_app`; `AppContext::post()` surface. Tests (headless, `TestBackend`/
`CapturingBackend` per `doc/review-2026-08-28.md`): (a) N threads setting fields
concurrently → final snapshot value = one of the legal last writes, no crash, TSan-clean;
(b) **idle-wake test**: no input, off-thread set ⇒ publish happens; (c) per-producer FIFO
order through the queue; (d) `stall_latency` still passes unmodified; (e) new set-throughput
micro-benchmark, 1 vs 8 producers. *P1 because it is the whole thesis of this plan and is
testable without any Python.*

**P2 — Python MVP.** nanobind as a meson subproject (wrap); `PyModel` + descriptors +
handles for scalar/string + Slider/Checkbox/Button; auto-stack view; `prism.run()` with
GIL release; set/get/observe from any thread via P1; GIL-acquire + exception-catch
trampolines. Tests: pytest suite — multi-thread set storm against a running headless app,
read-your-writes cache check, observer values, Connection GC from worker threads, clean
interpreter shutdown with live app. **Both interpreters in CI: GIL build (3.13/3.14) and
free-threaded (3.14t)** — the 3.14t run is the one that would have caught every race the
challenger flagged; the GIL build run proves the `gil_scoped_acquire` path. Verify nanobind
free-threaded support claims against current nanobind docs at this step.

**P3 — completeness.** Shared/Channel Python wrappers (+ wake post), custom Python `view()`,
`prism.transaction()` context manager (§2 batch closure), `Derived` with Python compute
closure, `List<T>`/table sources, app quit/lifecycle from Python, `doc/design/python-bindings.md`.

**Deferred:** GPU backend interplay, widget catalog beyond sentinels, asyncio integration,
free-threaded perf tuning beyond correctness.

## Open questions (flag, don't block)

- Snapshot-emit changes one edge semantic: receivers *connected during an emit* no longer
  fire in that same pass (today's index loop can see them, `connection.hpp:57`). Audit tests
  in P0; if any rely on it, keep a same-thread fast path.
- nanobind free-threaded maturity on 3.15t (the local interpreter is 3.15.0rc1) — pin the
  CI matrix to whichever free-threaded minor nanobind supports cleanly.
- Python exception sink beyond stderr (debug Channel) — P3 nicety, not a correctness item.
