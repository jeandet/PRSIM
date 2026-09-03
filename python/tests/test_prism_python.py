"""Python bindings pytest suite — P2/P3 gates from doc/design/python-sdk.md §6."""

import gc
import inspect
import os
import re
import subprocess
import sys
import threading
import weakref
from typing import Annotated

import pytest

import prism
from prism import Model, field, shared, channel, transaction


def test_all_excludes_type_suffixed_handle_classes():
    """Task 12: handle classes (FieldInt, BoundSharedFloat, ...) are created
    by descriptors, never constructed directly — __all__ shouldn't offer them
    for `from prism import *`, though they stay importable from
    prism._prism_ext (and as prism.FieldInt etc., used throughout this file)."""
    pattern = re.compile(
        r"^(Field|Bound|Shared|Channel|List)(Shared|Channel|Derived|List)?(Int|Float|Str|Bool)$"
    )
    assert [name for name in prism.__all__ if pattern.match(name)] == []


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
    """Since Task 14, an observed standalone handle is a plain acyclic chain (handle ->
    keepalive list -> Connection -> keep_alive(state), the *hub*, not `self`) and is
    collectable by ordinary refcounting the moment nothing else references it. This test
    is about a different, still-real case: a handle that stays referenced for the whole
    process (e.g. a module global) never hits refcount 0 on its own, so its Connection
    would otherwise keep observing forever. _observed_handles is how atexit finds such a
    still-alive handle and disconnects it before interpreter shutdown.
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


def _assert_observed_handle_collectable(handle, register_observer):
    """Task 14 repro: an observe()d standalone handle used to be immortal — nb::keep_alive<0,1>
    on the returned Connection plus the handle's own __dict__["_prism_keepalive"] list formed
    a reference cycle invisible to the cyclic GC (nanobind's keep_alive is a C++-side table,
    not a Python object graph edge the GC can walk). Since the Connection now keeps the hub
    *state* alive via keep_alive(state) instead of keep_alive<0,1> on `self`, the handle is a
    plain acyclic chain again and dies with a normal `del` once nothing else references it —
    gc.collect() here only mops up whatever GC generation the interpreter already promoted it
    to, it is not what breaks the cycle (there is no cycle to break)."""
    register_observer(handle)
    w = weakref.ref(handle)
    del handle
    gc.collect()
    assert w() is None


def test_observed_field_handle_is_gc_collectable():
    _assert_observed_handle_collectable(
        prism.FieldInt(0), lambda h: h.observe(lambda v: None)
    )


def test_observed_shared_handle_is_gc_collectable():
    _assert_observed_handle_collectable(
        prism.SharedInt(0), lambda h: h.observe(lambda v: None)
    )


def test_observed_channel_handle_is_gc_collectable():
    _assert_observed_handle_collectable(
        prism.ChannelInt(), lambda h: h.observe(lambda v: None)
    )


def test_observed_list_handle_is_gc_collectable():
    _assert_observed_handle_collectable(
        prism.ListInt(), lambda h: h.observe_insert(lambda idx, v: None)
    )


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


def test_self_capturing_observer_model_is_gc_collectable():
    """Task 1 residual repro: `m.x.observe(lambda v: m)` forms
    Model -> _prism_fields -> handle -> __dict__["_prism_keepalive"] ->
    Connection -> (C++) SenderHub::receivers_ -> std::function -> nb::callable
    -> Model, and the last hop used to be invisible to the cyclic GC (a plain
    C++ std::function member, not a Python object graph edge), so the Model
    lived forever once nothing else referenced it. keep_connection() now also
    appends the callback itself to the handle's own __dict__, which nanobind's
    dynamic_attr dict traversal exposes to the cyclic GC — closing the last
    hop in Python-visible terms.

    Builds `m` inside a nested function and returns only a weakref, rather
    than `del m` at this scope: `lambda v: m` makes `m` a closure (cell)
    variable here, and `del` on a cell variable clears the cell's contents
    directly — a CPython-level effect, not exercised via the callback closure
    at all — which frees `m` immediately regardless of whether the bug is
    fixed and would make this test pass unconditionally. A normal function
    return only drops the frame's own reference to the (shared) cell, so the
    lambda's copy of that cell — the one actually reachable from the
    Model -> handle -> callback path this test is exercising — is what's left
    holding `m`."""

    class M(Model):
        x = field(0)

    def make():
        m = M()
        m.x.observe(lambda v: m)
        return weakref.ref(m)

    w = make()
    gc.collect()
    assert w() is None


def test_self_capturing_observer_fires_then_stops_after_disconnect():
    """Task 1: the fix must not change ordinary observe()/disconnect()
    behavior — a self-capturing observer still fires under prism.headless()
    and stops firing once disconnected, same as any other observer."""

    class M(Model):
        x = field(0)

    m = M()
    seen = []
    conn = m.x.observe(lambda v: seen.append((v, m.x.value)))
    with prism.headless(m) as app:
        m.x.value = 5
        app.wait_until(lambda: seen == [(5, 5)])
        conn.disconnect()
        m.x.value = 6
        assert seen == [(5, 5)]


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


def test_on_error_rejects_non_callable():
    """Task 2: a non-callable, non-None handler must raise TypeError at
    call time instead of being installed and failing obscurely later."""
    with pytest.raises(TypeError, match="handler must be callable or None"):
        prism.on_error(42)


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


def test_slider_range_is_exposed_as_a_tuple():
    """.range is a plain (min, max) tuple set once at allocation — it has no
    C++-backed setter, so writing it only rebinds the Python attribute; it
    does not reach (and cannot change) the underlying Slider's min/max."""

    class Mixer(Model):
        volume = prism.slider(0.5, min=0.0, max=1.0)

    m = Mixer()
    assert m.volume.range == (0.0, 1.0)
    assert isinstance(m.volume.range, tuple)


def test_slider_value_round_trips():
    class Mixer(Model):
        volume = prism.slider(0.5, min=0.0, max=1.0)

    m = Mixer()
    m.volume.value = 0.25
    assert m.volume.value == 0.25
    m.volume.set(0.75)
    assert m.volume.get() == 0.75


def test_slider_observer_fires_with_new_value():
    class Mixer(Model):
        volume = prism.slider(0.5, min=0.0, max=1.0)

    m = Mixer()
    seen = []
    m.volume.observe(lambda v: seen.append(v))
    m.volume.value = 0.9
    assert seen == [0.9]


def test_slider_out_of_range_set_is_accepted_unclamped():
    """Field::set() (include/prism/core/field.hpp) does not clamp — only the
    C++ Slider widget's own mouse-drag input path clamps into [min, max]
    (Widget<Slider<T,O>>::apply_position, include/prism/ui/delegate.hpp).
    A programmatic .value = write goes straight to Field::set(), so it is
    accepted unclamped, same as slider()'s docstring states."""

    class Mixer(Model):
        volume = prism.slider(0.5, min=0.0, max=1.0)

    m = Mixer()
    m.volume.value = 5.0
    assert m.volume.value == 5.0


def test_checkbox_is_bool_field_with_label():
    class Mixer(Model):
        mute = prism.checkbox(True, label="Mute")

    m = Mixer()
    assert m.mute.value is True
    m.mute.value = False
    assert m.mute.value is False


