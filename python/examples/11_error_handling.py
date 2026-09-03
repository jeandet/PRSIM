"""11_error_handling.py — prism.on_error() hook for observer/worker exceptions.

Demonstrates:
  - An observer that raises on odd values
  - A worker that raises once (one-shot, no interval)
  - prism.on_error(handler) counting and logging exceptions without ever
    stopping the drain — the next event still fires, the app keeps running
  - ``--headless`` to run under prism.headless() for CI / no display

The handler runs on whichever thread raised: observer/derived exceptions
route through the logic thread, but a ``prism.worker()`` exception is
caught and reported directly on that worker's own thread (see
``prism/__init__.py:_report_worker_error``). So the handler below only
does a thread-safe ``list.append`` — never a field read-modify-write.

Run:
  PYTHONPATH=build/python python python/examples/11_error_handling.py
  PYTHONPATH=build/python python python/examples/11_error_handling.py --headless
"""

import sys
import threading

import prism


class ErrorDemo(prism.Model):
    value = prism.field(0)
    status = prism.field("ready")


def main() -> None:
    m = ErrorDemo()
    errors: list[Exception] = []

    def handle_error(exc: Exception) -> None:
        errors.append(exc)
        print(f"[on_error] {type(exc).__name__}: {exc}")

    prism.on_error(handle_error)

    def on_value_change(v: int) -> None:
        if v % 2 == 1:
            raise ValueError(f"odd value not allowed: {v}")
        m.status.value = f"ok: {v}"

    m.value.observe(on_value_change)

    ticks = [0]

    def ticker(stop: threading.Event) -> None:
        ticks[0] += 1
        m.value.value = ticks[0]
        if ticks[0] >= 10:
            stop.set()

    prism.worker(ticker, interval=0.03)

    def flaky_worker(stop: threading.Event) -> None:
        raise RuntimeError("worker failed once")

    prism.worker(flaky_worker)

    if "--headless" in sys.argv:
        with prism.headless(m, timeout=0.6) as app:
            app.wait_until(lambda: ticks[0] >= 10, timeout=0.5)
    else:
        prism.run(m, title="Error Handling — Python")

    print(f"status={m.status.value} error_count={len(errors)}")
    prism.on_error(None)


if __name__ == "__main__":
    main()
