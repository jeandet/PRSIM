# AGENTS.md

Guidance for coding agents (and humans) working in this repository. This covers what's specific
to PRISM; general workflow discipline (TDD, git safety, one-build-at-a-time) is assumed to come
from the operator's own instructions.

## What this is

PRISM (Persistent Rendering & Interactive Scene Model) is a C++26 Model-View-Behavior GUI
framework — a from-scratch Qt-scale replacement, not a widget toolkit on top of one. Model =
`Field<T>` structs, View = `Widget<T>` (synthesized automatically via P2996 reflection when
available), Behavior = user `on_change`/`observe` chains. See `README.md` for the full pitch and
quick-start examples, `doc/design/` for per-subsystem design rationale.

## Build & test

```bash
meson setup builddir
ninja -C builddir
meson test -C builddir
```

- Requires Meson >= 1.5, GCC 16+ for `-freflection` (auto-detected by the build; without it,
  model structs need a manual `view()` method instead of automatic reflection — most structs in
  `examples/` and `tests/` are written to work either way).
- Dependencies (SDL3, stdexec, doctest, fmt, magic_enum) are fetched as Meson wraps under
  `subprojects/` — don't reach for system packages for these.
- Never run more than one build/test invocation against `builddir` concurrently, and never
  background one "just in case it's slow." One `ninja`/`meson test` call, foreground, waited out,
  read for its real output — then the next. Overlapping backgrounded builds against the same
  build dir have visibly degraded the host machine before.
- `meson wrap update` periodically bumps wrapdb-tracked dependencies (routine hygiene, e.g.
  before a PR) — rebuild and rerun the full suite afterward.

## Module layout (`include/prism/`)

Eight namespaced modules: `core` (`Field`/`State`/`Derived`/`Shared`/`Channel`/`List`/
`Connection`/reflection), `render` (`DrawList`), `input` (`InputEvent`/hit testing), `ui`
(`Widget<T>` delegates, layout, `Node`), `delegates` (built-in widget implementations), `app`
(`WidgetTree`, `ViewBuilder`, `model_app`, event routing), `widgets` (`Tree`/`Table`/`Plot`/debug
inspector), `backends` (SDL3, headless software/null backends used by tests).

## Conventions

- Architecture is MVB, not MVU or immediate-mode — check `doc/design/` before introducing a
  pattern that looks like either.
- `Field<T>` holds only a value, never a label — labels come from reflection (the member name) or
  UI-layer wrapping, not from the field itself.
- Widgets are persistent and dirty-repainted, never rebuilt from scratch every frame.
- `Shared<T>` is cross-thread latest-value state (coalescing — intermediate `set()`s are dropped);
  `Channel<T>` is a cross-thread ordered/lossless event stream (every `send()` is delivered, in
  order). Reach for the one whose semantics you actually need, not whichever is more familiar.
- Core headers (`include/prism/core/`) stay SDL-free; SDL types never leak past the backend
  layer.
- Tests use doctest. Headless tests (no live SDL window) are registered in `tests/meson.build`'s
  `headless_tests` map; `tests/test_*.cpp` mirrors `include/prism/**` roughly 1:1. Example code
  under `examples/` gets its own tests too where it has non-trivial logic worth pinning down
  (e.g. `tests/test_proc_metrics.cpp` for `examples/model_system_monitor/`).

## Where to look first

- `README.md` — architecture tour, quick-start examples, roadmap.
- `doc/design/` — per-subsystem design rationale (one file per major subsystem).
- `doc/review-2026-08-28.md` — most recent architecture review, verified findings, and the
  agreed work order it produced.
