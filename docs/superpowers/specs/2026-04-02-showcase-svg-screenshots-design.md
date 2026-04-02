# Showcase SVG Screenshots — Design Spec

## Goal

Add auto-generated SVG screenshots to the README. Each screenshot is produced by compiling and running a small example that captures a headless snapshot and exports it via `to_svg()`. The example code shown in the README **is** the code that generates the screenshot — no manual screenshots that rot over time.

## Architecture

### CapturingBackend (reusable header)

Extract the `CapturingBackend` pattern (currently duplicated across tests) into `include/prism/app/capturing_backend.hpp`. This backend:

1. Creates a `HeadlessWindow`
2. On `run()`, immediately sends `WindowClose`
3. On `submit()`, captures the `SceneSnapshot` into a user-provided `shared_ptr`

This gives a single-frame headless capture: `model_app()` builds the widget tree, produces one snapshot, then exits.

### Showcase executables

Each showcase lives in `examples/showcase/` as a standalone `.cpp` file:

```
examples/showcase/
├── meson.build
├── showcase_counter.cpp
├── showcase_slider.cpp
├── showcase_layout.cpp
├── showcase_canvas.cpp
├── showcase_theme.cpp
└── showcase_plot.cpp
```

Each showcase:
1. Defines a model struct (the code shown in the README)
2. Optionally sets interesting field values for the screenshot
3. Uses `CapturingBackend` to capture a snapshot
4. Writes `to_svg(*snap)` to the path given as `argv[1]`

### Meson integration

`examples/showcase/meson.build` iterates a dict of `{name: source}` pairs. For each:
- Builds an executable with `prism_dep`
- Creates a `custom_target` that runs the executable and captures the SVG output

### Generated SVGs

SVGs are committed to `doc/screenshots/` so the README works on GitHub without CI. A helper script `scripts/update_screenshots.sh` runs the build and copies SVGs.

### README changes

A new "Quick Tour" section after the architecture description shows 4-6 examples, each as:
1. A code block with the model struct
2. An `<img>` tag referencing `doc/screenshots/<name>.svg`

## Showcase examples

| Name | Feature | Model |
|------|---------|-------|
| `counter` | Simplest model — Field<int> + Field<string> | `struct Counter { Field<int> count{42}; Field<string> label{"hello"}; }` |
| `slider` | Slider delegate from Field<Slider<>> | `struct Volume { Field<Slider<>> level{...}; }` |
| `layout` | vstack/hstack composition | Nested struct with multiple fields |
| `canvas` | Custom drawing escape hatch | Canvas with shapes |
| `theme` | Theme colors | Model using themed widgets |
| `plot` | PlotSource scientific visualization | Simple waveform plot |

## File changes

| File | Change |
|------|--------|
| `include/prism/app/capturing_backend.hpp` | **New** — reusable headless capture backend |
| `examples/showcase/*.cpp` | **New** — showcase source files |
| `examples/showcase/meson.build` | **New** — build + SVG generation targets |
| `examples/meson.build` | Add `subdir('showcase')` |
| `scripts/update_screenshots.sh` | **New** — build + copy script |
| `doc/screenshots/*.svg` | **New** — generated SVG files |
| `README.md` | Add Quick Tour section |
| `tests/test_model_app.cpp` | Refactor to use `capturing_backend.hpp` (optional) |

## Non-goals

- PNG/raster output (SVG is sufficient for README)
- CI automation (can be added later)
- Interactive examples (these are static snapshots)
