# PRISM Design Documents

Each document details the design, rationale, and constraints for one subsystem. They are meant to be read before implementing and updated as the design evolves.

| Document | Subsystem | Status |
|---|---|---|
| [threading-model.md](threading-model.md) | Lock-free snapshot handoff, MPSC diff queue, thread roles | Draft |
| [scene-snapshot.md](scene-snapshot.md) | SceneSnapshot structure, versioning, diffing strategy | Draft |
| [draw-list.md](draw-list.md) | DrawList format, command set, serialisation, extensibility | Draft |
| [render-backend.md](render-backend.md) | RenderBackend concept, software backend, GPU backend path | Draft |
| [input-events.md](input-events.md) | Input queue, hit testing, signals, focus management | Draft |
| [reactivity.md](reactivity.md) | Signals, observables, bindings, observable collections | Draft |
| [layout-engine.md](layout-engine.md) | Constraint regions, parallel solving, pure functional layout | Planned |
| [widget-model.md](widget-model.md) | Value-type widgets, DrawList as user drawing API, composition | Draft |
| [styling.md](styling.md) | Theme as data, Context propagation, per-instance overrides, state variants | Draft |
| [python-bindings.md](python-bindings.md) | nanobind wrapping, GIL-free Python 3.14+, callback threading | Planned |
| [testing-strategy.md](testing-strategy.md) | doctest, synchronous scheduler for tests, headless rendering, visual regression | Planned |
| [tracing-profiling.md](tracing-profiling.md) | Tracy behind generic macros, trace points at pipeline boundaries | Planned |
