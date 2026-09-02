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

``delay_ms`` is a safety ceiling, not the convergence signal: the headless
backend sleeps for exactly ``delay_ms`` then fires WindowClose with no final
drain, and posts made after that close are dropped. There is no exposed
quit/close binding to end the app early once the counts converge, so this
example polls ``event_count``/``counter`` on the calling thread with a
bounded timeout instead of trusting the wall clock — if a loaded runner
hasn't converged before the timeout, it fails with an explicit "did not
converge" message rather than a silent off-by-N count.

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
HEADLESS_CEILING_MS = 5300
CONVERGE_TIMEOUT_S = 5.0
CONVERGE_POLL_S = 0.005


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

    t = threading.Thread(
        target=lambda: prism._run_headless(m, delay_ms=HEADLESS_CEILING_MS)
    )
    t.start()
    for _ in range(100):
        if prism._is_running():
            break
        time.sleep(0.01)

    with ThreadPoolExecutor(max_workers=N_WORKERS) as pool:
        futures = [pool.submit(stress_worker, m, n) for n in range(N_WORKERS)]
        for f in futures:
            f.result()

    target_events = N_WORKERS * N_CHANNEL_SENDS
    target_counter = N_WORKERS * N_INCREMENTS
    deadline = time.monotonic() + CONVERGE_TIMEOUT_S
    while (
        event_count[0] != target_events or m.counter.value != target_counter
    ) and time.monotonic() < deadline:
        time.sleep(CONVERGE_POLL_S)
    converged_at = time.monotonic()

    if event_count[0] != target_events or m.counter.value != target_counter:
        raise AssertionError(
            f"did not converge within {CONVERGE_TIMEOUT_S}s: "
            f"event_count={event_count[0]} (want {target_events}), "
            f"counter={m.counter.value} (want {target_counter})"
        )

    t.join()
    app_closed_at = time.monotonic()
    assert converged_at <= app_closed_at, "converged after the headless app already closed"

    assert event_count[0] == target_events, event_count[0]
    assert m.counter.value == target_counter, m.counter.value

    print(f"channel count = {event_count[0]}, counter = {m.counter.value}")
    if hasattr(sys, "_is_gil_enabled"):
        print(f"GIL enabled: {sys._is_gil_enabled()}")


if __name__ == "__main__":
    main()
