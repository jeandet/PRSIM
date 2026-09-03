"""08_dashboard.py — plot + tree + slider/checkbox controls in one model.

Demonstrates:
  - a Plot field and a Tree field composed in one Model via a custom view()
  - @prism.on_change(...) rebuilding the plot from slider + shared-ticker deps
  - a background prism.worker() auto-sweeping the ticker

Run:
  PYTHONPATH=builddir/python python3 python/examples/08_dashboard.py
"""

import math

import prism
from tree_sources import TREE_DATA, DictTreeSource


class Dashboard(prism.Model):
    plot = prism.plot_field()
    tree = prism.tree_field(DictTreeSource(TREE_DATA, roots=["Device"]))
    frequency = prism.slider(2.0, min=0.1, max=10.0)
    amplitude = prism.slider(1.0, min=0.1, max=3.0)
    auto_sweep = prism.checkbox(False, label="Auto sweep")
    status = prism.field("Dashboard ready")
    tick = prism.shared(0)

    def view(self, vb):
        vb.hstack(self.frequency, self.amplitude, self.auto_sweep)
        vb.widget(self.status)
        vb.canvas(self.plot)
        vb.tree(self.tree)

    @prism.on_change(frequency, amplitude, tick, immediate=True)
    def rebuild_plot(self):
        f, a, n = self.frequency.value, self.amplitude.value, 300
        xs = [i / (n - 1) * 8 * math.pi for i in range(n)]
        ys = [a * math.sin(f * t) + 0.3 * math.sin(3 * f * t) for t in xs]
        self.plot.replace_series(xs, ys, color="#7aa2f7", thickness=2.0)
        self.plot.set_labels(x="t", y="y")
        self.status.value = f"tick={self.tick.value} f={f:.2f} a={a:.2f}"


m = Dashboard()


def sweeper():
    if m.auto_sweep.value:
        m.tick.value = (m.tick.value + 1) % 1000
        m.frequency.value = 2.0 + 1.5 * math.sin(m.tick.value * 0.02)


prism.worker(sweeper, interval=0.05)
prism.run(m, title="Fancy Dashboard — Plot + Tree — Python")
