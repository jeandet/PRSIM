"""01_counter.py — minimal PRISM model: two fields, auto-stacked view.

Demonstrates:
  - prism.field() descriptors with an auto-stacked view (no view() needed)
  - m.field.observe(cb), fire-and-forget
  - any-thread field write before prism.run()

Run:
  PYTHONPATH=builddir/python python3 python/examples/01_counter.py
"""

import prism


class Counter(prism.Model):
    count = prism.field(42)
    label = prism.field("Hello, PRISM!")


m = Counter()
m.count.observe(lambda v: print(f"count -> {v}"))
m.count.value = 43
prism.run(m, title="Counter — Python")
