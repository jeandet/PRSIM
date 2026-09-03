"""06_live_plot.py — Fancy Plot example with live data.

Demonstrates:
  - prism.plot_field() + BoundPlot.replace_series/set_labels — one dispatched
    post each, instead of separate clear_series/add_series/notify calls
  - vb.canvas(plot) via ViewBuilder trampoline (C++ canvas escape hatch)
  - Live updates from sliders + background thread (any-thread set)
  - Derived stats + transaction

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
        "frequency",
        "amplitude",
    )

    def view(self, vb):
        # canvas first (expands), then controls
        vb.canvas(self.plot)
        vb.hstack(self.frequency, self.amplitude, self.show_cos)
        vb.widget(self.status)
        vb.widget(self.title)

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
m.rebuild()

# Behavior: recompute on slider/checkbox change (on_change → logic thread)
m.frequency.observe(lambda v: m.rebuild())
m.amplitude.observe(lambda v: m.rebuild())
m.show_cos.observe(lambda v: m.rebuild())

# background: simulate live sensor jitter via Shared-like periodic update
# (30 ticks at 0.5s, same as the original range(30) loop). prism.worker()
# is stopped by run() on exit, so 'm' is captured directly, no weakref.
ticks_left = [30]


def jitter(stop):
    if ticks_left[0] <= 0:
        stop.set()
        return
    ticks_left[0] -= 1
    with prism.transaction():
        m.frequency.value = 2.0 + 0.5 * math.sin(time.time())


prism.worker(jitter, interval=0.5)

prism.run(m, title="Live Plot — Python")
