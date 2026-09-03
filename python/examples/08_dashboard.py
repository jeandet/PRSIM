"""08_dashboard.py — Fancy Dashboard combining Plot + Tree + Controls.

Demonstrates:
  - Plot + Tree + slider/checkbox controls composed in one Model
  - prism.worker(interval=...) driving an auto-sweep from a background thread
  - Shared<T> ticker observed to trigger a plot rebuild

Layout is plot then tree stacked vertically for simplicity — Tree's own
internal hstack already handles its list/detail split. This mirrors
examples/model_dashboard and model_system_monitor's multi-widget approach.

Run:
  PYTHONPATH=build/python python python/examples/08_dashboard.py
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
    # shared state updated by background thread (coalescing)
    tick = prism.shared(0)

    def view(self, vb):
        # Top row: controls
        vb.hstack(self.frequency, self.amplitude, self.auto_sweep)
        vb.widget(self.status)
        # Main area: plot + tree side-by-side
        # Tree's own hstack will handle its list/detail split; here we just stack plot and tree vertically for simplicity
        # To get a true horizontal split, put them in an hstack with a handle — we emulate via vstack of canvas then tree
        # For a more ambitious split-pane, uncomment the hstack version below:
        # vb.hstack(lambda: (vb.canvas(self.plot), vb.tree(self.tree)))
        vb.canvas(self.plot)
        vb.tree(self.tree)

    def rebuild_plot(self):
        f = self.frequency.value
        a = self.amplitude.value
        N = 300
        xs = [i / (N - 1) * 8 * math.pi for i in range(N)]
        ys = [a * math.sin(f * t) + 0.3 * math.sin(3 * f * t) for t in xs]
        self.plot.replace_series(xs, ys, color="#7aa2f7", thickness=2.0, fill=False)
        self.plot.set_labels(x="t", y="y")
        self.status.value = f"tick={self.tick.value} f={f:.2f} a={a:.2f}"


m = Dashboard()
m.rebuild_plot()

# Behavior wiring
m.frequency.observe(lambda v: m.rebuild_plot())
m.amplitude.observe(lambda v: m.rebuild_plot())
m.tick.observe(lambda v: m.rebuild_plot() if m.auto_sweep.value else None)


# Auto-sweep worker: stopped by run() on exit, so 'm' is captured
# directly, no weakref needed.
def sweeper(stop):
    if m.auto_sweep.value:
        # Shared: latest value wins, coalesced
        m.tick.value = (m.tick.value + 1) % 1000
        # nudge frequency slightly
        m.frequency.value = 2.0 + 1.5 * math.sin(m.tick.value * 0.02)


prism.worker(sweeper, interval=0.05)

prism.run(m, title="Fancy Dashboard — Plot + Tree — Python")
