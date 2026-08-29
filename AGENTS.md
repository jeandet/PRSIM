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

## Goal, not just architecture

PRISM's end goal is a full Qt-scale C++ GUI framework replacement, not a demo or a toy. "Instant
UI from a struct" (reflection-driven auto-UI) is the entry wedge that gets people to look, not
the whole point — don't let it crowd out the harder, less flashy work (proving the render/scene
architecture scales, widening the widget catalog, threading/perf story) that the wedge is
supposed to lead into. See `doc/design/reactivity.md` and `doc/design/threading-model.md` for why
the core is an observer/sender graph (`Connection`/`SenderHub`) with a persistent, dirty-repainted
widget tree — not diffing, not a virtual DOM, not immediate-mode — from day one, not as a later
optimization.

Two validated practices worth continuing rather than rediscovering:

- **Build composite, multi-thread, multi-widget reference apps, not just single-widget demos.**
  `examples/model_system_monitor/` is the load-bearing one — building it (and now extending it,
  e.g. for `Channel<T>`) has repeatedly surfaced real framework bugs (drain-on-tick ordering,
  shutdown livelocks, cross-thread state races) that unit tests and single-widget dogfooding both
  missed. When a feature needs a forcing function to prove it's real, reach for this app before
  writing a new demo.
- **Passing tests + "done" is not sufficient evidence a GUI feature actually works.** Expect real
  bugs to surface only once a feature is used for something, and treat that as normal, not a sign
  something went wrong in planning.

Where a widget needs to bind to data in different shapes (`Tree`, `Table`), the established
pattern is a small number of adapter tiers — e.g. filesystem source / manual `TreeSource` or
`ColumnStorage` implementation / reflected-struct tier — so the same widget works whether or not
`-freflection` is available and whatever shape the underlying data actually has, rather than one
widget per data source.

## C++ style in this codebase

The general taste is KISS, functional-over-imperative, and one level of abstraction per function
— see how that plays out concretely here:

- **Climb the ladder before adding a dependency or writing new low-level machinery.** `Channel<T>`
  (`include/prism/core/channel.hpp`) is built directly on the existing lock-free `mpsc_queue<T>`
  rather than pulling in a third-party concurrent-queue library, because the primitive it needed
  already existed in this codebase. Check `core/` and `subprojects/*.wrap` before reaching outward.
- **One level of abstraction per function/file; split when that stops being true.** `WidgetTree`
  used to mix a 630-line builder DSL, stateless tree-walk helpers, and the frame pipeline in one
  1960-line file; it's now three headers (`widget_tree.hpp`, `widget_tree_layout.hpp`,
  `widget_tree_traversal.hpp`) plus `view_builder.hpp`, split along exactly that seam. Prefer
  extracting a stateless free function over growing a class method when the two don't actually
  share state.
- **No comment-decorated code blocks — extract instead.** This codebase's own `detail::` namespace
  convention (free functions in an inner `detail` namespace, called by the public API) is the
  standard way to name a block of logic instead of commenting it. See the caveat right below,
  though.
- **Never name a nested namespace bare `detail` under a project module namespace.** Test files
  routinely do `using namespace app; using namespace ui; ...` together, and qualified lookup of
  `prism::detail::X` then searches every nominated namespace named `detail` — a second one
  silently collides with the first regardless of intent. Name it after the module instead (e.g.
  `widget_detail`, per `include/prism/app/event_routing.hpp`).
- **Compile-time dispatch over runtime polymorphism for widget behavior.** Every built-in widget
  is a `Widget<T>` template specialization (`include/prism/ui/delegate.hpp`), not a subclass of a
  virtual base — `is_widget_v<T>` is a concept, not a marker interface. Follow that shape for new
  widget types rather than introducing a vtable.
- **Strong, zero-cost types for geometry — never a bare `float` for a coordinate.** `X`/`Y`/`DX`/
  `DY`/`Width`/`Height` are distinct `Scalar<Tag>` types with their own affine algebra; mixing an
  absolute coordinate and a delta, or an X and a Y, is a compile error here, not a runtime bug.
- **Keep pure logic separable from thread/IO glue, and test the pure part directly.** E.g.
  `diff_process_events()` (`examples/model_system_monitor/proc_metrics.hpp`) is a plain function
  over two snapshots, unit-tested with no `/proc` or thread involved — the polling loop that calls
  it is a thin wrapper, not where the logic itself lives.
- **Mark a deliberate cut with a `simplify:` comment naming the ceiling and the upgrade path**,
  right where the cut is made (see `include/prism/ui/layout.hpp` and `core/channel.hpp` for two
  examples) — not just in a commit message, which the next person touching the code won't see.

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
