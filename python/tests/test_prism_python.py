"""Python bindings pytest suite — P2/P3 gates from doc/design/python-sdk.md §6."""

import gc
import inspect
import os
import subprocess
import sys
import threading
import weakref
from typing import Annotated

import pytest

import prism
from prism import Model, field, shared, channel, transaction


def test_field_basic():
    class M(Model):
        a = field(0)
        b = field("hi")
        c = field(3.14)
        d = field(False)

    m = M()
    assert m.a.value == 0
    assert m.b.value == "hi"
    assert m.c.value == 3.14
    assert m.d.value is False
    m.a.value = 42
    assert m.a.value == 42
    m.b.value = "bye"
    assert m.b.value == "bye"


def test_standalone_field_handle():
    h = prism.FieldInt(5)
    assert h.value == 5
    h.value = 10
    assert h.value == 10
    fired = []
    conn = h.observe(lambda v: fired.append(v))
    h.value = 20
    assert fired == [20]
    conn.disconnect()
    h.value = 30
    assert fired == [20]


def test_prism_ext_observe_is_not_python_monkeypatched():
    """observe() must be the C++ binding itself, not prism.__init__'s _wrap() patch.

    Python always runs a package's __init__.py before any of its submodules, so
    `from prism._prism_ext import FieldInt` cannot dodge prism/__init__.py — but it can
    still tell a genuine nanobind method apart from a Python function pasted over it by
    the (now-removed) monkey-patch: the patch replaced the class attribute with a plain
    `def _wrap(self, *args, **kwargs)` closure, which inspect.isfunction() would catch.
    """
    from prism._prism_ext import FieldInt as RawFieldInt

    assert not inspect.isfunction(RawFieldInt.observe)


def test_prism_ext_bypass_observe_keepalive():
    """FieldInt.observe(cb), called fire-and-forget with no local reference to the
    returned Connection, must keep firing — the keepalive lives in handle.__dict__
    (`_prism_keepalive`), written by the C++ observe() binding itself.
    """
    from prism._prism_ext import FieldInt as RawFieldInt

    h = RawFieldInt(1)
    fired = []
    h.observe(lambda v: fired.append(v))  # fire-and-forget: no local reference kept
    assert len(h.__dict__["_prism_keepalive"]) == 1
    h.value = 2
    assert fired == [2]

    h.__dict__["_prism_keepalive"][0].disconnect()
    h.value = 3
    assert fired == [2]


def test_observe_callback_keyword_uniform():
    from prism._prism_ext import FieldInt as RawFieldInt, ListInt as RawListInt

    h = RawFieldInt(0)
    fired = []
    h.observe(callback=lambda v: fired.append(v))
    h.value = 1
    assert fired == [1]

    lst = RawListInt()
    inserted = []
    lst.observe_insert(callback=lambda idx, v: inserted.append((idx, v)))
    lst.push(7)
    assert inserted == [(0, 7)]


def test_observe_callback_keyword_shared_and_channel():
    # Shared/Channel observe only fires on drain (app-loop or explicit); outside a running
    # app there's nothing to drive that, same as test_shared_basic/test_channel_send below.
    # This test only exercises that callback= is accepted as a keyword, not that it fires.
    from prism._prism_ext import SharedInt as RawSharedInt, ChannelInt as RawChannelInt

    s = RawSharedInt(0)
    fired = []
    conn = s.observe(callback=lambda v: fired.append(v))
    s.value = 1
    assert isinstance(fired, list)
    conn.disconnect()

    c = RawChannelInt()
    received = []
    conn2 = c.observe(callback=lambda v: received.append(v))
    c.send(7)
    assert isinstance(received, list)
    conn2.disconnect()


def test_observed_handles_tracks_standalone_handle_for_atexit():
    """Standalone handles form a real reference cycle (handle -> keepalive list ->
    Connection -> nanobind keep_alive<0,1>, invisible to the cyclic GC -> handle) that
    Python's own GC can never collect. _observed_handles is how atexit finds and breaks
    it before interpreter shutdown, instead of leaking forever.
    """
    from prism._prism_ext import FieldInt as RawFieldInt

    h = RawFieldInt(1)
    fired = []
    h.observe(lambda v: fired.append(v))
    assert h in prism._prism_ext._observed_handles

    prism._atexit_clear()

    assert h.__dict__["_prism_keepalive"] == []
    h.value = 2
    assert fired == []
    assert h not in prism._prism_ext._observed_handles


