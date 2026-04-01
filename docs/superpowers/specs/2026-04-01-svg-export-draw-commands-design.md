# SVG Export + Draw Command Extensions

**Date:** 2026-04-01
**Status:** Draft

## Goal

Add SVG export for DrawList/SceneSnapshot (snapshot testing, documentation, plot export) and extend the draw command set with primitives needed for UI polish and scientific visualization.

## New Draw Commands

Four new structs added to the `DrawCmd` variant in `draw_list.hpp`:

```cpp
struct RoundedRect {
    Rect rect;
    Color color;
    float radius;       // corner radius
    float thickness;     // 0 = filled, >0 = stroke only
};

struct Line {
    Point from;
    Point to;
    Color color;
    float thickness;
};

struct Polyline {
    std::vector<Point> points;
    Color color;
    float thickness;
};

struct Circle {
    Point center;
    float radius;
    Color color;
    float thickness;     // 0 = filled, >0 = stroke only
};
```

Variant becomes:

```cpp
using DrawCmd = std::variant<FilledRect, RectOutline, TextCmd, ClipPush, ClipPop,
                             RoundedRect, Line, Polyline, Circle>;
```

### Conventions

- `thickness == 0` means filled, `thickness > 0` means stroke only (applies to RoundedRect, Circle).
- All use plain `float` for thickness/radius (consistent with existing `RectOutline::thickness`).
- DrawList gets convenience methods: `rounded_rect()`, `line()`, `polyline()`, `circle()`, each applying `current_offset()`.
- `bounding_box()` updated to account for Line (min/max of endpoints), Polyline (min/max of all points), Circle (center ± radius), RoundedRect (same as rect).

## SVG Export

### Location

`include/prism/core/svg_export.hpp` — header-only, core, no SDL dependency.

### API

```cpp
namespace prism {
    std::string to_svg(const DrawList& dl);
    std::string to_svg(const SceneSnapshot& snap);
}
```

### Command → SVG Mapping

| DrawCmd | SVG element |
|---------|------------|
| `FilledRect` | `<rect x="..." y="..." width="..." height="..." fill="rgba(...)"/>` |
| `RectOutline` | `<rect ... stroke="rgba(...)" fill="none" stroke-width="..."/>` |
| `RoundedRect` | `<rect ... rx="..." fill/stroke based on thickness/>` |
| `TextCmd` | `<text x="..." y="..." font-family="monospace" font-size="...">...</text>` |
| `Line` | `<line x1="..." y1="..." x2="..." y2="..." stroke="rgba(...)" stroke-width="..."/>` |
| `Polyline` | `<polyline points="x,y x,y ..." stroke="rgba(...)" fill="none" stroke-width="..."/>` |
| `Circle` | `<circle cx="..." cy="..." r="..." fill/stroke based on thickness/>` |
| `ClipPush` | `<clipPath id="clip-N"><rect .../></clipPath>` in `<defs>`, open `<g clip-path="url(#clip-N)">` |
| `ClipPop` | `</g>` |

### SceneSnapshot Export

1. Compute viewBox from union of all geometry rects.
2. Emit `<svg xmlns="..." viewBox="...">`.
3. Collect all clip defs across all draw lists, emit in a single `<defs>` block.
4. Iterate `z_order`, emit each DrawList's commands as SVG elements.
5. Emit overlay DrawList last.
6. Close `</svg>`.

### Text Handling

SVG text uses `font-family="monospace"` as a reasonable default (PRISM bundles JetBrains Mono, a monospace font). Exact glyph metrics won't match SDL_ttf pixel-perfect, but that's acceptable — SVG viewers handle text layout natively.

## SDL Backend Changes

Extend `render_cmd()` visitor in `sdl_window.cpp`:

| Command | SDL3 call |
|---------|-----------|
| `RoundedRect` | `SDL_RenderFillRoundRect` / `SDL_RenderRoundRect` (SDL 3.4+), fallback to regular rect |
| `Line` | `SDL_RenderLine` |
| `Polyline` | `SDL_RenderLines` with `SDL_FPoint[]` |
| `Circle` | Manual midpoint circle algorithm or `SDL_RenderCircle` if available |

## Files Changed

| File | Change |
|------|--------|
| `include/prism/core/draw_list.hpp` | 4 new command structs, variant update, DrawList methods, bounding_box update |
| `include/prism/core/svg_export.hpp` | **New file** — `to_svg(DrawList)`, `to_svg(SceneSnapshot)` |
| `src/backends/sdl_window.cpp` | Extend `render_cmd()` for 4 new commands |
| `tests/test_svg_export.cpp` | SVG output tests: known commands → expected SVG string fragments |
| `tests/test_draw_list.cpp` | Tests for new DrawList methods and bounding_box with new commands |

## Out of Scope

- Path command (general bezier/arc paths) — future
- Gradient fills — future
- ImageBlit — future, orthogonal to SVG
- PNG export (needs font rasterizer without SDL) — separate effort
- SVG import / round-trip — not planned
