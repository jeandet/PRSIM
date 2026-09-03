"""02_mixer.py — Sliders, checkbox, custom layout, observe.

Mirrors showcase_slider.cpp + README Mixer. Demonstrates:
  - prism.slider / prism.checkbox sentinels
  - manual view() with hstack/vstack (ViewBuilder trampoline)
  - observer + GIL-free background thread mutation
  - (for derived see 05_lists_and_derived.py)

Run:
  PYTHONPATH=build/python python python/examples/02_mixer.py
"""

import prism


class Mixer(prism.Model):
    # slider()/checkbox() are plain float/bool field() aliases today — the
    # min/max/label they accept aren't wired to a ranged/labeled widget yet.
    volume_slider = prism.slider(0.75, min=0.0, max=1.0)
    mute = prism.checkbox(False, label="Mute")
    count = prism.field(42)

    def view(self, vb):
        # explicit layout overrides auto-stack
        vb.hstack(self.volume_slider, self.mute)
        vb.widget(self.count)


m = Mixer()

# observe field
m.count.observe(lambda v: print(f"[observe] count={v}"))

# background thread mutates from any thread (posted to logic thread).
# prism.worker() is stopped by run() on exit, so 'm' can be captured
# directly — no weakref needed.
values = iter(range(50, 55))


def bump(stop):
    v = next(values, None)
    if v is None:
        stop.set()
        return
    print(f"[worker] setting count={v} (is_logic_thread={prism.is_logic_thread()})")
    m.count.value = v


prism.worker(bump, interval=1.0)

prism.run(m, title="Mixer — Python")
