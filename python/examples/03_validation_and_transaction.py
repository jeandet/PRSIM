"""03_validation_and_transaction.py — Annotated validation + transaction() coalescing.

Demonstrates:
  - typing.Annotated + pydantic validators, auto-wired by Model.__init_subclass__
  - prism.field(..., validator=...) for the explicit form
  - prism.transaction() buffering writes into one publish

Run:
  PYTHONPATH=builddir/python python3 python/examples/03_validation_and_transaction.py
"""

from typing import Annotated

from pydantic import Field as PField

import prism

Vol = Annotated[float, PField(ge=0, le=1)]
Pct = Annotated[int, PField(ge=0, le=100)]


class Settings(prism.Model):
    volume: Vol = 0.75
    brightness: Pct = 60
    username = prism.field("jeandet")


m = Settings()
print(f"initial volume={m.volume.value} brightness={m.brightness.value}")

try:
    m.volume.value = 1.5
except Exception as e:
    print(f"validation rejected volume=1.5: {e}")

seen = []
m.volume.observe(lambda v: seen.append(v))
m.brightness.observe(lambda v: seen.append(v))

with prism.transaction():
    m.volume.value = 0.2
    m.brightness.value = 80
    assert m.volume.value == 0.75  # buffered until the block exits

print(
    f"after transaction volume={m.volume.value} brightness={m.brightness.value} observed={seen}"
)

prism.run(m, title="Validation + Transaction — Python")
