"""06_live_plot.py — Fancy Plot example with live data.

Demonstrates:
  - prism.plot_field() + BoundPlot.add_series/clear_series/notify
  - vb.canvas(plot) via ViewBuilder trampoline (C++ canvas escape hatch)
  - Live updates from sliders + background thread (any-thread set)
  - Derived stats + transaction

Run:
  PYTHONPATH=build/python python python/examples/06_live_plot.py
"""

import math
import threading
import time

import prism


class LivePlot(prism.Model):
    plot = prism.plot_field()
    frequency = prism.slider(2.0, min=0.1, max=10.0)
    amplitude = prism.slider(1.0, min=0.1, max=5.0)
    show_cos = prism.checkbox(False, label="Show cosine")
    status = prism.field("Ready")

    # derived: samples count
    title = prism.derived(lambda self: f"freq={self.frequency.value:.1f} amp={self.amplitude.value:.1f}", "frequency", "amplitude")

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
        # clear and add — must notify afterwards
        self.plot.clear_series()
        self.plot.add_series(xs, ys_sin, color="#0088cc", thickness=2.0)
        if show_cos:
            ys_cos = [a * math.cos(f * t) for t in xs]
            self.plot.add_series(xs, ys_cos, color="#ff6b35", thickness=2.0)
        self.plot.notify()
        # viewport labels
        self.plot.x_label = "Time (rad)"
        self.plot.y_label = "Amplitude"
        self.status.value = f"plotted {N} pts f={f:.2f} a={a:.2f} cos={'on' if show_cos else 'off'}"


if __name__ == "__main__":
    m = LivePlot()
    m.rebuild()

    # Behavior: recompute on slider/checkbox change (on_change → logic thread)
    m.frequency.observe(lambda v: m.rebuild())
    m.amplitude.observe(lambda v: m.rebuild())
    m.show_cos.observe(lambda v: m.rebuild())

    # background: simulate live sensor jitter via Shared-like periodic update
    # here we just nudge frequency from another thread to show any-thread posting
    def jitter():
        for i in range(30):
            time.sleep(0.5)
            # any-thread mutation is posted to logic thread queue
            with prism.transaction():
                # small auto-sweep
                m.frequency.value = 2.0 + 0.5 * math.sin(time.time())
            # rebuild will happen via observer

    t = threading.Thread(target=jitter, daemon=True)
    t.start()

    prism.run(m, title="Live Plot — Python")
