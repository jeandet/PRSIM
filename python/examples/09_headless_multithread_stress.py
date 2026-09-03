"""09_headless_multithread_stress.py — Headless multi-thread stress test / CI check.

Demonstrates:
  - Shared<T> and Channel<T> written concurrently from a thread pool
  - The cross-thread increment idiom: ``field.value += 1`` from N threads is
    a read-modify-write race (the read may see a stale cached value) — use
    the atomic ``field.add(n)`` instead. It posts one dispatched closure per
    call that runs ``get()`` + ``set()`` entirely on the logic thread, so
    concurrent ``add()``s from many threads can never race the way
    ``+=`` would. Channel<T> is still the right tool when every raw value
    (not just the running total) needs to arrive in order — see ``events``
    below.
  - prism.headless() driving the app with no display — this is what the CI
    ``3.14t`` (free-threaded) lane runs to prove the threading model holds
    with the GIL disabled. ``app.wait_until()`` is the convergence signal
    (not the wall clock): the block exits — closing the app, with a final
    drain so nothing posted right before is dropped — as soon as the counts
    match, or raises ``TimeoutError`` if a loaded runner never converges.

Run:
  PYTHONPATH=build/python python python/examples/09_headless_multithread_stress.py
"""

import sys
from concurrent.futures import ThreadPoolExecutor

import prism

N_WORKERS = 8
N_SHARED_SETS = 1000
N_CHANNEL_SENDS = 1000
N_INCREMENTS = 100
HEADLESS_TIMEOUT_S = 10.0
CONVERGE_TIMEOUT_S = 5.0


class StressModel(prism.Model):
    reading = prism.shared(0)
    events = prism.channel(0)
    counter = prism.field(0)
    doubled_counter = prism.derived(lambda self: self.counter.value * 2, counter)


def stress_worker(model: StressModel, n: int) -> None:
    for i in range(N_SHARED_SETS):
        model.reading.value = i
    for i in range(N_CHANNEL_SENDS):
        model.events.send(i)
    for _ in range(N_INCREMENTS):
        model.counter.add(1)


def main() -> None:
    m = StressModel()
    event_count = 0

    def on_event(v: int) -> None:
        nonlocal event_count
        event_count += 1

    m.events.observe(on_event)

    target_events = N_WORKERS * N_CHANNEL_SENDS
    target_counter = N_WORKERS * N_INCREMENTS

    with prism.headless(m, timeout=HEADLESS_TIMEOUT_S) as app:
        with ThreadPoolExecutor(max_workers=N_WORKERS) as pool:
            futures = [pool.submit(stress_worker, m, n) for n in range(N_WORKERS)]
            for f in futures:
                f.result()

        app.wait_until(
            lambda: event_count == target_events and m.counter.value == target_counter,
            timeout=CONVERGE_TIMEOUT_S,
        )

    assert event_count == target_events, event_count
    assert m.counter.value == target_counter, m.counter.value
    assert m.doubled_counter.value == target_counter * 2, m.doubled_counter.value

    print(f"channel count = {event_count}, counter = {m.counter.value}")
    if hasattr(sys, "_is_gil_enabled"):
        print(f"GIL enabled: {sys._is_gil_enabled()}")


if __name__ == "__main__":
    main()
