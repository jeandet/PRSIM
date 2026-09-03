"""10_worker_pool_plot.py — ThreadPoolExecutor(4) computing FFT spectra into a live plot.

Demonstrates:
  - a prism.worker() producer submitting one FFT job per window to a
    ThreadPoolExecutor(max_workers=4)
  - a pure-Python radix-2 FFT (cmath, no numpy) computing a magnitude
    spectrum per window on the pool threads
  - each pool thread posting straight to the plot via plot.replace_series(),
    so concurrent posts never leave the plot showing a mix of two windows
  - a channel(int) tick counted by a single logic-thread observer into
    windows_done/status
  - no view(): the auto-stacked view shows windows_done next to status too
    (it used to be hidden behind a custom view()) — simpler than adding one
    back just to hide a field
  - prism.headless() for --headless / CI

Run:
  PYTHONPATH=builddir/python python3 python/examples/10_worker_pool_plot.py
  PYTHONPATH=builddir/python python3 python/examples/10_worker_pool_plot.py --headless
"""

import cmath
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
    return [math.sin(2 * math.pi * freq * i / WINDOW_SIZE + phase) for i in range(WINDOW_SIZE)]


class WorkerPoolPlot(prism.Model):
    plot = prism.plot_field()
    status = prism.field("starting")
    windows_done = prism.field(0)
    tick = prism.channel(0)


def _compute_and_plot(model: WorkerPoolPlot, window: list[float], bins: list[int]) -> None:
    ys = compute_spectrum(window)
    model.plot.replace_series(bins, ys, color="#0088cc", thickness=2.0)
    model.tick.send(1)


def main() -> None:
    m = WorkerPoolPlot()
    start = time.monotonic()
    bins = list(range(WINDOW_SIZE // 2))
    m.plot.set_labels(x="Frequency bin", y="Magnitude")

    def on_tick(_: int) -> None:
        m.windows_done.add(1)
        elapsed = time.monotonic() - start
        rate = m.windows_done.value / elapsed if elapsed > 0 else 0.0
        m.status.value = f"{m.windows_done.value} windows, {rate:.1f} windows/sec"

    m.tick.observe(on_tick)

    def producer(stop: threading.Event) -> None:
        phase = 0.0
        with ThreadPoolExecutor(max_workers=4) as pool:
            pending: list = []
            while not stop.is_set():
                pending = [f for f in pending if not f.done()]
                if len(pending) >= MAX_PENDING:
                    time.sleep(0.001)
                    continue
                pending.append(pool.submit(_compute_and_plot, m, _make_window(phase), bins))
                phase += 0.37

    prism.worker(producer)

    if "--headless" in sys.argv:
        with prism.headless(m, timeout=1.0) as app:
            app.wait_until(lambda: m.windows_done.value >= 1, timeout=1.0)
    else:
        prism.run(m, title="Worker Pool Plot — Python")

    print(f"status={m.status.value}")
    if hasattr(sys, "_is_gil_enabled"):
        print(f"GIL enabled: {sys._is_gil_enabled()}")
    if "--headless" in sys.argv:
        assert m.windows_done.value >= 1, "no window was plotted"


if __name__ == "__main__":
    main()
