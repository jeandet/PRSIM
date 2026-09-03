"""02_mixer.py — sliders, checkbox, custom layout.

Demonstrates:
  - prism.slider()/prism.checkbox() descriptors
  - manual view() with vb.hstack/vb.widget (overrides auto-stack)
  - m.field.observe(cb) and a background prism.worker() mutating a field

Run:
  PYTHONPATH=builddir/python python3 python/examples/02_mixer.py
"""

import prism


class Mixer(prism.Model):
    volume_slider = prism.slider(0.75, min=0.0, max=1.0)
    mute = prism.checkbox(False, label="Mute")
    count = prism.field(42)

    def view(self, vb):
        vb.hstack(self.volume_slider, self.mute)
        vb.widget(self.count)


m = Mixer()
m.count.observe(lambda v: print(f"[observe] count={v}"))
prism.worker(lambda: m.count.add(1), interval=1.0, repeat=5)
prism.run(m, title="Mixer — Python")
