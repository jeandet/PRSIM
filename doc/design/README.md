# PRISM Design Documents

Each document details the design, rationale, and constraints for one subsystem. They are meant to be read before implementing and updated as the design evolves.

| Document | Subsystem | Status |
|---|---|---|
| [threading-model.md](threading-model.md) | Lock-free snapshot handoff, MPSC diff queue, thread roles | Draft |
| [scene-snapshot.md](scene-snapshot.md) | SceneSnapshot structure, versioning, diffing strategy | Draft |
| [draw-list.md](draw-list.md) | DrawList format, command set, serialisation, extensibility | Draft |
| [render-backend.md](render-backend.md) | RenderBackend concept, software backend, GPU backend path | Draft |
| [layout-engine.md](layout-engine.md) | Constraint regions, parallel solving, pure functional layout | Planned |
| [reactivity.md](reactivity.md) | Signals, observable properties, bindings, std::execution | Planned |
| [widget-model.md](widget-model.md) | Value-type widgets, declarative composition, handle system | Planned |
| [python-bindings.md](python-bindings.md) | nanobind wrapping, GIL-free Python 3.14+, callback threading | Planned |
| [testing-strategy.md](testing-strategy.md) | doctest, synchronous scheduler for tests, headless rendering, visual regression | Planned |
| [tracing-profiling.md](tracing-profiling.md) | Tracy behind generic macros, trace points at pipeline boundaries | Planned |