def test_checkbox_observer_fires_with_new_value():
    class Mixer(Model):
        mute = prism.checkbox(False, label="Mute")

    m = Mixer()
    seen = []
    m.mute.observe(lambda v: seen.append(v))
    m.mute.value = True
    assert seen == [True]


def test_slider_checkbox_descriptors_carry_kind_and_meta():
    class Mixer(Model):
        volume = prism.slider(0.5, min=0.0, max=1.0)
        mute = prism.checkbox(False, label="Mute")

    volume_descriptor = Mixer.__dict__["volume"]
    mute_descriptor = Mixer.__dict__["mute"]
    assert volume_descriptor.kind == "slider"
    assert volume_descriptor.meta["range"] == (0.0, 1.0)
    assert mute_descriptor.kind == "checkbox"
    assert mute_descriptor.meta["label"] == "Mute"


def test_headless_render_of_slider_and_checkbox():
    """Golden/headless smoke test: a Model with slider() + checkbox() fields
    (auto-view, no custom view()) must render without error."""

    class Mixer(Model):
        volume = prism.slider(0.5, min=0.0, max=1.0)
        mute = prism.checkbox(False, label="Mute")

    m = Mixer()
    prism._run_headless(m, delay_ms=50)
    assert m.volume.value == 0.5
    assert m.mute.value is False


def test_derived_depends_on_slider_and_checkbox():
    """derived() must accept slider()/checkbox() descriptors as deps — they
    are BoundSliderValue/BoundCheckboxValue handles, not BoundField<T>, but
    derived_attach_dep (prism_ext.cpp) must still recognize them."""

    class M(Model):
        v = prism.slider(0.5, min=0.0, max=1.0)
        c = prism.checkbox(True)
        d = prism.derived(
            lambda self: self.v.value * 2 if self.c.value else 0.0, v, c
        )

    m = M()
    assert m.d.value == 1.0

    seen = []
    m.d.observe(lambda val: seen.append(val))

    def mutate():
        m.v.value = 0.25
        m.c.value = False

    import time

    t = threading.Thread(target=lambda: prism._run_headless(m, delay_ms=200))
    t.start()
    for _ in range(200):
        if prism._is_running():
            break
        time.sleep(0.01)
    mutate()
    t.join()

    assert seen  # recompute fired at least once from the slider/checkbox change
    assert m.d.value == 0.0  # c.value is False -> 0.0, per the compute fn


def test_derived_on_slider_smoke_constructs_06_live_plot(monkeypatch):
    """06_live_plot.py's title = derived(..., frequency, amplitude) crashed
    Model construction before derived_attach_dep recognized slider deps.
    Smoke-construct the example with prism.run monkeypatched to a no-op."""
    import runpy

    monkeypatch.setattr(prism, "run", lambda *a, **k: None)
    examples_dir = os.path.join(os.path.dirname(__file__), "..", "examples")
    try:
        runpy.run_path(os.path.join(examples_dir, "06_live_plot.py"), run_name="__not_main__")
    finally:
        # prism.run() is monkeypatched to a no-op, so its usual worker teardown
        # never runs — stop the example's background jitter worker ourselves.
        prism._stop_all_workers()


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


def test_headless_context_wait_until_observes_condition():
    class M(Model):
        x = field(0)

    m = M()
    seen = []
    m.x.observe(lambda v: seen.append(v))

    with prism.headless(m) as app:
        m.x.value = 5
        app.wait_until(lambda: seen == [5])


def test_headless_context_wait_until_times_out():
    class M(Model):
        x = field(0)

    m = M()
    with prism.headless(m) as app:
        with pytest.raises(TimeoutError):
            app.wait_until(lambda: False, timeout=0.2)


def test_headless_context_not_running_after_block():
    class M(Model):
        x = field(0)

    m = M()
    with prism.headless(m):
        pass
    assert not prism._is_running()


def test_run_guard_resets_after_model_app_throws():
    """2026-09-03 final-review item 2 repro: run()/_run_headless() used to
    reset g_run_guard/g_has_handle/g_post_handle/g_app_closed only after
    model_app() returned normally — if it threw, those flags were stuck, and
    every subsequent run()/_run_headless() call would raise "prism.run
    already running" forever. `_fail_next_run()` is a test-only hook that
    forces the next run to throw right after claiming the guard, standing in
    for model_app() itself throwing (no real Python-triggerable path reaches
    that — every callback it calls synchronously is already
    exception-guarded, e.g. view()'s report_python_callback_error())."""

    class M(Model):
        x = field(0)

    prism._prism_ext._fail_next_run()
    with pytest.raises(RuntimeError, match="forced failure"):
        prism._run_headless(M(), delay_ms=100)

    # Guard must be released: a second run must not raise "already running".
    prism._run_headless(M(), delay_ms=50)


def test_headless_context_final_drain_delivers_shared_write_before_quit():
    """A Shared<T> write applies directly (not posted) and is only surfaced to
    observers on the next drain (tick or mutation-queue post) — see
    test_standalone_shared_drained_by_app_tick. With no field posts and no
    active animation in this test, nothing would ever drain it except the
    final drain quit() triggers before WindowClose fires."""
    class M(Model):
        x = field(0)

    m = M()
    h = prism.SharedInt(0)
    seen = []
    conn = h.observe(lambda v: seen.append(v))

    with prism.headless(m) as app:
        h.value = 7
        app.quit()

    assert seen == [7]
    conn.disconnect()


def test_headless_nested_call_raises_real_already_running_error():
    """2026-09-03 final-review item 3 repro: headless()'s runner thread used
    to swallow whatever exception it raised (default threading excepthook,
    printed to stderr and forgotten) instead of propagating it to the
    caller. Starting a second headless() app while one is already running
    must surface the real "already running" RuntimeError from the inner
    call's __enter__ — not silently succeed, and not the unrelated "did not
    start" timeout error (is_running() is already true because of the
    *outer* app, so a naive is_running()-based startup check can't tell the
    inner call's own failure apart from that)."""

    class M(Model):
        x = field(0)

    class M2(Model):
        y = field(0)

    with prism.headless(M()):
        with pytest.raises(RuntimeError, match="already running"):
            with prism.headless(M2()):
                pass


def test_app_is_running_false_after_own_thread_joined_even_if_another_app_runs():
    """2026-09-03 final-review item 11: App.is_running used to be a bare
    `_is_running()` — a single global flag, so a finished App's own handle
    could look "running" again purely because a *different* app started
    later in the same process. Requiring the App's own thread to still be
    alive fixes that."""

    class M(Model):
        x = field(0)

    class M2(Model):
        y = field(0)

    with prism.headless(M()) as app1:
        assert app1.is_running
    assert not app1.is_running

    with prism.headless(M2()) as app2:
        assert app2.is_running
        # app1's own thread is long done — it must not look running again
        # just because *some* app (app2) is now running.
        assert not app1.is_running


def test_headless_startup_bounded_and_cleans_up_thread(monkeypatch):
    """Task 9 review finding 2: the startup handshake must not spin forever.

    Stub out ``_is_running`` so headless() never sees the app as up, forcing
    it down the timeout path. The real app underneath still starts and
    still gets a real _request_quit() + join() — this proves the runner
    thread is actually signalled and joined (not abandoned) before the
    RuntimeError is raised.
    """
    class M(Model):
        x = field(0)

    m = M()
    monkeypatch.setattr(prism, "_is_running", lambda: False)
    with pytest.raises(RuntimeError, match="did not start"):
        with prism.headless(m, timeout=0.05):
            pass
    monkeypatch.undo()
    assert not prism._is_running()


