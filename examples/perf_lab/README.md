# perf_lab

PRISM's performance laboratory: the workloads the architecture claims to survive, with
the numbers on screen.

- **100k-row table** (hand-written `ColumnStorage`, virtualized) — a rotating 1000-row
  slice is mutated per telemetry tick, so visible rows rebind every publish.
- **1M-point plot** — one polyline series over a ring buffer; every telemetry drain
  re-records it. This is the deliberately expensive path.
- **1 kHz synthetic telemetry** — a producer thread posts into a coalescing
  `Shared<double>`; intermediate samples are dropped by design.
- **Stats bar** — present FPS and present/build times, dirty-widget count, draw-command
  count, snapshot bytes, and snapshot age, refreshed ~2 Hz.

```bash
./builddir/examples/perf_lab/perf_lab                    # interactive
./builddir/examples/perf_lab/perf_lab --headless 10      # 10 s headless run, prints a report
./builddir/examples/perf_lab/perf_lab --rows 1000 --points 5000 --rate 100   # gentle
```