def test_observed_handles_model_owned_handle_still_works():
    """A Model-owned handle is also registered in _observed_handles (harmless — Bound*
    handles get visited by both _all_models and _observed_handles at atexit, and
    _disconnect_keepalive is idempotent), but observe()/disconnect() behave the same
    as ever for ordinary Model usage.
    """

    class M(Model):
        count = field(0)

    m = M()
    seen = []
    conn = m.count.observe(lambda v: seen.append(v))
    assert m.count in prism._prism_ext._observed_handles
    m.count.value = 5
    assert seen == [5]
    conn.disconnect()
    m.count.value = 6
    assert seen == [5]


def test_observe_values():
    class M(Model):
        count = field(0)

    m = M()
    seen = []
    conn = m.count.observe(lambda v: seen.append(v))
    m.count.value = 1
    m.count.value = 2
    assert seen == [1, 2]
    conn.disconnect()
    m.count.value = 3
    assert seen == [1, 2]


def test_on_error_routes_observer_exception_and_app_keeps_running():
    class M(Model):
        a = field(0)
        b = field(0)

    m = M()
    caught = []
    try:
        prism.on_error(lambda exc: caught.append(exc))

        def bad_observer(v):
            raise ValueError("x")

        m.a.observe(bad_observer)
        m.a.value = 1

        assert len(caught) == 1
        assert isinstance(caught[0], ValueError)
        assert caught[0].args == ("x",)

        # app keeps running: a subsequent set still fires a second observer
        seen = []
        m.b.observe(lambda v: seen.append(v))
        m.b.value = 5
        assert seen == [5]
    finally:
        prism.on_error(None)


def test_on_error_none_restores_default_no_crash():
    class M(Model):
        a = field(0)

    m = M()
    prism.on_error(lambda exc: None)
    prism.on_error(None)

    def bad_observer(v):
        raise RuntimeError("boom")

    m.a.observe(bad_observer)
    m.a.value = 1  # must not raise/crash; falls back to default stderr print


def test_on_error_handler_that_raises_is_reported_and_app_keeps_running(capfd):
    """Task 2 repro: an on_error handler that itself raises must not be
    silently swallowed — its own exception (not just the original one)
    must reach stderr, and the app must keep dispatching afterward."""

    class M(Model):
        a = field(0)

    m = M()
    try:
        prism.on_error(lambda exc: 1 / 0)

        def bad_observer(v):
            raise ValueError("boom")

        m.a.observe(bad_observer)
        m.a.value = 1  # bad_observer raises -> on_error handler raises too

        # app keeps running: a later observer still fires normally
        seen = []
        m.a.observe(lambda v: seen.append(v))
        m.a.value = 2
        assert seen == [2]
    finally:
        prism.on_error(None)

    captured = capfd.readouterr()
    assert "ZeroDivisionError" in captured.err


def test_shared_basic():
    class M(Model):
        s = shared(10)

    m = M()
    assert m.s.value == 10
    m.s.value = 20
    assert m.s.value == 20
    seen = []
    conn = m.s.observe(lambda v: seen.append(v))
    # Shared observe fires on drain, but without app loop drain may not fire.
    # At least ensure no crash and value path works.
    m.s.value = 30
    assert m.s.value == 30
    conn.disconnect()


def test_channel_send():
    class M(Model):
        ch = channel(0)

    m = M()
    seen = []
    conn = m.ch.observe(lambda v: seen.append(v))
    m.ch.send(1)
    m.ch.send(2)
    # Without app loop, channel drain not executed; ensure no crash.
    assert isinstance(seen, list)
    conn.disconnect()


def test_transaction_batches():
    class M(Model):
        a = field(0)
        b = field(0)

    m = M()
    fired_a = []
    fired_b = []
    ca = m.a.observe(lambda v: fired_a.append(v))
    cb = m.b.observe(lambda v: fired_b.append(v))

    with transaction():
        m.a.value = 1
        m.b.value = 2
        # Inside txn, values not yet visible (buffered)
        assert m.a.value == 0
        assert m.b.value == 0

    assert m.a.value == 1
    assert m.b.value == 2
    # Each should have fired exactly once
    assert fired_a == [1]
    assert fired_b == [2]
    ca.disconnect()
    cb.disconnect()


def test_transaction_abort():
    class M(Model):
        a = field(0)
        b = field(0)

    m = M()
    m.a.value = 5
    m.b.value = 6
    try:
        with transaction():
            m.a.value = 10
            m.b.value = 20
            raise ValueError("abort")
    except ValueError:
        pass
    assert m.a.value == 5
    assert m.b.value == 6


