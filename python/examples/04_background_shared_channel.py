"""04_background_shared_channel.py — Shared<T> latest-value + Channel<T> ordered stream.

Demonstrates:
  - prism.shared(): any-thread latest-value slot, coalesced on drain
  - prism.channel(): any-thread lossless event stream, one observer fire per send
  - prism.list_field() + vb.list() growing a log from a channel observer
  - a background prism.worker() driving both from another thread

Run:
  PYTHONPATH=builddir/python python3 python/examples/04_background_shared_channel.py
"""

import itertools
import random

import prism


class SensorBoard(prism.Model):
    temperature = prism.shared(20.0)
    label = prism.field("Sensor idle")
    events = prism.channel(0)
    log = prism.list_field([])

    def view(self, vb):
        vb.vstack(self.temperature, self.label)
        vb.list(self.log)


m = SensorBoard()
event_ids = itertools.count()


def sensor_tick():
    m.temperature.value = 20.0 + random.uniform(-2, 8)
    m.events.send(next(event_ids))


def on_temp(v):
    m.label.value = f"Temp {v:.1f} °C — {m.log.size()} events"


m.events.observe(lambda v: m.log.push(f"event {v}"))
m.temperature.observe(on_temp)
prism.worker(sensor_tick, interval=0.3)
prism.run(m, title="Shared + Channel — Python")
