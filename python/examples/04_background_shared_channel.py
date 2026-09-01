"""04_background_shared_channel.py — Shared<T> + Channel<T> + threads.

Demonstrates cross-thread primitives (AGENTS.md taxonomy):
  - Shared<T>: latest-value, coalescing, any-thread set/get
  - Channel<T>: lossless ordered event stream, every send delivered
  - List field + observe_* for streamed data
  - any-thread mutation posted to logic thread (see doc/design/python-sdk.md §2)

Run:
  PYTHONPATH=build/python python python/examples/04_background_shared_channel.py
"""

import random
import threading
import time

import prism


class SensorBoard(prism.Model):
    # latest-value sensor reading (background thread overwrites, UI drains)
    temperature = prism.shared(20.0)
    label = prism.field("Sensor idle")
    events = prism.channel(0)          # lossless int event stream
    log = prism.list_field([])         # growing log, displayed via vb.list

    def view(self, vb):
        vb.vstack(self.temperature, self.label)
        vb.list(self.log)


def sensor_thread(model: SensorBoard, stop: threading.Event):
    n = 0
    while not stop.is_set():
        time.sleep(0.3)
        # Shared: latest value wins, intermediate writes coalesced
        model.temperature.value = 20.0 + random.uniform(-2, 8)
        # Channel: every send ordered and delivered
        model.events.send(n)
        n += 1


if __name__ == "__main__":
    m = SensorBoard()

    # observe Shared (fires on drain) and Channel (fires per send)
    conn_s = SensorBoard.temperature.observe(m, lambda v: print(f"[shared] temp={v:.2f}"))
    conn_c = SensorBoard.events.observe(m, lambda v: m.log.push(f"event {v}"))

    # also periodic UI update from observer
    def on_temp(v):
        m.label.value = f"Temp {v:.1f} °C — {m.log.size()} events"

    conn_label = SensorBoard.temperature.observe(m, on_temp)

    stop = threading.Event()
    t = threading.Thread(target=sensor_thread, args=(m, stop), daemon=True)
    # start before run to prove pre-run vs in-run paths (spec §2 pre-run direct)
    t.start()

    try:
        prism.run(m, title="Shared + Channel — Python")
    finally:
        stop.set()
        t.join(timeout=1)
