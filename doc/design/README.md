# PRISM Design Documents

Each document details the design, rationale, and constraints for one subsystem. They are meant to be read before implementing and updated as the design evolves.

| Document | Subsystem | Status |
|---|---|---|
| [app-facade.md](app-facade.md) | `prism::app<State>()` + `Ui<State>` MVU entry point | **Implemented** — `Ui<State>` + `app<State>()` + update callback |
| [threading-model.md](threading-model.md) | Event-driven snapshot handoff, thread roles, input flow | **Implemented (POC)** |
| [scene-snapshot.md](scene-snapshot.md) | SceneSnapshot structure, versioning, full replacement model | **Implemented (POC)** |
| [draw-list.md](draw-list.md) | DrawList format, command set, serialisation, extensibility | **Implemented (POC)** |
| [render-backend.md](render-backend.md) | BackendBase vtable, SoftwareBackend, Backend wrapper | **Implemented** |
| [input-events.md](input-events.md) | Input queue, event forwarding, hit testing | **Implemented** (queue + forwarding + keyboard), hit regions planned Phase 2 |
| [widget-model.md](widget-model.md) | Function components, `Ui<State>` context, `.on_X()` handlers, low-level Widget concept | **Redesigned** — components are functions, not structs |
| [reactivity.md](reactivity.md) | MVU state flow, `.on_X()` handlers, internal signals, future reflection-derived UI | **Redesigned** — MVU primary, signals internal |
| [styling.md](styling.md) | Theme as data, Context propagation, per-instance overrides, state variants | Draft |
| [layout-engine.md](layout-engine.md) | `row()`, `column()`, `spacer()`, constraint regions, parallel solving | Planned (Phase 2) |
| [python-bindings.md](python-bindings.md) | nanobind wrapping, GIL-free Python 3.14+, callback threading | Planned |
| [testing-strategy.md](testing-strategy.md) | doctest, synchronous scheduler for tests, headless rendering, visual regression | Planned |
| [tracing-profiling.md](tracing-profiling.md) | Tracy behind generic macros, trace points at pipeline boundaries | Planned |
