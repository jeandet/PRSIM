"""12_asyncio_bridge.py — asyncio event loop bridged into a prism.worker.

Demonstrates:
  - an asyncio loop pumped from a single prism.worker() and no raw thread:
    fn(stop) repeatedly runs the loop for one short slice via
    loop.run_until_complete(asyncio.sleep(...)) until stop is set
  - an observer (logic thread) calling asyncio.run_coroutine_threadsafe()
    to schedule work on that loop from a field change
  - the coroutine feeding its result back through a prism.channel — the
    only thread-safe way into the model from the loop's own thread
  - prism.headless() for --headless / CI

Run:
  PYTHONPATH=builddir/python python3 python/examples/12_asyncio_bridge.py
  PYTHONPATH=builddir/python python3 python/examples/12_asyncio_bridge.py --headless
"""

import asyncio
import sys
import threading

import prism


class AsyncioBridge(prism.Model):
    trigger = prism.field(0)
    results = prism.channel(0.0)
    last_result = prism.field(0.0)
    round_trips = prism.field(0)
    status = prism.field("starting")


async def _fetch(model: AsyncioBridge, request: int) -> None:
    await asyncio.sleep(0.01)
    model.results.send(float(request) * 1.5)


def _shutdown(loop: asyncio.AbstractEventLoop) -> None:
    pending = asyncio.all_tasks(loop)
    for task in pending:
        task.cancel()
    try:
        if pending:
            loop.run_until_complete(
                asyncio.wait_for(asyncio.gather(*pending, return_exceptions=True), timeout=2.0)
            )
    except asyncio.TimeoutError:
        print("12_asyncio_bridge: timed out waiting for task cancellation", file=sys.stderr)
    finally:
        loop.close()


def main() -> None:
    m = AsyncioBridge()
    loop = asyncio.new_event_loop()

    def pump_loop(stop: threading.Event) -> None:
        while not stop.is_set():
            loop.run_until_complete(asyncio.sleep(0.01))
        _shutdown(loop)

    prism.worker(pump_loop)

    m.trigger.observe(lambda v: asyncio.run_coroutine_threadsafe(_fetch(m, v), loop))

    def on_result(v: float) -> None:
        m.last_result.value = v
        m.round_trips.add(1)
        m.status.value = f"round trip {m.round_trips.value}: {v:.1f}"

    m.results.observe(on_result)

    ticks = 0

    def ticker() -> None:
        nonlocal ticks
        ticks += 1
        m.trigger.value = ticks

    prism.worker(ticker, interval=0.05)

    if "--headless" in sys.argv:
        with prism.headless(m, timeout=1.0) as app:
            app.wait_until(lambda: m.round_trips.value >= 1, timeout=1.0)
    else:
        prism.run(m, title="Asyncio Bridge — Python")

    print(f"status={m.status.value} round_trips={m.round_trips.value}")
    if "--headless" in sys.argv:
        assert m.round_trips.value >= 1, "no asyncio round trip completed"


if __name__ == "__main__":
    main()
