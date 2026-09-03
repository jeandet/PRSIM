"""Post-install smoke test run by cibuildwheel against the freshly built wheel (no display)."""

import prism


class M(prism.Model):
    x = prism.field(1)


m = M()
seen: list[int] = []
m.x.observe(seen.append)
with prism.headless(m, timeout=10.0) as app:
    m.x.value = 5
    app.wait_until(lambda: seen == [5])
print("wheel OK", seen)
