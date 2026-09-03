"""09_headless_multithread_stress.py — 8-thread storm over shared/channel/field/derived.

Demonstrates:
  - Shared<T>/Channel<T> written concurrently from a thread pool
  - field.add(n): the atomic cross-thread increment (f.value += 1 off the
    logic thread is a read-modify-write race); Channel is still right when
    every message, not just the running total, must be seen
  - prism.headless() + app.wait_until() — this is the CI check the 3.14t
    free-threaded lane runs to prove the threading model holds GIL-disabled

Run:
  PYTHONPATH=builddir/python python3 python/examples/09_headless_multithread_stress.py
"""

import sys
from concurrent.futures import ThreadPoolExecutor

import prism

N_WORKERS, N_SETS, N_SENDS, N_INCR = 8, 1000, 1000, 100


class StressModel(prism.Model):
    reading = prism.shared(0)
    events = prism.channel(0)
    counter = prism.field(0)
    doubled = prism.derived(lambda self: self.counter.value * 2, counter)


def stress(model: StressModel) -> None:
    for i in range(N_SETS):
        model.reading.value = i
    for i in range(N_SENDS):
        model.events.send(i)
    for _ in range(N_INCR):
        model.counter.add(1)


def main() -> None:
    m = StressModel()
    seen = 0

    def on_event(_: int) -> None:
        nonlocal seen  # events.observe() runs on the logic thread only, no lock needed
        seen += 1

    m.events.observe(on_event)

    with prism.headless(m, timeout=10.0) as app:
        with ThreadPoolExecutor(max_workers=N_WORKERS) as pool:
            for f in [pool.submit(stress, m) for _ in range(N_WORKERS)]:
                f.result()
        app.wait_until(
            lambda: seen == N_WORKERS * N_SENDS and m.counter.value == N_WORKERS * N_INCR,
            timeout=5.0,
        )

    assert m.doubled.value == N_WORKERS * N_INCR * 2
    print(f"channel count = {seen}, counter = {m.counter.value}")
    if hasattr(sys, "_is_gil_enabled"):
        print(f"GIL enabled: {sys._is_gil_enabled()}")


if __name__ == "__main__":
    main()
