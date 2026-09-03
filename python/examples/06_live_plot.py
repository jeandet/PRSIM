"""06_live_plot.py — Fancy Plot example with live data.

Demonstrates:
  - prism.plot_field() + BoundPlot.replace_series/set_labels — one dispatched
    post each, instead of separate clear_series/add_series/notify calls
  - vb.canvas(plot) via ViewBuilder trampoline (C++ canvas escape hatch)
  - Live updates from sliders + background thread (any-thread set)
  - @prism.on_change(..., immediate=True) driving the plot rebuild
  - Derived stats

Run:
  PYTHONPATH=build/python python python/examples/06_live_plot.py
"""

import math
import time

import prism


class LivePlot(prism.Model):
    plot = prism.plot_field()
    frequency = prism.slider(2.0, min=0.1, max=10.0)
    amplitude = prism.slider(1.0, min=0.1, max=5.0)
    show_cos = prism.checkbox(False, label="Show cosine")
    status = prism.field("Ready")

    # derived: samples count
    title = prism.derived(
        lambda self: f"freq={self.frequency.value:.1f} amp={self.amplitude.value:.1f}",
        frequency,
        amplitude,
    )

    def view(self, vb):
        # canvas first (expands), then controls
        vb.canvas(self.plot)
        vb.hstack(self.frequency, self.amplitude, self.show_cos)
        vb.widget(self.status)
        vb.widget(self.title)

    @prism.on_change(frequency, amplitude, show_cos, immediate=True)
    def rebuild(self):
        f = self.frequency.value
        a = self.amplitude.value
        show_cos = self.show_cos.value
        N = 400
        xs = [i / (N - 1) * 4 * math.pi for i in range(N)]
        ys_sin = [a * math.sin(f * t) for t in xs]
        series = [(xs, ys_sin, "#0088cc")]
        if show_cos:
            ys_cos = [a * math.cos(f * t) for t in xs]
            series.append((xs, ys_cos, "#ff6b35"))
        self.plot.replace_series(series, thickness=2.0)
        self.plot.set_labels(x="Time (rad)", y="Amplitude")
        self.status.value = (
            f"plotted {N} pts f={f:.2f} a={a:.2f} cos={'on' if show_cos else 'off'}"
        )


m = LivePlot()

# background: simulate live sensor jitter via Shared-like periodic update
# (30 ticks at 0.5s). prism.worker() is stopped by run() on exit, so 'm' is
# captured directly, no weakref; repeat=30 stops the worker itself after
# the 30th tick, so jitter() needs neither the stop event nor a counter.
def jitter():
    m.frequency.value = 2.0 + 0.5 * math.sin(time.time())


prism.worker(jitter, interval=0.5, repeat=30)

prism.run(m, title="Live Plot — Python")
