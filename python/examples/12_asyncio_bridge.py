"""12_asyncio_bridge.py — asyncio event loop bridged into a prism.worker.

Demonstrates:
  - An asyncio event loop running for the app's lifetime inside a single
    prism.worker() (no interval — the fn blocks in loop.run_forever()).
  - An observer (logic thread) calling asyncio.run_coroutine_threadsafe()
    to schedule work on that loop from a plain field change.
  - The coroutine doing async work then feeding the result back through a
    prism.channel — the only thread-safe way into the model from the loop
    thread.
  - Clean shutdown: a watcher thread waits on the prism.worker stop event,
    then calls loop.call_soon_threadsafe(loop.stop) and is joined; once
    run_forever() returns, shutdown_loop() cancels any still-pending
    tasks (e.g. a _fetch() coroutine mid asyncio.sleep) and runs the loop
    once more to let them finish cancelling, before closing it — a
    coroutine destroyed while pending would otherwise print an "was
    destroyed but it is pending" warning to stderr.
  - prism._run_headless() for --headless / CI: runs for 1s then asserts
    the round trip happened at least once.

Run:
  PYTHONPATH=build/python python python/examples/12_asyncio_bridge.py
  PYTHONPATH=build/python python python/examples/12_asyncio_bridge.py --headless
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


def shutdown_loop(loop: asyncio.AbstractEventLoop) -> None:
    """Cancel any pending tasks and let the loop run them to completion
    before closing it — closing with a task still pending mid-await
    prints "Task was destroyed but it is pending!" to stderr."""
    pending = asyncio.all_tasks(loop)
    for task in pending:
        task.cancel()
    if pending:
        loop.run_until_complete(asyncio.gather(*pending, return_exceptions=True))
    loop.close()


def main(headless: bool = False) -> None:
    m = AsyncioBridge()
    loop = asyncio.new_event_loop()

    def run_loop(stop: threading.Event) -> None:
        asyncio.set_event_loop(loop)
        watcher = threading.Thread(
            target=lambda: (stop.wait(), loop.call_soon_threadsafe(loop.stop)),
            daemon=True,
        )
        watcher.start()
        loop.run_forever()
        watcher.join()
        shutdown_loop(loop)

    prism.worker(run_loop)

    def on_trigger(v: int) -> None:
        asyncio.run_coroutine_threadsafe(_fetch(m, v), loop)

    m.trigger.observe(on_trigger)

    def on_result(v: float) -> None:
        m.last_result.value = v
        m.round_trips.value += 1
        m.status.value = f"round trip {m.round_trips.value}: {v:.1f}"

    AsyncioBridge.results.observe(m, on_result)

    ticks = [0]

    def ticker(stop: threading.Event) -> None:
        ticks[0] += 1
        m.trigger.value = ticks[0]

    prism.worker(ticker, interval=0.05)

    if headless or "--headless" in sys.argv[1:]:
        prism._run_headless(m, delay_ms=1000)
    else:
        prism.run(m, title="Asyncio Bridge — Python")

    print(f"status={m.status.value} round_trips={m.round_trips.value}")

    if headless or "--headless" in sys.argv[1:]:
        assert m.round_trips.value >= 1, "no asyncio round trip completed"
        print(f"round_trips={m.round_trips.value}")


if __name__ == "__main__":
    main()