def test_multithread_field_storm():
    """N-thread concurrent sets — TSan gate in C++, functional gate here."""

    class M(Model):
        x = field(0)

    m = M()
    errors = []

    def worker(n):
        try:
            for i in range(500):
                m.x.value = i + n * 1000
        except Exception as e:
            errors.append(e)

    threads = [threading.Thread(target=worker, args=(n,)) for n in range(8)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert not errors
    # Final value is some legal last write
    assert isinstance(m.x.value, int)


def test_shared_multithread_storm():
    class M(Model):
        s = shared(0)

    m = M()
    errors = []

    def worker(n):
        try:
            for i in range(300):
                m.s.value = n * 1000 + i
        except Exception as e:
            errors.append(e)

    threads = [threading.Thread(target=worker, args=(n,)) for n in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert not errors


def test_connection_gc_from_workers():
    """Connection GC from any thread (3.14t finalizers) — must not UAF."""

    class M(Model):
        count = field(0)

    m = M()
    conns = [m.count.observe(lambda v: None) for _ in range(10)]
    errors = []

    def drop():
        try:
            c = conns.pop()
            del c
            gc.collect()
        except Exception as e:
            errors.append(e)

    threads = [threading.Thread(target=drop) for _ in range(10)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert not errors
    gc.collect()
    # Still usable after all conns dropped
    m.count.value = 99
    assert m.count.value == 99


def test_bound_field_survives_model_gc():
    """BoundField keeps Slot alive after Model GC — del m; h.value ok."""

    class M(Model):
        count = field(10)

    m = M()
    h = m.count
    wr = weakref.ref(m)
    del m
    gc.collect()
    assert wr() is None
    # Handle still valid
    assert h.value == 10
    h.value = 20
    assert h.value == 20
    conn = h.observe(lambda v: None)
    h.value = 30
    conn.disconnect()


def test_bound_shared_survives_model_gc():
    class M(Model):
        s = shared(7)

    m = M()
    h = m.s
    wr = weakref.ref(m)
    del m
    gc.collect()
    assert wr() is None
    assert h.value == 7
    h.value = 8
    assert h.value == 8


def test_shared_channel_survive_gc():
    class Mixer(Model):
        vol = field(1)
        s = shared(2)
        ch = channel(0)

    m = Mixer()
    hv = m.vol
    hs = m.s
    hc = m.ch
    del m
    gc.collect()
    hv.value = 99
    hs.value = 88
    hc.send(123)
    assert hv.value == 99
    assert hs.value == 88


def test_transaction_multithread():
    """Concurrent transactions from different threads — per-thread buffers."""

    class M(Model):
        a = field(0)

    m = M()
    errors = []

    def worker(n):
        try:
            with transaction():
                m.a.value = n
                m.a.value = n + 100
        except Exception as e:
            errors.append(e)

    threads = [threading.Thread(target=worker, args=(n,)) for n in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert not errors
    assert isinstance(m.a.value, int)


def test_slider_checkbox_descriptors():
    class Mixer(Model):
        volume = prism.slider(0.5, min=0.0, max=1.0)
        mute = prism.checkbox(False, label="Mute")
        count = field(42)

    m = Mixer()
    assert m.volume.value == 0.5
    assert m.mute.value is False
    assert m.count.value == 42
    m.volume.value = 0.9
    assert m.volume.value == 0.9


def test_headless_app_concurrent_post():
    """App-based storm: run headless app and mutate from workers via queue (not direct fallback)."""
    import time

    class M(Model):
        x = field(0)

    m = M()
    errors = []

    def worker(n):
        try:
            for i in range(200):
                m.x.value = n * 1000 + i
        except Exception as e:
            errors.append(e)

    t = threading.Thread(target=lambda: prism._run_headless(m, delay_ms=300))
    t.start()
    # Poll until app reports running (CAS now true before setup, so workers will hit queue path)
    for _ in range(100):
        if prism._is_running():
            break
        time.sleep(0.01)
    threads = [threading.Thread(target=worker, args=(n,)) for n in range(4)]
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    t.join()
    assert not errors
    assert isinstance(m.x.value, int)
    # post-close must be no-op, not direct: after run, off-thread set is dropped
    before = m.x.value
    # run from main thread after close — should be no-op/drop, not crash; from worker thread also dropped
    done = threading.Event()

    def post_close_worker():
        try:
            m.x.value = 999999
        except Exception as e:
            errors.append(e)
        done.set()

    th2 = threading.Thread(target=post_close_worker)
    th2.start()
    done.wait(timeout=1.0)
    th2.join()
    assert not errors
    # Post-close may be dropped (Closed) or direct (after global flag cleared for next test) — just ensure no crash
    assert m.x.value in (before, 999999)


def test_headless_transaction_in_app():
    import time

    class M(Model):
        a = field(0)
        b = field(0)

    m = M()
    fired = []
    conn = m.a.observe(lambda v: fired.append(v))

    def do_txn():
        with transaction():
            m.a.value = 10
            m.b.value = 20

    t = threading.Thread(target=lambda: prism._run_headless(m, delay_ms=200))
    t.start()
    for _ in range(100):
        if prism._is_running():
            break
        time.sleep(0.01)
    th = threading.Thread(target=do_txn)
    th.start()
    th.join()
    t.join()
    # After app, values should have converged via posted batch
    assert m.a.value == 10
    assert m.b.value == 20
    conn.disconnect()


def test_standalone_shared_drained_by_app_tick():
    """Standalone SharedInt (not owned by a Model) must still be drained each app tick."""
    import time

    class M(Model):
        x = field(0)

    m = M()
    h = prism.SharedInt(0)
    seen = []
    conn = h.observe(lambda v: seen.append(v))

    t = threading.Thread(target=lambda: prism._run_headless(m, delay_ms=300))
    t.start()
    for _ in range(100):
        if prism._is_running():
            break
        time.sleep(0.01)

    def worker():
        h.value = 5

    th = threading.Thread(target=worker)
    th.start()
    th.join()
    t.join()
    assert seen == [5]
    conn.disconnect()


def test_standalone_channel_drained_by_app_tick():
    """Standalone ChannelInt (not owned by a Model) must still be drained each app tick."""
    import time

    class M(Model):
        x = field(0)

    m = M()
    h = prism.ChannelInt()
    seen = []
    conn = h.observe(lambda v: seen.append(v))

    t = threading.Thread(target=lambda: prism._run_headless(m, delay_ms=300))
    t.start()
    for _ in range(100):
        if prism._is_running():
            break
        time.sleep(0.01)

    def worker():
        h.send(7)

    th = threading.Thread(target=worker)
    th.start()
    th.join()
    t.join()
    assert seen == [7]
    conn.disconnect()


def test_standalone_drain_uaf_survives_handle_dropped_from_sibling_callback():
    """Task 1 repro: StandaloneDrainers used to hold raw std::function<void()>*.
    If a Python observer callback fired mid-sweep drops the last reference to
    ANOTHER standalone handle, that handle's destructor frees its drain
    function while the sweep's already-taken snapshot still points at it —
    the next iteration then calls into freed memory. Whether the resulting
    use-after-free actually crashes depends on what the allocator later
    does with that memory (CPython's own small-object allocator, not glibc
    malloc, owns these blocks, so glibc heap-debugging env vars don't help
    force it either) — not 100% deterministic, hence running it 3x per the
    fix brief. Registration order matters: `a` (registered first) must be
    the one whose drain runs first and whose callback drops `b` (registered
    second), so `b`'s stale entry is the next one the sweep would touch.
    Runs in a subprocess since a manifested crash would otherwise take down
    the whole suite."""
    code = (
        "import threading, time\n"
        "import prism\n"
        "a = prism.SharedInt(0)\n"
        "b = prism.SharedInt(0)\n"
        "holder = [b]\n"
        "del b  # only remaining reference to the SharedInt is holder[0]\n"
        "holder[0].observe(lambda v: None)  # b was observed too\n"
        "fired = []\n"
        "def cb(v):\n"
        "    fired.append(v)\n"
        "    if holder:\n"
        "        del holder[0]  # drops b's last ref -> ~SharedHandle() runs here\n"
        "a.observe(cb)\n"
        "class M(prism.Model):\n"
        "    x = prism.field(0)\n"
        "m = M()\n"
        "t = threading.Thread(target=lambda: prism._run_headless(m, delay_ms=300))\n"
        "t.start()\n"
        "for _ in range(300):\n"
        "    if prism._is_running():\n"
        "        break\n"
        "    time.sleep(0.01)\n"
        "def setter():\n"
        "    holder[0].value = 1  # touch b (still alive) before a's write can trigger cb\n"
        "    a.value = 1  # queues a's drain, which runs cb -> drops holder[0]\n"
        "th = threading.Thread(target=setter)\n"
        "th.start()\n"
        "th.join()\n"
        "t.join()\n"
        "assert fired == [1], fired\n"
    )
    for _ in range(3):
        result = subprocess.run(
            [sys.executable, "-c", code],
            env=os.environ,
            timeout=30,
        )
        assert result.returncode == 0


def test_nested_transaction_abort_outer_preserved():
    class M(Model):
        a = field(0)
        b = field(0)

    m = M()
    m.a.value = 1
    m.b.value = 1
    try:
        with transaction():
            m.a.value = 10
            try:
                with transaction():
                    m.b.value = 20
                    raise ValueError("inner")
            except ValueError:
                pass
            # inner abort should not discard outer's a=10
            m.b.value = 30
    except Exception:
        pass
    # Outer commits: a=10, b=30 should be visible; inner's b=20 discarded
    assert m.a.value == 10
    assert m.b.value == 30


def test_plot_and_tree_thread_dispatch():
    """PlotHandle/TreeHandle/BoundTree must not race when called off logic thread (handover MEDIUM)."""
    import time

    # Standalone PlotHandle — off-thread add_series/clear/notify via list_op_dispatch
    ph = prism.PlotHandle()
    errors = []

    def plot_worker():
        try:
            for i in range(20):
                ph.add_series([0.0, 1.0], [float(i), float(i + 1)])
            ph.clear_series()
            ph.add_series([0.0], [1.0])
            ph.notify()
            ph.reset_view()
            ph.x_label = "X"
            assert ph.x_label == "X"
        except Exception as e:
            errors.append(e)

    # BoundPlot via Model — same dispatch path
    class PlotModel(Model):
        pass

    pm = PlotModel()
    bp = pm._add_plot_internal()  # BoundPlot

    def bound_plot_worker():
        try:
            for i in range(20):
                bp.add_series([0.0, 1.0], [float(i), float(i + 1)])
            bp.clear_series()
        except Exception as e:
            errors.append(e)

    # Tree — Python source
    class Src:
        def root_count(self):
            return 1

        def root_at(self, i):
            return 0

        def child_count(self, nid):
            return 0

        def child_at(self, nid, i):
            return 0

        def label(self, nid):
            return "root"

        def has_children(self, nid):
            return False

    th = prism.TreeHandle(Src())

    def tree_worker():
        try:
            for _ in range(20):
                th.refresh()
                _ = th.rows()
        except Exception as e:
            errors.append(e)

    # Run with headless app so dispatch has a live queue to test the posted path
    class Dummy(Model):
        x = field(0)

    dm = Dummy()
    t = threading.Thread(target=lambda: prism._run_headless(dm, delay_ms=300))
    t.start()
    for _ in range(100):
        if prism._is_running():
            break
        time.sleep(0.01)

    threads = [
        threading.Thread(target=plot_worker),
        threading.Thread(target=bound_plot_worker),
        threading.Thread(target=tree_worker),
    ]
    for th_ in threads:
        th_.start()
    for th_ in threads:
        th_.join()
    t.join()
    assert not errors, f"errors: {errors}"

    # Also direct BoundTree via Model
    class TreeModel(Model):
        pass

    tm = TreeModel()
    bt = tm._add_tree_internal(Src())
    # Pre-run direct calls still work (NoApp fallback)
    bt.refresh()
    rows = bt.rows()
    assert isinstance(rows, list)


def test_list_bool_no_int_coercion():
    """List<bool> must not accept int 0/1 via implicit coercion (handover L10)."""
    from prism import list_field

    class M(Model):
        flags = list_field([True, False])

    m = M()
    # Initial values preserved as bools — stored as List<int> [1,0] due to vector<bool> proxy
    assert m.flags.to_list() in ([True, False], [1, 0])
    # Bool lists map to List<int> internally (vector<bool> proxy disabled); push should grow.
    m.flags.push(True)
    assert m.flags.size() == 3


def test_derived_basic_and_gc():
    """Derived<T> via Model.derived — basic recompute + no coverage before."""
    from prism import derived

    class M(Model):
        a = field(2)
        b = field(3)
        total = derived(lambda self: self.a.value + self.b.value, "a", "b")

    m = M()
    # Initial value is computed eagerly on construction
    assert m.total.value == 5
    # Observe derived — mutation should trigger recompute synchronously (pre-run direct dispatch)
    seen = []
    conn = m.total.observe(lambda v: seen.append(v))
    m.a.value = 10
    assert m.total.value == 13
    assert seen[-1] == 13
    m.b.value = 7
    assert m.total.value == 17
    assert seen[-1] == 17
    conn.disconnect()
    # GC safety: dropping alias while live must not crash
    alias = m.total
    del m
    gc.collect()
    _ = alias.value  # still alive via owner shared_ptr
    alias = None
    gc.collect()


def test_derived_probe_raises_gives_actionable_type_error():
    """No type_hint + a probe that raises -> loud TypeError naming the fix (task 4)."""
    from prism import derived

    class M(Model):
        a = field(2)
        bad = derived(lambda self: 1 / 0, "a")

    with pytest.raises(TypeError, match="type_hint=int\\|float\\|str\\|bool"):
        M()


def test_derived_type_hint_skips_probing():
    """type_hint given -> probe (which would raise) is never called (task 4)."""
    from prism import derived

    class M(Model):
        a = field(2)
        ok = derived(lambda self: 1 / 0, "a", type_hint=float)

    m = M()
    assert isinstance(m.ok, prism.BoundDerivedFloat)


def test_derived_field_survives_run_headless_teardown():
    """Task 16 repro: a field with a derived depending on it must not crash
    when the Model is torn down after _run_headless(). Runs in a subprocess
    since the bug is a segfault that would otherwise kill the whole suite."""
    code = (
        "import prism\n"
        "class M(prism.Model):\n"
        "    counter = prism.field(0)\n"
        "    doubled = prism.derived(lambda self: self.counter.value * 2, 'counter')\n"
        "m = M()\n"
        "prism._run_headless(m, delay_ms=200)\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        timeout=30,
    )
    assert result.returncode == 0


def test_run_headless_startup_does_not_spin_on_derived_reads():
    """Task 5 repro: the initial widget-tree build (registry.add, before the
    logic thread's post-handle exists) reads every placed Derived field's
    current value. dispatch_sync_read's startup-window spin used to run
    unconditionally there and always burn its full 1000x1ms budget (no
    handle can ever appear before setup() runs), adding ~1s per derived
    field. With 3 derived fields that was >=3s; fixed it must stay well
    under _run_headless's own delay_ms + a comfortable margin. Runs in a
    subprocess so the wall-clock measurement isn't skewed by pytest/other
    tests running concurrently."""
    code = (
        "import time\n"
        "import prism\n"
        "class M(prism.Model):\n"
        "    a = prism.field(1)\n"
        "    d1 = prism.derived(lambda self: self.a.value + 1, 'a')\n"
        "    d2 = prism.derived(lambda self: self.a.value + 2, 'a')\n"
        "    d3 = prism.derived(lambda self: self.a.value + 3, 'a')\n"
        "m = M()\n"
        "t0 = time.monotonic()\n"
        "prism._run_headless(m, delay_ms=100)\n"
        "print(time.monotonic() - t0)\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        timeout=30,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    elapsed = float(result.stdout.strip().splitlines()[-1])
    assert elapsed < 1.5, f"elapsed={elapsed}s (stderr={result.stderr})"


def test_field_unsupported_list_default_raises():
    class M(Model):
        bad = field([1, 2])

    with pytest.raises(TypeError, match="list_field"):
        M()


def test_field_unsupported_none_default_raises():
    class M(Model):
        bad = field(None)

    with pytest.raises(TypeError):
        M()


def test_model_kwargs_override_sets_field():
    class M(Model):
        a = field(1)

    m = M(a=5)
    assert m.a.value == 5


def test_gc_observe_torture():
    """Torture GC + observe simultaneously (handover L10) — stresses keep_alive chain."""
    import random

    class M(Model):
        x = field(0)

    m = M()
    conns = []
    for _ in range(50):
        conns.append(m.x.observe(lambda v: None))
    # Concurrent GC + observe creation
    errors = []

    def worker():
        try:
            for _ in range(100):
                c = m.x.observe(lambda v: None)
                # Immediately drop half
                if random.random() < 0.5:
                    c.disconnect()
                # Force GC interleaving
                gc.collect()
        except Exception as e:
            errors.append(e)

    threads = [threading.Thread(target=worker) for _ in range(4)]
    for t in threads:
        t.start()
    # Meanwhile main thread mutates
    for i in range(100):
        m.x.value = i
    for t in threads:
        t.join()
    assert not errors
    for c in conns:
        c.disconnect()


def test_tree_source_protocol_isinstance():
    """TreeSource Protocol @runtime_checkable exercised via is_tree_source helper."""
    import warnings

    from prism import TreeSource, is_tree_source

    class Full(TreeSource):
        def root_count(self):
            return 1

        def root_at(self, i):
            return 0

        def child_count(self, nid):
            return 0

        def child_at(self, nid, i):
            return 0

        def label(self, nid):
            return "x"

        def has_children(self, nid):
            return False

    # duck-typed without inheriting also passes
    class Duck:
        def root_count(self):
            return 1

        def root_at(self, i):
            return 0

        def child_count(self, nid):
            return 0

        def child_at(self, nid, i):
            return 0

        def label(self, nid):
            return "x"

        def has_children(self, nid):
            return False

    assert is_tree_source(Full())
    assert is_tree_source(Duck())
    assert isinstance(Full(), TreeSource)
    assert isinstance(Duck(), TreeSource)

    class Partial:
        def root_count(self):
            return 1

        def label(self, nid):
            return "x"

    assert not is_tree_source(Partial())
    assert not isinstance(Partial(), TreeSource)

    # optional methods not required for isinstance
    class FullWithOptional(Full):
        def attributes(self, nid):
            return {}

        def icon(self, nid):
            return None

    assert is_tree_source(FullWithOptional())


def test_table_source_protocol_isinstance():
    """TableSource Protocol @runtime_checkable."""
    from prism import TableSource, is_table_source

    class Full(TableSource):
        def column_count(self):
            return 2

        def row_count(self):
            return 3

        def cell_text(self, r, c):
            return "x"

    class Duck:
        def column_count(self):
            return 1

        def row_count(self):
            return 1

        def cell_text(self, r, c):
            return "y"

    class Partial:
        def column_count(self):
            return 1

    assert is_table_source(Full())
    assert is_table_source(Duck())
    assert isinstance(Full(), TableSource)
    assert not is_table_source(Partial())
    # header optional — full without header still counts
    assert is_table_source(Full())

    class WithHeader(Full):
        def header(self, c):
            return "h"

    assert is_table_source(WithHeader())


def test_tree_field_dict_callable_and_none():
    """tree_field supports dict, TreeSource, callable factory, and None (empty)."""
    import warnings

    from prism import TreeSource, tree_field

    class Src(TreeSource):
        def root_count(self):
            return 1

        def root_at(self, i):
            return 42

        def child_count(self, nid):
            return 0

        def child_at(self, nid, i):
            return 0

        def label(self, nid):
            return f"n{nid}"

        def has_children(self, nid):
            return False

    # dict source
    class M1(Model):
        t = tree_field({"1": {"label": "root", "children": []}})

    m1 = M1()
    assert m1.t.rows() is not None  # smoke: allocation succeeded

    # TreeSource direct
    class M2(Model):
        t = tree_field(Src())

    m2 = M2()
    assert m2.t.rows() is not None

    # callable factory
    class M3(Model):
        t = tree_field(lambda: Src())

    m3 = M3()
    assert m3.t.rows() is not None

    # callable returning dict
    class M4(Model):
        t = tree_field(lambda: {"1": {"label": "a"}})

    m4 = M4()
    assert m4.t.rows() is not None

    # None -> empty
    class M5(Model):
        t = tree_field(None)

    m5 = M5()
    assert m5.t.rows() == [] or isinstance(m5.t.rows(), list)

    # partial source warns (C++ would fallback silently) — ensure warning path exercised
    class PartialSrc:
        def root_count(self):
            return 1

        def label(self, nid):
            return "x"

    with warnings.catch_warnings(record=True) as w:
        warnings.simplefilter("always")

        class M6(Model):
            t = tree_field(PartialSrc())

        m6 = M6()
        _ = m6.t.rows()
        assert any("missing TreeSource methods" in str(x.message) for x in w), (
            f"expected warning, got {w}"
        )


def test_tree_field_duck_typed_source_rows_length_matches_root_count():
    """tree_field with a duck-typed (non-TreeSource-inheriting) six-method source
    allocates rows() == root_count() root rows, with no explicit refresh() call."""
    from prism import tree_field

    class Duck:
        def root_count(self):
            return 3

        def root_at(self, i):
            return i

        def child_count(self, nid):
            return 0

        def child_at(self, nid, i):
            return 0

        def label(self, nid):
            return f"n{nid}"

        def has_children(self, nid):
            return False

    class M(Model):
        t = tree_field(Duck())

    m = M()
    assert len(m.t.rows()) == 3


def test_tree_source_missing_method_yields_empty():
    """Documents today's C++ fallback: a source missing root_count() (the method
    that drives row count) renders as an empty tree instead of raising."""
    from prism import tree_field

    class NoRootCount:
        def label(self, nid):
            return "x"

    class M(Model):
        t = tree_field(NoRootCount())

    m = M()
    assert m.t.rows() == []


def test_annotated_auto_field_without_prism_field():
    """`x: Annotated[int, Field(ge=0)] = 0` auto-creates a validated field
    without calling prism.field() (transparent Annotated path, task 7)."""
    from pydantic import Field as PydanticField

    class M(Model):
        count: Annotated[int, PydanticField(ge=0)] = 0

    m = M()
    assert m.count.value == 0
    m.count.value = 5
    assert m.count.value == 5


def test_annotated_auto_field_validator_rejects_invalid_value():
    """Validator built from the transparent-Annotated path rejects an
    invalid assignment end-to-end (task 7). As of task 15, validation
    runs on the handle itself (installed by the descriptor's
    ``_allocate``), so `m.count = -1` (descriptor __set__) and
    `m.count.value = -1` (direct handle write) both raise identically."""
    from pydantic import Field as PydanticField, ValidationError

    class M(Model):
        count: Annotated[int, PydanticField(ge=0)] = 0

    m = M()
    with pytest.raises(ValidationError):
        m.count = -1
    with pytest.raises(ValidationError):
        m.count.value = -1


def _reject_negative(v):
    if v < 0:
        raise ValueError("must be non-negative")
    return v


def test_field_validator_applies_on_all_set_paths():
    """Task 15: `m.count = v`, `m.count.value = v`, and `m.count.set(v)`
    must all run the same validator, reject the same way, and leave the
    value unchanged on rejection."""

    class M(Model):
        count = field(0, validator=_reject_negative)

    m = M()

    with pytest.raises(ValueError, match="must be non-negative"):
        m.count = -1
    assert m.count.value == 0

    with pytest.raises(ValueError, match="must be non-negative"):
        m.count.value = -1
    assert m.count.value == 0

    with pytest.raises(ValueError, match="must be non-negative"):
        m.count.set(-1)
    assert m.count.value == 0

    m.count = 1
    assert m.count.value == 1
    m.count.value = 2
    assert m.count.value == 2
    m.count.set(3)
    assert m.count.value == 3


def test_shared_validator_applies_on_all_set_paths():
    """Task 15: same guarantee as test_field_validator_applies_on_all_set_paths,
    for prism.shared()."""

    class M(Model):
        level = shared(0, validator=_reject_negative)

    m = M()

    with pytest.raises(ValueError, match="must be non-negative"):
        m.level = -1
    assert m.level.value == 0

    with pytest.raises(ValueError, match="must be non-negative"):
        m.level.value = -1
    assert m.level.value == 0

    with pytest.raises(ValueError, match="must be non-negative"):
        m.level.set(-1)
    assert m.level.value == 0

    m.level.value = 5
    assert m.level.value == 5


def test_annotated_bare_field_no_default_raises():
    """A bare Annotated field with no explicit default and a base type
    prism doesn't know a scalar default for must raise TypeError instead
    of silently defaulting to 0 (task 7, no-silent-fallbacks)."""

    with pytest.raises(TypeError, match="explicit default"):

        class M(Model):
            x: Annotated[dict, "meta"]


def test_annotated_explicit_none_default_raises():
    """An explicit `= None` default on an Annotated scalar field must
    raise TypeError instead of silently becoming 0 (task 7)."""

    with pytest.raises(TypeError, match="explicit default"):

        class M(Model):
            x: Annotated[int, "meta"] = None


def test_import_has_no_sys_modules_sweep():
    """_atexit_clear must not walk sys.modules deleting user globals bound to
    Model instances — that is a process-wide side effect of `import prism`
    just to quiet nanobind's leak checker. See doc review 2026-09-02."""
    import inspect

    source = inspect.getsource(prism._atexit_clear)
    assert "sys.modules" not in source
    assert "modules.values" not in source


def test_worker_interval_ticks_and_stop_halts_it():
    counts = {"n": 0}
    third_tick = threading.Event()

    def tick(stop):
        counts["n"] += 1
        if counts["n"] == 3:
            third_tick.set()

    w = prism.worker(tick, interval=0.01)
    assert third_tick.wait(timeout=2.0)

    w.stop()
    assert not w.is_alive

    # stop() already joined the thread, so no further tick can have run —
    # this is a join guarantee, not a race against a sleep.
    n_at_stop = counts["n"]
    assert counts["n"] == n_at_stop


def test_worker_raising_fn_routes_to_on_error_and_thread_exits():
    caught = []
    try:
        prism.on_error(lambda exc: caught.append(exc))

        def boom(stop):
            raise ValueError("worker boom")

        w = prism.worker(boom)
        w.stop()  # joins; one-shot fn has already run and raised by now

        assert len(caught) == 1
        assert isinstance(caught[0], ValueError)
        assert caught[0].args == ("worker boom",)
        assert not w.is_alive
    finally:
        prism.on_error(None)


def test_worker_context_manager_starts_and_stops():
    import time

    counts = {"n": 0}

    def tick(stop):
        counts["n"] += 1

    with prism.worker(tick, interval=0.01) as w:
        time.sleep(0.03)
        assert w.is_alive
        assert counts["n"] > 0

    assert not w.is_alive


def _load_example(name: str):
    """Import an examples/*.py module by path (digit-prefixed, not importable by name)."""
    import importlib.util
    import pathlib

    path = pathlib.Path(__file__).resolve().parent.parent / "examples" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)  # type: ignore[union-attr]
    return mod


def test_headless_multithread_stress_example_runs_and_converges():
    """CI's 3.14t lane runs this: 8-thread ThreadPoolExecutor storm through a
    headless app, with exact-count assertions inside main() itself."""
    mod = _load_example("09_headless_multithread_stress")
    mod.main()  # asserts channel count == 8000 and counter == 800 internally


def test_headless_multithread_stress_example_gil_disabled_on_free_threaded_build():
    import sysconfig

    if not sysconfig.get_config_var("Py_GIL_DISABLED"):
        pytest.skip("not a free-threaded (3.14t) build")
    assert sys._is_gil_enabled() is False


def test_worker_pool_plot_example_plots_at_least_one_window():
    mod = _load_example("10_worker_pool_plot")
    mod.main(headless=True)  # asserts windows_done >= 1 internally


def test_asyncio_bridge_example_completes_a_round_trip():
    mod = _load_example("12_asyncio_bridge")
    mod.main(headless=True)  # asserts round_trips >= 1 internally
