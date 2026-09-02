"""09_headless_multithread_stress.py — Headless multi-thread stress test / CI check.

Demonstrates:
  - Shared<T> and Channel<T> written concurrently from a thread pool
  - The cross-thread increment idiom: ``field.value += 1`` from N threads is
    a read-modify-write race (the read may see a stale cached value), so
    counted increments go through a Channel instead — each worker sends a
    ``1``, and a logic-thread observer (single-threaded, so safe) does the
    actual ``+= 1``. Never read-modify-write a plain Field from a worker
    thread.
  - prism._run_headless() driving the app with no display — this is what
    the CI ``3.14t`` (free-threaded) lane runs to prove the threading model
    holds with the GIL disabled.

No ``derived`` field here: ``prism.derived(...)`` + ``prism._run_headless()``
segfaults at teardown even with zero threads involved (confirmed with a
2-line repro — a pre-existing bug, same family as the "view() + derived"
headless-teardown race documented in 02_mixer.py/05_lists_and_derived.py,
just triggered by headless + derived alone). Out of scope to fix here —
tracked as a known gotcha, not swept under the rug.

Run:
  PYTHONPATH=build/python python python/examples/09_headless_multithread_stress.py
"""

import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

import prism

N_WORKERS = 8
N_SHARED_SETS = 1000
N_CHANNEL_SENDS = 1000
N_INCREMENTS = 100


class StressModel(prism.Model):
    reading = prism.shared(0)
    events = prism.channel(0)
    incr = prism.channel(0)
    counter = prism.field(0)


def stress_worker(model: StressModel, n: int) -> None:
    for i in range(N_SHARED_SETS):
        model.reading.value = i
    for i in range(N_CHANNEL_SENDS):
        model.events.send(i)
    for _ in range(N_INCREMENTS):
        model.incr.send(1)


def main() -> None:
    m = StressModel()
    event_count = [0]
    StressModel.events.observe(m, lambda v: event_count.__setitem__(0, event_count[0] + 1))
    StressModel.incr.observe(m, lambda v: setattr(m.counter, "value", m.counter.value + 1))

    t = threading.Thread(target=lambda: prism._run_headless(m, delay_ms=300))
    t.start()
    for _ in range(100):
        if prism._is_running():
            break
        time.sleep(0.01)

    with ThreadPoolExecutor(max_workers=N_WORKERS) as pool:
        futures = [pool.submit(stress_worker, m, n) for n in range(N_WORKERS)]
        for f in futures:
            f.result()

    t.join()

    assert event_count[0] == N_WORKERS * N_CHANNEL_SENDS, event_count[0]
    assert m.counter.value == N_WORKERS * N_INCREMENTS, m.counter.value

    print(f"channel count = {event_count[0]}, counter = {m.counter.value}")
    if hasattr(sys, "_is_gil_enabled"):
        print(f"GIL enabled: {sys._is_gil_enabled()}")


if __name__ == "__main__":
    main()
