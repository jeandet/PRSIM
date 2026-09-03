"""01_counter.py — Minimal PRISM Python example.

Mirrors examples/showcase/showcase_counter.cpp (two Fields) and the
README hello-world. Uses auto-stacked view (no def view needed).

Run:
  PYTHONPATH=build/python python python/examples/01_counter.py
Requires a display (SDL). For headless smoke test:
  PYTHONPATH=build/python python -c "import prism; m=Counter(); prism._run_headless(m)"
"""

import prism


class Counter(prism.Model):
    count = prism.field(42)
    label = prism.field("Hello, PRISM!")


m = Counter()

# observe: fire-and-forget, no local reference needed
m.count.observe(lambda v: print(f"count -> {v}"))

# any thread may mutate; here main thread before run
m.count.value = 43

# blocking window; releases GIL around SDL pump
prism.run(m, title="Counter — Python")
