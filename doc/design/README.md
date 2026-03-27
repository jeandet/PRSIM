# PRISM Design Documents

Each document details the design, rationale, and constraints for one subsystem. They are meant to be read before implementing and updated as the design evolves.

**Architectural pivot (2026-03-27):** PRISM moved from MVU (per-frame view rebuild) to a **persistent widget tree with C++26 sender-based observer pattern**. The goal is a Qt replacement, not an immediate/retained-mode hybrid. `Field<T>` model structs are the core abstraction — simultaneously data, observable, and widget spec. Documents marked "Needs update" predate this pivot and contain MVU-era assumptions.

| Document | Subsystem | Status |
|---|---|---|
| [app-facade.md](app-facade.md) | `prism::app<State>()` + `Ui<State>` MVU entry point | **Needs update** — pivot to persistent widget tree |
| [threading-model.md](threading-model.md) | Event-driven snapshot handoff, thread roles, input flow | **Implemented (POC)** — still valid post-pivot |
| [scene-snapshot.md](scene-snapshot.md) | SceneSnapshot structure, versioning, full replacement model | **Implemented (POC)** — dirty repaint replaces full rebuild |
| [draw-list.md](draw-list.md) | DrawList format, command set, serialisation, extensibility | **Implemented (POC)** — still valid |
| [render-backend.md](render-backend.md) | BackendBase vtable, SoftwareBackend, Backend wrapper | **Implemented** — still valid |
| [input-events.md](input-events.md) | Input queue, event forwarding, hit testing | **Implemented** — still valid, sender vision now primary |
| [widget-model.md](widget-model.md) | Function components, `Ui<State>` context, `.on_X()` handlers | **Needs update** — pivot to persistent widgets from Field<T> structs |
| [reactivity.md](reactivity.md) | MVU state flow, `.on_X()` handlers, internal signals | **Needs update** — sender/observer is now primary, not internal |
| [styling.md](styling.md) | Theme as data, Context propagation, per-instance overrides | Draft — still valid |
| [layout-engine](../../docs/superpowers/specs/2026-03-27-layout-hit-regions-design.md) | `row()`, `column()`, `spacer()`, stack-based layout, hit testing | **Implemented (Phase 2)** — still valid |
| [python-bindings.md](python-bindings.md) | nanobind wrapping, GIL-free Python 3.14+, callback threading | Planned |
| [testing-strategy.md](testing-strategy.md) | doctest, synchronous scheduler for tests, headless rendering, visual regression | Planned |
| [tracing-profiling.md](tracing-profiling.md) | Tracy behind generic macros, trace points at pipeline boundaries | Planned |
