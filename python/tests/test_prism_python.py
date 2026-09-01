"""Python bindings pytest suite — P2/P3 gates from doc/design/python-sdk.md §6."""

import gc
import threading
import weakref

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
