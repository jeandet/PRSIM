"""06_live_plot.py — live plot driven by sliders and a background worker.

Demonstrates:
  - prism.plot_field() + vb.canvas() (the C++ canvas escape hatch)
  - plot.replace_series()/set_labels() — one dispatched post each
  - @prism.on_change(..., immediate=True) rebuilding the plot from deps
  - a background prism.worker() jittering a slider

Run:
  PYTHONPATH=builddir/python python3 python/examples/06_live_plot.py
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
    caption = prism.derived(
        lambda self: f"freq={self.frequency.value:.1f} amp={self.amplitude.value:.1f}",
        frequency,
        amplitude,
    )

    def view(self, vb):
        vb.canvas(self.plot)
        vb.hstack(self.frequency, self.amplitude, self.show_cos)
        vb.widget(self.status)
        vb.widget(self.caption)

    @prism.on_change(frequency, amplitude, show_cos, immediate=True)
    def redraw(self):
        f, a, n = self.frequency.value, self.amplitude.value, 400
        xs = [i / (n - 1) * 4 * math.pi for i in range(n)]
        series = [(xs, [a * math.sin(f * t) for t in xs], "#0088cc")]
        if self.show_cos.value:
            series.append((xs, [a * math.cos(f * t) for t in xs], "#ff6b35"))
        self.plot.replace_series(series, thickness=2.0)
        self.plot.set_labels(x="Time (rad)", y="Amplitude")
        self.status.value = f"plotted {n} pts f={f:.2f} a={a:.2f}"


m = LivePlot()


def jitter():
    m.frequency.value = 2.0 + 0.5 * math.sin(time.time())


prism.worker(jitter, interval=0.5, repeat=30)
prism.run(m, title="Live Plot — Python")
