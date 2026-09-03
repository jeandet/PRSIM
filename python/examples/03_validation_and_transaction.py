"""03_validation_and_transaction.py — Annotated validation + coalesced updates.

Shows:
  - typing.Annotated + pydantic Field via prism.validator_for
  - prism.field(..., validator=...) and transparent Annotated
  - prism.transaction() coalescing

Run:
  PYTHONPATH=build/python python python/examples/03_validation_and_transaction.py
"""

from typing import Annotated

from pydantic import Field as PField

import prism

Vol = Annotated[float, PField(ge=0, le=1)]
Pct = Annotated[int, PField(ge=0, le=100)]


class Settings(prism.Model):
    # explicit validator
    volume: Vol = 0.75  # transparent Annotated: auto-creates field + validator
    # if you prefer explicit:
    # volume = prism.field(0.75, validator=prism.validator_for(Vol))

    brightness: Pct = 60
    username = prism.field("jeandet")


m = Settings()
print(f"initial volume={m.volume.value} brightness={m.brightness.value}")

# validation: raises pydantic ValidationError
try:
    m.volume.value = 1.5
except Exception as e:
    print(f"validation rejected volume=1.5: {e}")

m.volume.value = 0.9
print(f"volume now {m.volume.value}")

# transaction: two sets coalesced into one publish / one observer fire
seen = []
m.volume.observe(lambda v: seen.append(v))
m.brightness.observe(lambda v: seen.append(v))

with prism.transaction():
    m.volume.value = 0.2
    m.brightness.value = 80
    # buffered: still old values inside
    assert m.volume.value == 0.9
    assert m.brightness.value == 60
    print("inside transaction (buffered)")

print(
    f"after transaction volume={m.volume.value} brightness={m.brightness.value} observed={seen}"
)

prism.run(m, title="Validation + Transaction — Python")
