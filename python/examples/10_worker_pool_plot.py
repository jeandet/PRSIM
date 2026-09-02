"""10_worker_pool_plot.py — ThreadPoolExecutor(4) computing spectra into a live plot.

Demonstrates:
  - A producer prism.worker() submitting one FFT job per window to a
    ThreadPoolExecutor(max_workers=4) — on a free-threaded (3.14t) build
    those 4 workers run truly in parallel, which is what pushes
    windows/sec up; on a GIL build they still overlap I/O-free CPU work
    less, but the demo runs the same way either way.
  - A pure-Python radix-2 FFT (cmath, no numpy) computing a magnitude
    spectrum per window on the pool threads.
  - Each spectrum crossing back to the logic thread through a
    prism.channel(str) as a JSON string. Two reasons: channels only carry
    scalars (int/float/str/bool), not a list; and plot.clear_series/
    add_series/notify are each a separate thread-dispatched post, so
    calling them straight from 4 pool threads would interleave between
    windows — routing through the channel's single logic-thread observer
    applies all three atomically per window.
  - The channel observer (logic thread, single-threaded) redraws the plot
    and updates a "N windows, R windows/sec" status field. A
    `replace_series(xs, ys)` primitive that did clear+add+notify in one
    post would make the JSON hop unnecessary — not adding it here.
  - prism._run_headless() for --headless / CI: runs for 1s then asserts
    at least one window was plotted.

Run:
  PYTHONPATH=build/python python python/examples/10_worker_pool_plot.py
  PYTHONPATH=build/python python python/examples/10_worker_pool_plot.py --headless
"""

import cmath
import json
import math
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

import prism

WINDOW_SIZE = 256
MAX_PENDING = 8


def _fft(samples: list[complex]) -> list[complex]:
    n = len(samples)
    if n <= 1:
        return samples
    even = _fft(samples[0::2])
    odd = _fft(samples[1::2])
    half = n // 2
    result: list[complex] = [0j] * n
    for k in range(half):
        twiddle = cmath.exp(-2j * math.pi * k / n) * odd[k]
        result[k] = even[k] + twiddle
        result[k + half] = even[k] - twiddle
    return result


def compute_spectrum(window: list[float]) -> list[float]:
    coeffs = _fft([complex(x) for x in window])
    n = len(window)
    return [abs(c) * 2.0 / n for c in coeffs[: n // 2]]


def _make_window(phase: float) -> list[float]:
    freq = 5.0 + 3.0 * math.sin(phase * 0.05)
    return [
        math.sin(2 * math.pi * freq * i / WINDOW_SIZE + phase) for i in range(WINDOW_SIZE)
    ]


class WorkerPoolPlot(prism.Model):
    plot = prism.plot_field()
    status = prism.field("starting")
    windows_done = prism.field(0)
    spectra = prism.channel("")

    def view(self, vb):
        vb.canvas(self.plot)
        vb.widget(self.status)


def _compute_and_send(model: WorkerPoolPlot, window: list[float]) -> None:
    model.spectra.send(json.dumps(compute_spectrum(window)))


def main(headless: bool = False) -> None:
    m = WorkerPoolPlot()
    start = time.monotonic()
    bins = list(range(WINDOW_SIZE // 2))

    def on_spectrum(payload: str) -> None:
        ys = json.loads(payload)
        m.windows_done.value += 1
        elapsed = time.monotonic() - start
        rate = m.windows_done.value / elapsed if elapsed > 0 else 0.0
        m.plot.clear_series()
        m.plot.add_series(bins, ys, color="#0088cc", thickness=2.0)
        m.plot.notify()
        m.status.value = f"{m.windows_done.value} windows, {rate:.1f} windows/sec"

    WorkerPoolPlot.spectra.observe(m, on_spectrum)

    def producer(stop: threading.Event) -> None:
        phase = 0.0
        with ThreadPoolExecutor(max_workers=4) as pool:
            pending = []
            while not stop.is_set():
                pending = [f for f in pending if not f.done()]
                if len(pending) >= MAX_PENDING:
                    time.sleep(0.001)
                    continue
                pending.append(pool.submit(_compute_and_send, m, _make_window(phase)))
                phase += 0.37

    prism.worker(producer)

    if headless or "--headless" in sys.argv[1:]:
        prism._run_headless(m, delay_ms=1000)
    else:
        prism.run(m, title="Worker Pool Plot — Python")

    print(f"status={m.status.value}")
    if hasattr(sys, "_is_gil_enabled"):
        print(f"GIL enabled: {sys._is_gil_enabled()}")

    if headless or "--headless" in sys.argv[1:]:
        assert m.windows_done.value >= 1, "no window was plotted"
        print(f"windows_done={m.windows_done.value}")


if __name__ == "__main__":
    main()