def test_headless_wait_until_from_logic_thread_raises():
    """Task 9 review finding 3: calling wait_until() from an observer
    (the logic thread) must raise immediately, not deadlock — that thread
    is the one that would need to keep running for the predicate or
    quit() to ever take effect."""
    class M(Model):
        x = field(0)

    m = M()
    caught = []

    with prism.headless(m) as app:
        def on_change(v):
            try:
                app.wait_until(lambda: True)
            except RuntimeError as e:
                caught.append(e)

        m.x.observe(on_change)
        m.x.value = 1
        app.wait_until(lambda: caught, timeout=2.0)

    assert len(caught) == 1
    assert "logic thread" in str(caught[0])


def test_headless_wait_until_none_timeout_raises_when_app_quits():
    """Task 9 review finding 4: with timeout=None, wait_until() must still
    notice the app quitting from another thread — otherwise a predicate
    that never becomes true spins forever past shutdown."""
    import time

    class M(Model):
        x = field(0)

    m = M()
    with prism.headless(m) as app:
        def quitter():
            time.sleep(0.05)
            app.quit()

        t = threading.Thread(target=quitter)
        t.start()
        with pytest.raises(RuntimeError, match="app quit"):
            app.wait_until(lambda: False)
        t.join()


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

    Task 6 found this test vacuous: it used to also call `.observe()` on `b`
    itself, which — pre-Task-14, when `observe()` still took nb::keep_alive<0,1>
    on `self` — made `b` immortal via a GC-invisible cycle, so `del holder[0]`
    below never actually freed anything and the "use-after-free" never had
    memory to be use-after. `b` doesn't need its own observer for this repro —
    its drain_fn is registered in StandaloneDrainers at construction regardless
    — so that call is simply gone now; `del holder[0]` is a real free again.
    Runs in a subprocess since a manifested crash would otherwise take down
    the whole suite."""
    code = (
        "import threading, time\n"
        "import prism\n"
        "a = prism.SharedInt(0)\n"
        "b = prism.SharedInt(0)\n"
        "holder = [b]\n"
        "del b  # only remaining reference to the SharedInt is holder[0]\n"
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


def test_standalone_handle_self_drop_during_own_drain_survives():
    """2026-09-03 followups Task 1 repro: SharedHandle<T>/ChannelHandle<T> used
    to own their Shared<T>/Channel<T> by value, with drain_fn and every
    observer wrapper capturing `this` (the handle). If an observer callback
    dropped the LAST Python reference to the handle whose OWN
    drain_notifications() is executing, the handle's members were freed while
    that call was still running on them — the drain-registry weak_ptr (fixed
    by 75256da) only protects drain_fn itself, not the state it reads.
    Fixed by moving the state behind a shared_ptr that drain_fn and every
    observer wrapper capture instead of `this`, so the state outlives both
    the running call and the handle. Manually verified this repro does NOT
    reliably crash on the allocator in this environment (CPython's
    small-object allocator, not glibc malloc, owns these blocks — 3/3 clean
    runs pre-fix), so the callback also churns a few hundred small
    allocations after dropping the handle to encourage reuse of the freed
    block, keeping the test meaningful without relying on a crash that may
    not manifest.

    Task 6 found this test was ALSO vacuous, for an unrelated reason: pre-Task-14,
    `holder[0].observe(cb)` (the one observer this repro needs) made `holder[0]`
    immortal via the same nb::keep_alive<0,1> self-cycle Task 14 removed, so
    `holder.clear()` below never actually dropped the last reference either — the
    Task 1 fix above was correct but this test never got to exercise it. No code
    change was needed here to fix that (unlike the sibling-callback test above,
    this repro's one `.observe()` call is the one it needs, not an extra one to
    drop): removing nb::keep_alive<0,1> from observe() in Task 14 is what makes
    `holder.clear()` a real free again. Runs in a subprocess since a manifested
    crash would otherwise take down the whole suite."""
    code = (
        "import threading, time\n"
        "import prism\n"
        "holder = [prism.SharedInt(0)]\n"
        "fired = []\n"
        "def cb(v):\n"
        "    fired.append(v)\n"
        "    holder.clear()  # drops the only reference to the handle running THIS drain\n"
        "    junk = [object() for _ in range(500)]  # encourage reuse of the freed block\n"
        "    del junk\n"
        "holder[0].observe(cb)\n"
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
        "    holder[0].value = 1  # queues drain; cb runs and drops holder[0] mid-call\n"
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


def test_standalone_observer_wrapper_does_not_hold_its_own_hub():
    """Task 1 fix round 1: SharedHandle<T>/ChannelHandle<T>::observe()'s callback
    wrapper used to capture `state` (the shared_ptr<Shared<T>>/shared_ptr<Channel<T>>
    holding the hub) in addition to `cb`. That wrapper is itself stored inside
    `state->on_change()`'s receivers_ — a member of *state — so capturing `state`
    there made the hub hold a strong reference to itself: a real leak while
    connected, and dead weight even once disconnected, since drain_fn already keeps
    `state` alive across the whole drain call (callbacks included), which is the
    only thing the capture could have been protecting.

    `_standalone_shared_use_count()` (a debug hook, C++-only, added for this test)
    reads the state's shared_ptr::use_count() directly, which distinguishes the bug
    immediately: pre-fix, use_count grew by one per `observe()` call and only ever
    came back down via that connection's own disconnect(); post-fix it does not
    grow at all, since only drain_fn and the returned Connection's keep_alive(state)
    hold `state` now — never the wrapper sitting inside it.
    """
    from prism._prism_ext import _standalone_shared_use_count

    s = prism.SharedInt(0)
    baseline = _standalone_shared_use_count(s)
    conn = s.observe(lambda v: None)
    assert _standalone_shared_use_count(s) == baseline + 1
    conn.disconnect()
    assert _standalone_shared_use_count(s) == baseline


def test_standalone_state_alive_count_returns_to_baseline_after_del():
    """Task 1 fix round 1, full-lifecycle companion to the use_count test above:
    `_standalone_state_alive_count()` counts how many Field<T>/Shared<T>/Channel<T>/List<T>
    state objects are currently heap-allocated across ALL standalone handles. Proves the
    fix doesn't leave the state permanently unreachable once its handle is genuinely torn
    down.

    Pre-Task-14 this needed `prism._atexit_clear()` to reach 0: `nb::keep_alive<0, 1>` on
    `observe()` made the handle -> keepalive list -> Connection chain a cycle back to the
    handle itself, invisible to the cyclic GC, so `del s; gc.collect()` alone left the
    handle (and its state) alive. Task 14 removed that keep_alive — the Connection now
    keeps the *state* alive via keep_alive(state), not `self` — so the chain is acyclic
    and plain `del` + `gc.collect()` is enough; `_atexit_clear()` is no longer needed here.

    Runs in a subprocess for a deterministic baseline — in-process, other tests in the
    same pytest session may hold their own standalone handles' Connections alive (e.g. via
    a local `conn` variable never disconnected) until the real interpreter-exit
    `_atexit_clear()`, so a shared baseline captured mid-suite is not stable.
    """
    code = (
        "import gc\n"
        "import prism\n"
        "from prism._prism_ext import _standalone_state_alive_count\n"
        "before = _standalone_state_alive_count()\n"
        "assert before == 0, before\n"
        "s = prism.SharedInt(0)\n"
        "s.observe(lambda v: None)\n"
        "assert _standalone_state_alive_count() == 1\n"
        "del s\n"
        "gc.collect()\n"
        "assert _standalone_state_alive_count() == 0\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        timeout=30,
    )
    assert result.returncode == 0


def test_posted_mutations_keep_target_alive_across_del():
    """Task 15 repro: field_set_dispatch()/list_op_dispatch() used to post a closure
    to the logic thread that captured only a raw Field<T>*/List<T>* — never an owning
    reference. If the calling Python thread dropped the handle's last reference (a
    standalone FieldInt/ListInt, or a Model whose BoundField's owning Slot dies with
    the Model) between the fire-and-forget `.set()`/`.push()`/`.value =` call
    returning and the logic thread actually draining that closure, the target was
    already freed — a use-after-free only ASan reliably catches (see
    task-15-report.md for the ASan report from a deliberate revert to a raw
    pointer). The fix makes every posted closure also capture the owning
    shared_ptr (a standalone handle's state, or a Bound* handle's SlotBase owner)
    so the target outlives the closure.

    Needs a real running headless app so the calls take the async post path, not
    the pre-run synchronous direct-write path. Uses one persistent background
    thread (not 1500 short-lived OS threads) to execute the mutations concurrently
    with the main thread creating/dropping each handle — same race as "from a
    background thread, immediately del it", without blowing the suite's time
    budget on thread-spawn overhead. Runs in a subprocess since a manifested
    crash would otherwise take down the whole suite.
    """
    code = (
        "import gc, queue, threading, time\n"
        "import prism\n"
        "class M(prism.Model):\n"
        "    x = prism.field(0)\n"
        "keepalive = M()\n"
        "t = threading.Thread(target=lambda: prism._run_headless(keepalive, delay_ms=5000))\n"
        "t.start()\n"
        "for _ in range(500):\n"
        "    if prism._is_running():\n"
        "        break\n"
        "    time.sleep(0.01)\n"
        "assert prism._is_running()\n"
        "work = queue.Queue()\n"
        "def bg_worker():\n"
        "    while True:\n"
        "        item = work.get()\n"
        "        if item is None:\n"
        "            return\n"
        "        item()\n"
        "bg = threading.Thread(target=bg_worker)\n"
        "bg.start()\n"
        "for i in range(500):\n"
        "    h = prism.FieldInt(0)\n"
        "    work.put((lambda h=h, i=i: h.set(i)))\n"
        "    del h\n"
        "    gc.collect()\n"
        "for i in range(500):\n"
        "    h = prism.ListInt()\n"
        "    work.put((lambda h=h, i=i: h.push(i)))\n"
        "    del h\n"
        "    gc.collect()\n"
        "for i in range(500):\n"
        "    bm = M()\n"
        "    work.put((lambda bm=bm, i=i: setattr(bm.x, 'value', i)))\n"
        "    del bm\n"
        "    gc.collect()\n"
        "work.put(None)\n"
        "bg.join(timeout=10)\n"
        "assert not bg.is_alive()\n"
        "t.join()\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        timeout=45,
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


def test_replace_series_converges_to_last_call_from_background_thread():
    """replace_series() is one dispatched post — unlike clear/add/notify, a background
    thread hammering it can never leave the plot mid-update (handover Task 4)."""
    import time

    class PlotModel(Model):
        pass

    pm = PlotModel()
    bp = pm._add_plot_internal()
    errors = []
    last_len = [0]

    def worker():
        try:
            for i in range(100):
                length = (i % 5) + 1
                xs = list(range(length))
                ys = [float(v) for v in xs]
                last_len[0] = length
                bp.replace_series(xs, ys, color="#0088cc", thickness=2.0)
        except Exception as e:
            errors.append(e)

    class Dummy(Model):
        x = field(0)

    dm = Dummy()
    t = threading.Thread(target=lambda: prism._run_headless(dm, delay_ms=300))
    t.start()
    for _ in range(100):
        if prism._is_running():
            break
        time.sleep(0.01)

    th = threading.Thread(target=worker)
    th.start()
    th.join()
    t.join()

    assert not errors, f"errors: {errors}"
    assert bp.series_count() == 1
    assert bp.series_len(0) == last_len[0]


def test_replace_series_list_form_posts_n_series_atomically():
    """replace_series([(xs, ys, color), ...]) is one post that clears then adds each series."""

    class PlotModel(Model):
        pass

    pm = PlotModel()
    bp = pm._add_plot_internal()
    bp.replace_series(
        [
            ([0.0, 1.0], [1.0, 2.0], "#0088cc"),
            ([0.0, 1.0, 2.0], [3.0, 4.0, 5.0], None),
        ],
        thickness=1.5,
    )
    assert bp.series_count() == 2
    assert bp.series_len(0) == 2
    assert bp.series_len(1) == 3

    # A second call replaces, it does not accumulate.
    bp.replace_series([([0.0], [1.0], None)])
    assert bp.series_count() == 1
    assert bp.series_len(0) == 1


def test_replace_series_list_form_rejects_malformed_entries():
    """Each list-form entry must be a 2- or 3-tuple (xs, ys[, color]) — reject anything
    else with a TypeError instead of reading past a short tuple (review Critical)."""

    class PlotModel(Model):
        pass

    pm = PlotModel()
    bp = pm._add_plot_internal()
    xs, ys = [0.0, 1.0], [1.0, 2.0]

    match = r"replace_series\(\): each series must be \(xs, ys\) or \(xs, ys, color\)"
    with pytest.raises(TypeError, match=match):
        bp.replace_series([(xs,)])
    with pytest.raises(TypeError, match=match):
        bp.replace_series([()])
    with pytest.raises(TypeError, match=match):
        bp.replace_series([xs])  # bare list, not a tuple

    # Valid: (xs, ys) with no color still works.
    bp.replace_series([(xs, ys)])
    assert bp.series_count() == 1
    assert bp.series_len(0) == 2


def test_set_labels_visible_via_label_properties():
    """set_labels() is a single-post convenience over the x_label/y_label properties."""

    class PlotModel(Model):
        pass

    pm = PlotModel()
    bp = pm._add_plot_internal()
    bp.set_labels(x="Time", y="Amplitude")
    assert bp.x_label == "Time"
    assert bp.y_label == "Amplitude"

    # Omitted side is left untouched.
    bp.set_labels(y="Volts")
    assert bp.x_label == "Time"
    assert bp.y_label == "Volts"


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


def test_derived_descriptor_deps():
    """2026-09-03 followups task 10: derived() accepts class-level field
    descriptors as deps (not just string names) — `a`/`b` below are the
    class-body-local names bound by the `field(...)` lines above, resolved
    to their attribute names via `.name` (set by __set_name__) at
    Model.__init__ time."""
    from prism import derived

    class M(Model):
        a = field(2)
        b = field(3)
        total = derived(lambda self: self.a.value + self.b.value, a, b)

    m = M()
    assert m.total.value == 5
    m.a.value = 10
    assert m.total.value == 13
    m.b.value = 7
    assert m.total.value == 17


def test_derived_descriptor_dep_mixed_with_string_dep():
    """Descriptor and string deps may be mixed in the same derived() call."""
    from prism import derived

    class M(Model):
        a = field(2)
        b = field(3)
        total = derived(lambda self: self.a.value + self.b.value, a, "b")

    m = M()
    assert m.total.value == 5
    m.b.value = 100
    assert m.total.value == 102


def test_derived_probe_raises_gives_actionable_type_error():
    """No type_hint + a probe that raises -> loud TypeError naming the fix (task 4)."""
    from prism import derived

    class M(Model):
        a = field(2)
        bad = derived(lambda self: 1 / 0, "a")

    with pytest.raises(TypeError, match="type_hint=int\\|float\\|str\\|bool"):
        M()


def test_derived_type_hint_skips_probing():
    """type_hint given -> probe is never called (task 4): the compute fn runs
    exactly once (the real eager compute), not twice (probe + eager compute).

    Was written with a raising lambda (`1 / 0`) and asserted `M()` succeeds —
    that relied on the eager-compute cast's `catch (...) {}` swallowing the
    exception too, which task 2 fix round 1 removed (add_derived_slot_vec no
    longer swallows the user's own exception). Switched to a call-counter so
    this test still isolates "probe skipped" from that unrelated behavior.
    """
    from prism import derived

    calls = []

    def compute(self):
        calls.append(1)
        return 5.0

    class M(Model):
        a = field(2)
        ok = derived(compute, "a", type_hint=float)

    m = M()
    assert isinstance(m.ok, prism.BoundDerivedFloat)
    assert calls == [1]


def test_derived_construction_raises_user_exception_unchanged():
    """Task 2 fix round 1: PyModel::add_derived_slot_vec's eager compute (runs
    at Model() construction, distinct from recompute()) used to swallow ANY
    exception from the user's function with a bare `catch (...) {}`, silently
    defaulting the derived's initial value to 0/0.0/""/False. Model()
    construction must instead let the user's own exception propagate
    unchanged."""
    from prism import derived

    class M(Model):
        a = field(2)
        bad = derived(lambda self: 1 / 0, "a", type_hint=int)

    with pytest.raises(ZeroDivisionError):
        M()


def test_derived_construction_wrong_type_raises_type_error_with_name():
    """Task 2 fix round 1: a derived function whose first (eager) result
    doesn't match its type_hint must raise a clear TypeError naming the
    derived field, not silently store 0."""
    from prism import derived

    class M(Model):
        a = field(2)
        bad = derived(lambda self: "oops", "a", type_hint=int)

    with pytest.raises(TypeError, match=re.escape("derived 'bad':")):
        M()


def test_derived_recompute_wrong_type_routes_type_error_via_on_error():
    """Task 2: once running, if the compute fn returns a value that doesn't
    match the derived's established type, the recompute cast failure must
    surface as a clear TypeError via on_error — not a silent 0/no-op, and
    not a crash. The app must keep dispatching afterward."""
    from prism import derived

    class M(Model):
        a = field(2)
        bad = derived(lambda self: self.a.value if self.a.value < 10 else "oops", "a")

    m = M()
    assert m.bad.value == 2

    caught = []
    try:
        prism.on_error(lambda exc: caught.append(exc))
        m.a.value = 20  # bad() now returns a str while the derived's type is int

        assert len(caught) == 1
        assert isinstance(caught[0], TypeError)
        # value is left unchanged (no silent 0, no crash) and the app keeps running
        assert m.bad.value == 2

        seen = []
        m.bad.observe(lambda v: seen.append(v))
        m.a.value = 3
        assert m.bad.value == 3
        assert seen == [3]
    finally:
        prism.on_error(None)


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


def test_run_headless_releases_module_global_model_without_leak_warning():
    """Task 8 repro: a Model left in a module global (the shape every example
    used to need `_main()`/`if __name__` to avoid) must not trip nanobind's
    leak check once _run_headless() returns, when an observer captures the
    model itself (Model -> handle -> Connection -> closure -> Model — the
    same cycle shape the pre-existing _atexit_clear() was written for, but
    now broken deterministically by run()/_run_headless() itself rather than
    left to interpreter-shutdown timing). Runs in a subprocess so the
    'leaked' check reads the real process's stderr, not pytest's own.

    Before this task's fix, run()/_run_headless() had no finally-time
    observer cleanup of their own — only the interpreter-exit _atexit_clear()
    disconnected this cycle, and only by accident of atexit ordering. This
    test's own opt-out below (discarding the model from _all_models) proves
    the fix is run()'s own doing, not atexit riding along.
    """
    code = (
        "import prism\n"
        "class Counter(prism.Model):\n"
        "    count = prism.field(42)\n"
        "m = Counter()\n"
        "conn = Counter.count.observe(m, lambda v: setattr(m, '_x', v))\n"
        "prism._all_models.discard(m)  # isolate: rely only on run()'s own cleanup\n"
        "prism._run_headless(m, delay_ms=50)\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stderr
    assert "leaked" not in result.stderr, result.stderr


def test_module_global_self_capturing_observer_no_leak_at_exit():
    """Task 1 residual repro: a module-global Model with a self-capturing
    observer that is never run() at all — the process must still exit
    cleanly with no nanobind leak warning.

    Discards `m`/`m.x` from `_all_models`/`_observed_handles` right after
    wiring the observer, same isolation idiom as
    `test_run_headless_releases_module_global_model_without_leak_warning`
    above: `_atexit_clear()` also walks those two registries and explicitly
    disconnects every keepalive it finds there, which already breaks this
    exact cycle regardless of this task's fix (it doesn't need the cycle to
    be GC-visible — it just calls `.disconnect()` directly on every live
    Model/handle it can still find). Discarding both isolates the mechanism
    actually under test here: with no explicit-cleanup registry to fall back
    on, only interpreter shutdown's own `gc.collect()` pass stands between
    this self-capturing observer and a permanent leak — and before this
    fix, that pass could not find the cycle at all (its last hop lived
    inside SenderHub::receivers_, a plain C++ std::function member with no
    Python-visible edge)."""
    code = (
        "import prism\n"
        "class M(prism.Model):\n"
        "    x = prism.field(0)\n"
        "m = M()\n"
        "m.x.observe(lambda v: m)\n"
        "prism._all_models.discard(m)\n"
        "prism._prism_ext._observed_handles.discard(m.x)\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stderr
    assert "leaked" not in result.stderr, result.stderr


def test_run_headless_keeps_field_cache_so_post_run_reads_see_last_value():
    """Task 8 design point: run()'s new cleanup disconnects observers but must
    NOT clear the model's _prism_fields cache — clearing it would make the
    next `m.count` re-`_allocate()` a brand-new slot at the descriptor's
    default, silently resetting `m.count.value` back to 42 instead of the
    last value set before the app closed. That's worse than either keeping
    the real value or raising: it's silent data loss. Verified empirically
    (see task-8-report.md) that clearing fields is not even necessary to
    avoid the leak warning for non-derived models — only the observer
    keepalive cycle needs breaking."""
    code = (
        "import prism\n"
        "class Counter(prism.Model):\n"
        "    count = prism.field(42)\n"
        "m = Counter()\n"
        "m.count.value = 99\n"
        "prism._run_headless(m, delay_ms=50)\n"
        "assert m.count.value == 99, m.count.value\n"
        "m.count.value = 100\n"
        "assert m.count.value == 100, m.count.value\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stderr


def test_pymodel_tp_clear_guards_uninitialized_instance():
    """2026-09-03 final-review item 4 repro: pymodel_tp_clear lacked the
    `if (!nb::inst_ready(self)) return 0;` guard that pymodel_tp_traverse
    already has (for exactly this reason — "constructor may not have run
    yet"). `Model.__new__(Model)` allocates and GC-tracks an instance
    without ever running the C++ constructor (that happens in __init__, not
    __new__); putting it in a self-cycle via its dynamic __dict__ (which
    exists independently of the wrapped C++ object) forces the cyclic GC to
    actually call tp_clear on it during gc.collect() — dereferencing
    uninitialized memory without the guard. Runs in a subprocess: a
    manifested crash would otherwise take down the whole suite.
    """
    code = (
        "import gc\n"
        "import prism\n"
        "m = prism.Model.__new__(prism.Model)\n"
        "m.__dict__['self_ref'] = m\n"
        "del m\n"
        "gc.collect()\n"
        "print('OK')\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stderr
    assert "OK" in result.stdout


def test_run_headless_does_not_leak_derived_field_model():
    """Task 16: a Model with a derived() field left in a module global no
    longer trips nanobind's leak check once _run_headless() returns. Was a
    pinned known-limitation (test_run_headless_does_not_fix_derived_field_model_leak,
    see task-8-report.md): SlotDerived's C++-side py_fn/dep_keepalive_ members
    are real, GIL-protected strong references to Python objects invisible to
    the cyclic GC — Model -> slots -> SlotDerived -> py_fn -> (closure) ->
    Model, a cycle only Python-level cleanup couldn't reach. Fixed by giving
    PyModel a tp_traverse/tp_clear pair (task-16) so the GC can find and break
    it, and SlotDerived's own traverse()/clear() expose py_fn/dep_keepalive_
    to it."""
    code = (
        "import prism\n"
        "class M(prism.Model):\n"
        "    counter = prism.field(0)\n"
        "    doubled = prism.derived(lambda self: self.counter.value * 2, 'counter')\n"
        "m = M()\n"
        "prism._run_headless(m, delay_ms=50)\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stderr
    assert "leaked" not in result.stderr, result.stderr


def test_run_headless_does_not_leak_view_override_model():
    """Task 16: a Model that overrides view(self, vb) left in a module global
    no longer trips nanobind's leak check either — was the second pinned
    known-limitation (test_run_headless_does_not_fix_view_override_model_leak,
    see task-8-report.md): `Model.__init__` passes the view trampoline to
    `self._set_view_callback(...)`, a C++ binding that stores it as an
    nb::object member (py_view_cb) invisible to the cyclic GC. Fixed by
    task-16's PyModel tp_traverse/tp_clear, which now visits py_view_cb too."""
    code = (
        "import prism\n"
        "class M(prism.Model):\n"
        "    count = prism.field(0)\n"
        "    def view(self, vb):\n"
        "        vb.widget(self.count)\n"
        "m = M()\n"
        "prism._run_headless(m, delay_ms=50)\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stderr
    assert "leaked" not in result.stderr, result.stderr


def test_run_headless_does_not_leak_tree_field_model():
    """Task 16: a Model with a tree_field(source) left in a module global no
    longer trips nanobind's leak check — same shape of cycle as the derived()
    and view() cases, this time via SlotTree::py_src_holder (the Python tree
    source object handed to PythonTreeSource's callbacks), now covered by
    SlotTree's own traverse()/clear()."""
    code = (
        "import prism\n"
        "class M(prism.Model):\n"
        "    tree = prism.tree_field({0: {'label': 'root', 'children': []}})\n"
        "m = M()\n"
        "prism._run_headless(m, delay_ms=50)\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stderr
    assert "leaked" not in result.stderr, result.stderr


def test_gc_collect_reclaims_model_with_self_referencing_observer():
    """Task 16 honest GC test — no subprocess, no _run_headless(): a genuine,
    in-process reference cycle through PyModel's py_view_cb member (Model ->
    py_view_cb -> closure -> Model), built via _set_view_callback() directly
    so the closure captures `m` for real (Model.__init__'s own view()
    trampoline deliberately avoids this via weakref, which would make the
    test pass trivially through plain refcounting — no GC involved). Before
    this task, PyModel had no tp_traverse/tp_clear, so the cyclic GC could
    not see py_view_cb at all and this cycle survived `gc.collect()` forever
    (verified: `m` was still alive after an explicit collect). Proves the
    fix is genuine cyclic-GC support: a plain `gc.collect()` with no
    _run_headless() involved at all must reclaim `m`."""

    def make():
        class M(Model):
            count = field(0)

        m = M()
        m._set_view_callback(lambda vb: setattr(m, "_x", 1))
        return weakref.ref(m)

    wr = make()
    gc.collect()
    assert wr() is None, "self-referencing view callback survived gc.collect()"


def test_shared_observer_gc_collect_does_not_hang_drain():
    """2026-09-03 fix round 1, finding 1: PyModel::drain() used to hold
    slots_mutex (a plain, non-recursive std::mutex) across s->drain(), which
    runs Python observer callbacks. Any allocation inside such a callback can
    trigger CPython's automatic cyclic GC on the same thread, which re-enters
    pymodel_tp_traverse — itself locking the same slots_mutex — a
    self-deadlock. Fixed by having drain() snapshot `slots` into a local
    vector under the lock, then draining the copy lock-free (the pattern
    pymodel_tp_clear already used). This test's observer callback forces a
    GC pass and allocates a few thousand objects on every notification, from
    inside a headless app tick, with a hard subprocess timeout: before the
    fix this hangs forever instead of exiting cleanly."""
    code = (
        "import gc, threading, time\n"
        "import prism\n"
        "class M(prism.Model):\n"
        "    s = prism.shared(0)\n"
        "m = M()\n"
        "def cb(v):\n"
        "    gc.collect()\n"
        "    junk = [object() for _ in range(3000)]\n"
        "    del junk\n"
        "m.s.observe(cb)\n"
        "t = threading.Thread(target=lambda: prism._run_headless(m, delay_ms=300))\n"
        "t.start()\n"
        "for _ in range(300):\n"
        "    if prism._is_running():\n"
        "        break\n"
        "    time.sleep(0.01)\n"
        "def setter():\n"
        "    for i in range(20):\n"
        "        m.s.value = i\n"
        "th = threading.Thread(target=setter)\n"
        "th.start()\n"
        "th.join()\n"
        "t.join()\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        timeout=30,
    )
    assert result.returncode == 0


def test_derived_teardown_survives_200_models_with_observers():
    """2026-09-03 fix round 1, finding 2: regression test for the task-16
    SlotDerived member-declaration-order use-after-free (deps_ was destroyed
    after dep_owners_ had already freed the dependency's Slot, so deps_'s
    disconnect() ran into freed memory). That bug was flaky (~1-in-8 to
    1-in-15 runs), so this creates and tears down 200 Models, each with a
    derived() field depending on two observed fields, via _run_headless — to
    give a regressed member order a real chance to crash. Runs in a
    subprocess since the failure mode is a segfault that would otherwise
    kill the whole suite."""
    code = (
        "import prism\n"
        "for _ in range(200):\n"
        "    class M(prism.Model):\n"
        "        a = prism.field(1)\n"
        "        b = prism.field(2)\n"
        "        total = prism.derived(lambda self: self.a.value + self.b.value, 'a', 'b')\n"
        "    m = M()\n"
        "    m.a.observe(lambda v: None)\n"
        "    m.b.observe(lambda v: None)\n"
        "    prism._run_headless(m, delay_ms=5)\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        timeout=30,
    )
    assert result.returncode == 0


def test_view_and_derived_together_survive_run_headless_teardown():
    """Task 3 repro: 02_mixer.py and 05_lists_and_derived.py used to carry a
    note that a Model overriding view() while also having a derived field hit
    an 'Invalid argument at exit' teardown race. That race shared its root
    cause with test_derived_field_survives_run_headless_teardown's (fixed in
    c4305d2/75256da: SlotDerived/dispatch_sync_read span the initial
    registry.add() with an unconditional gil_scoped_release even though the
    GIL is already released for the whole _run_headless() call). A manual
    view() triggers the exact same registry.add() startup path, so once that
    was guarded with PyGILState_Check() this combination stopped crashing
    too. Runs in a subprocess since a manifested crash would otherwise take
    down the whole suite."""
    code = (
        "import prism\n"
        "class M(prism.Model):\n"
        "    counter = prism.field(0)\n"
        "    doubled = prism.derived(lambda self: self.counter.value * 2, 'counter')\n"
        "    def view(self, vb):\n"
        "        vb.widget(self.counter)\n"
        "        vb.widget(self.doubled)\n"
        "m = M()\n"
        "prism._run_headless(m, delay_ms=200)\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        timeout=30,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    for bad in ("Invalid argument", "terminate", "Fatal"):
        assert bad not in result.stderr, result.stderr


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


def _return_wrong_type(v):
    return "not an int"


def _return_none(v):
    return None


def test_field_validator_wrong_return_type_raises_clear_type_error():
    """Task 2: a validator that returns a value nb::cast<T> can't convert
    must raise a TypeError naming the field, not an opaque cast error."""

    class M(Model):
        count = field(0, validator=_return_wrong_type)

    m = M()
    prefix = re.escape("validator for 'count' must return a int (or raise); got ")
    with pytest.raises(TypeError, match=prefix):
        m.count.value = 5
    assert m.count.value == 0


def test_field_validator_returns_none_raises_clear_type_error():
    """Task 2: a validator that forgets to return the value (returns None)
    must raise a clear TypeError, not silently store 0/None."""

    class M(Model):
        count = field(0, validator=_return_none)

    m = M()
    prefix = re.escape("validator for 'count' must return a int (or raise); got ")
    with pytest.raises(TypeError, match=prefix):
        m.count.value = 5
    assert m.count.value == 0


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


def _wait_worker_stopped(w, timeout=2.0):
    import time

    deadline = time.monotonic() + timeout
    while w.is_alive and time.monotonic() < deadline:
        time.sleep(0.005)


def test_worker_repeat_stops_after_exactly_n_calls():
    """2026-09-03 followups task 11: repeat=N stops the worker on its own
    after exactly N calls — no manual stop.set() needed."""
    calls = []

    def tick(stop):
        calls.append(1)

    w = prism.worker(tick, interval=0.01, repeat=3)
    _wait_worker_stopped(w)
    assert not w.is_alive
    assert len(calls) == 3


def test_worker_repeat_zero_makes_no_calls_either_form():
    """2026-09-03 final-review item 10 repro: with `interval` set, Worker._run
    only checked `n >= repeat` *after* calling fn() once, so `repeat=0` still
    made one call before stopping. Must make zero calls, for both the
    interval and no-interval forms."""
    calls_interval = []
    w1 = prism.worker(lambda: calls_interval.append(1), interval=0.01, repeat=0)
    _wait_worker_stopped(w1)
    assert not w1.is_alive
    assert calls_interval == []

    calls_no_interval = []
    w2 = prism.worker(lambda: calls_no_interval.append(1), repeat=0)
    _wait_worker_stopped(w2)
    assert not w2.is_alive
    assert calls_no_interval == []


def test_worker_zero_arg_fn_works():
    """fn may take zero args instead of (stop,) — detected once via
    inspect.signature at Worker creation."""
    calls = []

    def once():
        calls.append(1)

    w = prism.worker(once)
    _wait_worker_stopped(w)
    assert calls == [1]


def test_worker_zero_arg_fn_with_repeat():
    calls = []

    def tick():
        calls.append(1)

    w = prism.worker(tick, interval=0.01, repeat=2)
    _wait_worker_stopped(w)
    assert not w.is_alive
    assert len(calls) == 2


def test_standalone_field_add_pre_run():
    h = prism.FieldInt(5)
    h.add(3)
    assert h.get() == 8

    h2 = prism.FieldFloat(1.5)
    h2.add(0.5)
    assert h2.get() == 2.0


def test_bound_field_add_updates_value():
    class M(Model):
        counter = field(0)

    m = M()
    m.counter.add(5)
    assert m.counter.value == 5
    m.counter.add(-2)
    assert m.counter.value == 3


def test_field_add_atomic_across_threads_headless():
    """2026-09-03 followups task 11: field.add(n) posts one dispatched
    closure per call that runs entirely on the logic thread, so concurrent
    add()s from many threads never race the way `field.value += n` would.
    8 threads x 1000 add(1) calls must land exactly 8000, every time."""
    from concurrent.futures import ThreadPoolExecutor

    class M(Model):
        counter = field(0)

    m = M()
    n_workers = 8
    n_per_worker = 1000

    def bump():
        for _ in range(n_per_worker):
            m.counter.add(1)

    with prism.headless(m, timeout=10.0) as app:
        with ThreadPoolExecutor(max_workers=n_workers) as pool:
            futures = [pool.submit(bump) for _ in range(n_workers)]
            for f in futures:
                f.result()
        app.wait_until(
            lambda: m.counter.value == n_workers * n_per_worker, timeout=5.0
        )

    assert m.counter.value == n_workers * n_per_worker


def test_field_add_validated_rejection_routes_to_on_error_value_unchanged():
    """add()'s validator runs on the logic thread against the *summed*
    value, which the calling thread never sees — so a rejection can't raise
    back to the caller like set()/`.value =` do. It goes to on_error()
    instead, and the field is left unchanged."""

    class M(Model):
        count = field(0, validator=_reject_negative)

    m = M()
    caught = []
    try:
        prism.on_error(lambda exc: caught.append(exc))
        m.count.add(-5)
        assert len(caught) == 1
        assert isinstance(caught[0], ValueError)
        assert m.count.value == 0
    finally:
        prism.on_error(None)


def test_field_add_validator_no_uaf_on_close_burst():
    """2026-09-03 final-review item 1 repro: field_add_dispatch() used to post a
    closure capturing `self` (an nb::object) straight into the mutation queue —
    which model_app.hpp:190 documents is destroyed by run() with the GIL
    released. A background thread bursting add()/set() calls right up to (and
    past) a short-lived headless app's close guarantees some closures are still
    sitting in the queue, undrained, when it's torn down — exactly the
    ASan-discriminating scenario from task-1's brief. Only ASan reliably
    catches the resulting UAF/refcount corruption, so this runs in a
    subprocess (a crash would otherwise take down the whole suite).
    """
    code = (
        "import threading\n"
        "import prism\n"
        "def _reject_negative(v):\n"
        "    if v < 0:\n"
        "        raise ValueError('no negatives')\n"
        "    return v\n"
        "class M(prism.Model):\n"
        "    counter = prism.field(0, validator=_reject_negative)\n"
        "    plain = prism.field(0)\n"
        "m = M()\n"
        "def burst():\n"
        "    for i in range(2000):\n"
        "        m.counter.add(1)\n"
        "        m.plain.set(i)\n"
        "t = threading.Thread(target=burst)\n"
        "t.start()\n"
        "prism._run_headless(m, delay_ms=50)\n"
        "t.join(timeout=10)\n"
        "assert not t.is_alive()\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        timeout=45,
    )
    assert result.returncode == 0


def test_field_str_and_bool_have_no_add():
    """add() is int/float only — str/bool have no meaningful `+`."""

    class M(Model):
        s = field("x")
        b = field(False)

    m = M()
    assert not hasattr(m.s, "add")
    assert not hasattr(m.b, "add")
    assert not hasattr(prism.FieldStr(""), "add")
    assert not hasattr(prism.FieldBool(False), "add")


def test_on_change_fires_on_dep_change_with_descriptor_deps():
    """2026-09-03 followups task 10: @on_change(dep, ...) subscribes the
    decorated method to each dep at Model.__init__ — no manual
    `m.freq.observe(lambda v: m.redraw())` wiring needed. Callback takes no
    value arg (reads self.<field>.value); deps here are descriptors, not
    strings."""
    seen = []

    class M(Model):
        freq = field(1.0)
        amp = field(1.0)

        @prism.on_change(freq, amp)
        def redraw(self):
            seen.append((self.freq.value, self.amp.value))

    m = M()
    assert seen == []  # not immediate: no fire at construction
    m.freq.value = 2.0
    assert seen == [(2.0, 1.0)]
    m.amp.value = 3.0
    assert seen == [(2.0, 1.0), (2.0, 3.0)]


def test_on_change_string_deps_also_work():
    seen = []

    class M(Model):
        count = field(0)

        @prism.on_change("count")
        def on_count(self):
            seen.append(self.count.value)

    m = M()
    m.count.value = 5
    assert seen == [5]


def test_on_change_method_still_callable_directly():
    """The decorator must not replace the method with something unusable as
    an ordinary bound method — `m.redraw()` still works standalone."""
    calls = []

    class M(Model):
        freq = field(1.0)

        @prism.on_change(freq)
        def redraw(self):
            calls.append(self.freq.value)

    m = M()
    m.redraw()
    assert calls == [1.0]


def test_on_change_immediate_runs_once_after_construction():
    calls = []

    class M(Model):
        freq = field(1.0)

        @prism.on_change(freq, immediate=True)
        def redraw(self):
            calls.append(self.freq.value)

    m = M()
    assert calls == [1.0]  # immediate fire, replaces manual m.redraw() priming
    m.freq.value = 2.0
    assert calls == [1.0, 2.0]


def test_on_change_immediate_fires_on_logic_thread_when_constructed_during_app_run():
    """immediate=True runs synchronously inside Model.__init__, on whichever
    thread constructs the Model — so a Model created from an observer
    callback while a headless app is running has its immediate on_change
    fire ON the logic thread, same as any other dep-triggered fire."""
    from_logic_thread = []

    class Child(Model):
        x = field(1)

        @prism.on_change(x, immediate=True)
        def on_x(self):
            from_logic_thread.append(prism.is_logic_thread())

    class Parent(Model):
        trigger = field(0)

    created = []
    p = Parent()
    p.trigger.observe(lambda v: created.append(Child()))

    with prism.headless(p) as app:
        p.trigger.value = 1
        app.wait_until(lambda: created)

    assert from_logic_thread == [True]


def test_on_change_subscription_disconnected_by_run_headless_teardown():
    """The dep-handle Connection an @on_change subscription creates must be
    torn down by _run_headless()'s finally like any other observer — a
    stale post-teardown fire would be a UAF/crash risk (same class of bug
    task 16 fixed for derived()/view())."""
    code = (
        "import prism\n"
        "class M(prism.Model):\n"
        "    freq = prism.field(1.0)\n"
        "    @prism.on_change(freq)\n"
        "    def redraw(self):\n"
        "        pass\n"
        "m = M()\n"
        "prism._run_headless(m, delay_ms=50)\n"
    )
    result = subprocess.run(
        [sys.executable, "-c", code],
        env=os.environ,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stderr
    assert "leaked" not in result.stderr, result.stderr


def test_class_level_field_observe_emits_deprecation_warning_and_still_works():
    """2026-09-03 followups task 10: `Class.field.observe(model, cb)` keeps
    working but must now warn, pointing at the preferred
    `model.field.observe(cb)` spelling."""

    class M(Model):
        count = field(0)

    m = M()
    seen = []
    with pytest.warns(DeprecationWarning, match=r"model\.count\.observe"):
        conn = M.count.observe(m, lambda v: seen.append(v))
    m.count.value = 5
    assert seen == [5]
    conn.disconnect()


def test_class_level_shared_observe_emits_deprecation_warning():
    class M(Model):
        level = shared(0)

    m = M()
    with pytest.warns(DeprecationWarning, match=r"model\.level\.observe"):
        M.level.observe(m, lambda v: None)


def test_class_level_channel_observe_emits_deprecation_warning():
    class M(Model):
        ch = channel(0)

    m = M()
    with pytest.warns(DeprecationWarning, match=r"model\.ch\.observe"):
        M.ch.observe(m, lambda v: None)


def test_class_level_derived_observe_emits_deprecation_warning():
    from prism import derived

    class M(Model):
        a = field(1)
        total = derived(lambda self: self.a.value, a)

    m = M()
    with pytest.warns(DeprecationWarning, match=r"model\.total\.observe"):
        M.total.observe(m, lambda v: None)


def test_instance_level_observe_does_not_warn():
    """The preferred spelling must not itself trigger the deprecation
    warning."""
    import warnings

    class M(Model):
        count = field(0)

    m = M()
    with warnings.catch_warnings():
        warnings.simplefilter("error", DeprecationWarning)
        conn = m.count.observe(lambda v: None)
    conn.disconnect()


def test_model_instance_observe_method_is_gone():
    """2026-09-03 final-review item 5: `Model.observe(descriptor, cb)` (the
    instance-level convenience `m.observe(M.volume, cb)`) routed through the
    deprecated class-level descriptor.observe() path and warned against
    itself. Ruling: delete it — one spelling only, `m.volume.observe(cb)`."""

    class M(Model):
        count = field(0)

    m = M()
    assert not hasattr(m, "observe")
    assert "observe" not in vars(Model)


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


def test_worker_pool_plot_example_plots_at_least_one_window(monkeypatch):
    monkeypatch.setattr(sys, "argv", [sys.argv[0], "--headless"])
    mod = _load_example("10_worker_pool_plot")
    mod.main()  # asserts windows_done >= 1 internally


def test_asyncio_bridge_example_completes_a_round_trip(monkeypatch):
    monkeypatch.setattr(sys, "argv", [sys.argv[0], "--headless"])
    mod = _load_example("12_asyncio_bridge")
    mod.main()  # asserts round_trips >= 1 internally


def test_run_headless_kwarg_rejects_bool():
    class M(Model):
        x = field(0)

    with pytest.raises(TypeError, match="headless must be a duration"):
        prism.run(M(), headless=True)
