"""11_error_handling.py — prism.on_error() hook for observer/worker exceptions.

Demonstrates:
  - an observer that raises on odd values
  - a one-shot prism.worker() that raises
  - prism.on_error(handler) counting exceptions without ever stopping the
    drain — the handler runs on whichever thread raised: the logic thread
    for observer exceptions, a worker's own thread for prism.worker() ones
  - --headless to run under prism.headless() for CI / no display

Run:
  PYTHONPATH=builddir/python python3 python/examples/11_error_handling.py
  PYTHONPATH=builddir/python python3 python/examples/11_error_handling.py --headless
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

    ticks = 0

    def ticker() -> None:
        nonlocal ticks
        ticks += 1
        m.value.value = ticks

    prism.worker(ticker, interval=0.03, repeat=10)

    def flaky_worker(stop: threading.Event) -> None:
        raise RuntimeError("worker failed once")

    prism.worker(flaky_worker)

    prism.run(m, title="Error Handling — Python", headless=1.0 if "--headless" in sys.argv else None)

    print(f"status={m.status.value} error_count={len(errors)}")
    prism.on_error(None)


if __name__ == "__main__":
    main()
