"""04_background_shared_channel.py — Shared<T> + Channel<T> + threads.

Demonstrates cross-thread primitives (AGENTS.md taxonomy):
  - Shared<T>: latest-value, coalescing, any-thread set/get
  - Channel<T>: lossless ordered event stream, every send delivered
  - List field + observe_* for streamed data
  - any-thread mutation posted to logic thread (see doc/design/python-sdk.md §2)

Run:
  PYTHONPATH=build/python python python/examples/04_background_shared_channel.py
"""

import itertools
import random

import prism


class SensorBoard(prism.Model):
    # latest-value sensor reading (background thread overwrites, UI drains)
    temperature = prism.shared(20.0)
    label = prism.field("Sensor idle")
    events = prism.channel(0)  # lossless int event stream
    log = prism.list_field([])  # growing log, displayed via vb.list

    def view(self, vb):
        vb.vstack(self.temperature, self.label)
        vb.list(self.log)


m = SensorBoard()
_event_ids = itertools.count()


def sensor_tick():
    # Shared: latest value wins, intermediate writes coalesced
    m.temperature.value = 20.0 + random.uniform(-2, 8)
    # Channel: every send ordered and delivered
    m.events.send(next(_event_ids))


# observe Shared (fires on drain) and Channel (fires per send)
m.temperature.observe(lambda v: print(f"[shared] temp={v:.2f}"))
m.events.observe(lambda v: m.log.push(f"event {v}"))


# also periodic UI update from observer
def on_temp(v):
    m.label.value = f"Temp {v:.1f} °C — {m.log.size()} events"


m.temperature.observe(on_temp)

# prism.worker() starts immediately, before run(), to prove pre-run vs
# in-run dispatch paths (spec §2 pre-run direct) — and is stopped by
# run()'s exit, so no manual stop event / join is needed here.
prism.worker(sensor_tick, interval=0.3)

prism.run(m, title="Shared + Channel — Python")
