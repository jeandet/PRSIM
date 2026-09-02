"""08_dashboard.py — Fancy Dashboard combining Plot + Tree + Controls.

Combines primitives from 06 and 07 into a single multi-pane app:
  - Left: controls + status (sliders, checkbox)
  - Center: live Plot (canvas) driven by controls + background thread
  - Right: Tree browser (file structure) with detail panel

Layout: hstack([vstack(controls), canvas(plot), tree], with handles implicitly via Tree's internal hstack).
Simplified: we place plot and tree as siblings in a hstack for a two-panel effect;
PRISM's Tree internally already is hstack(list + handle + detail).

This mirrors examples/model_dashboard and model_system_monitor's multi-widget approach.

Run:
  PYTHONPATH=build/python python python/examples/08_dashboard.py
"""

import math

import prism
import importlib.util
import pathlib as _pathlib

# 07_file_tree has leading digit, so import via importlib
_spec = importlib.util.spec_from_file_location(
    "_07", _pathlib.Path(__file__).with_name("07_file_tree.py")
)
_07 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_07)  # type: ignore
DictTreeSource = _07.DictTreeSource
TREE_DATA = _07.TREE_DATA


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
        self.plot.clear_series()
        self.plot.add_series(xs, ys, color="#7aa2f7", thickness=2.0, fill=False)
        self.plot.notify()
        self.plot.x_label = "t"
        self.plot.y_label = "y"
        self.status.value = f"tick={self.tick.value} f={f:.2f} a={a:.2f}"


def _main():
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


if __name__ == "__main__":
    _main()
