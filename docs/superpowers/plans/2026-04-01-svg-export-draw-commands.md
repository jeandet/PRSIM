# SVG Export + Draw Command Extensions — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend DrawList with RoundedRect, Line, Polyline, Circle commands and add a header-only SVG exporter for SceneSnapshot.

**Architecture:** New command structs added to the `DrawCmd` variant. SVG export is a pure function (`to_svg`) that visits draw commands and emits XML strings. SDL backend extended with `render_cmd` overloads for each new command.

**Tech Stack:** C++23/26, doctest, SDL3, Meson

**Spec:** `docs/superpowers/specs/2026-04-01-svg-export-draw-commands-design.md`

---

### File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `include/prism/core/draw_list.hpp` | Modify | Add 4 command structs, update variant, add DrawList methods, update `bounding_box()` |
| `include/prism/core/svg_export.hpp` | Create | `to_svg(DrawList)` and `to_svg(SceneSnapshot)` — header-only, no SDL |
| `include/prism/backends/sdl_window.hpp` | Modify | Declare 4 new `render_cmd` overloads |
| `src/backends/sdl_window.cpp` | Modify | Implement 4 new `render_cmd` overloads |
| `tests/test_draw_list.cpp` | Modify | Tests for new commands and `bounding_box()` |
| `tests/test_svg_export.cpp` | Create | Tests for SVG output of all command types |
| `tests/meson.build` | Modify | Register `svg_export` test |

---

### Task 1: Add RoundedRect command

**Files:**
- Modify: `include/prism/core/draw_list.hpp:30-54` (command structs + variant)
- Modify: `tests/test_draw_list.cpp`

- [ ] **Step 1: Write failing test for RoundedRect in DrawList**

Add to `tests/test_draw_list.cpp`:

```cpp
TEST_CASE("DrawList rounded_rect command") {
    prism::DrawList dl;
    dl.rounded_rect(R(10, 20, 100, 50), prism::Color::rgba(255, 0, 0), 8.f);
    REQUIRE(dl.size() == 1);
    auto& rr = std::get<prism::RoundedRect>(dl.commands[0]);
    CHECK(rr.rect.origin.x.raw() == 10.f);
    CHECK(rr.rect.origin.y.raw() == 20.f);
    CHECK(rr.rect.extent.w.raw() == 100.f);
    CHECK(rr.rect.extent.h.raw() == 50.f);
    CHECK(rr.radius == 8.f);
    CHECK(rr.thickness == 0.f);
}

TEST_CASE("DrawList rounded_rect stroke") {
    prism::DrawList dl;
    dl.rounded_rect(R(0, 0, 50, 50), prism::Color::rgba(0, 0, 0), 5.f, 2.f);
    auto& rr = std::get<prism::RoundedRect>(dl.commands[0]);
    CHECK(rr.thickness == 2.f);
}

TEST_CASE("clip_push offsets rounded_rect") {
    prism::DrawList dl;
    dl.clip_push(P(10.f, 20.f), S(200.f, 200.f));
    dl.rounded_rect(R(0, 0, 50, 30), prism::Color::rgba(0, 0, 0), 4.f);
    dl.clip_pop();
    auto& rr = std::get<prism::RoundedRect>(dl.commands[1]);
    CHECK(rr.rect.origin.x.raw() == 10.f);
    CHECK(rr.rect.origin.y.raw() == 20.f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test test_draw_list -C builddir --print-errorlogs`
Expected: FAIL — `RoundedRect` not defined

- [ ] **Step 3: Implement RoundedRect**

In `include/prism/core/draw_list.hpp`, add the struct after `ClipPop` (line 52) and before the `using DrawCmd` line:

```cpp
struct RoundedRect {
    Rect rect;
    Color color;
    float radius;
    float thickness;  // 0 = filled, >0 = stroke only
};
```

Update the variant (line 54):

```cpp
using DrawCmd = std::variant<FilledRect, RectOutline, TextCmd, ClipPush, ClipPop,
                             RoundedRect>;
```

Add the DrawList method after `rect_outline()` (around line 72):

```cpp
void rounded_rect(Rect r, Color c, float radius, float thickness = 0.f)
{
    auto o = current_offset();
    commands.emplace_back(RoundedRect{
        {Point{r.origin.x + o.dx, r.origin.y + o.dy}, r.extent}, c, radius, thickness});
}
```

Update `bounding_box()` — the existing `if constexpr (requires { c.rect; })` already catches RoundedRect since it has a `.rect` member. No change needed.

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test test_draw_list -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/draw_list.hpp tests/test_draw_list.cpp
git commit -m "feat(draw): add RoundedRect command to DrawList"
```

---

### Task 2: Add Line command

**Files:**
- Modify: `include/prism/core/draw_list.hpp`
- Modify: `tests/test_draw_list.cpp`

- [ ] **Step 1: Write failing test for Line**

Add to `tests/test_draw_list.cpp`:

```cpp
TEST_CASE("DrawList line command") {
    prism::DrawList dl;
    dl.line(P(0, 0), P(100, 50), prism::Color::rgba(255, 0, 0), 2.f);
    REQUIRE(dl.size() == 1);
    auto& ln = std::get<prism::Line>(dl.commands[0]);
    CHECK(ln.from.x.raw() == 0.f);
    CHECK(ln.from.y.raw() == 0.f);
    CHECK(ln.to.x.raw() == 100.f);
    CHECK(ln.to.y.raw() == 50.f);
    CHECK(ln.thickness == 2.f);
}

TEST_CASE("clip_push offsets line endpoints") {
    prism::DrawList dl;
    dl.clip_push(P(10.f, 20.f), S(200.f, 200.f));
    dl.line(P(0, 0), P(50, 50), prism::Color::rgba(0, 0, 0), 1.f);
    dl.clip_pop();
    auto& ln = std::get<prism::Line>(dl.commands[1]);
    CHECK(ln.from.x.raw() == 10.f);
    CHECK(ln.from.y.raw() == 20.f);
    CHECK(ln.to.x.raw() == 60.f);
    CHECK(ln.to.y.raw() == 70.f);
}

TEST_CASE("bounding_box includes line endpoints") {
    prism::DrawList dl;
    dl.line(P(10, 20), P(90, 80), prism::Color::rgba(0, 0, 0), 1.f);
    auto bb = dl.bounding_box();
    CHECK(bb.origin.x.raw() == 10.f);
    CHECK(bb.origin.y.raw() == 20.f);
    CHECK(bb.extent.w.raw() == 80.f);
    CHECK(bb.extent.h.raw() == 60.f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test test_draw_list -C builddir --print-errorlogs`
Expected: FAIL — `Line` not defined

- [ ] **Step 3: Implement Line**

In `include/prism/core/draw_list.hpp`, add the struct after `RoundedRect`:

```cpp
struct Line {
    Point from;
    Point to;
    Color color;
    float thickness;
};
```

Update variant:

```cpp
using DrawCmd = std::variant<FilledRect, RectOutline, TextCmd, ClipPush, ClipPop,
                             RoundedRect, Line>;
```

Add DrawList method:

```cpp
void line(Point from, Point to, Color c, float thickness = 1.f)
{
    auto o = current_offset();
    commands.emplace_back(Line{
        Point{from.x + o.dx, from.y + o.dy},
        Point{to.x + o.dx, to.y + o.dy},
        c, thickness});
}
```

Update `bounding_box()` — Line has no `.rect` member and no `.origin`. Add a new branch to the visitor lambda:

```cpp
else if constexpr (requires { c.from; c.to; }) {
    expand({c.from, Size{Width{0}, Height{0}}});
    expand({c.to, Size{Width{0}, Height{0}}});
}
```

This uses zero-size rects at each endpoint so `expand()` picks up both points.

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test test_draw_list -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/draw_list.hpp tests/test_draw_list.cpp
git commit -m "feat(draw): add Line command to DrawList"
```

---

### Task 3: Add Polyline command

**Files:**
- Modify: `include/prism/core/draw_list.hpp`
- Modify: `tests/test_draw_list.cpp`

- [ ] **Step 1: Write failing test for Polyline**

Add to `tests/test_draw_list.cpp`:

```cpp
TEST_CASE("DrawList polyline command") {
    prism::DrawList dl;
    std::vector<prism::Point> pts = {P(0, 0), P(50, 25), P(100, 0)};
    dl.polyline(pts, prism::Color::rgba(0, 255, 0), 1.5f);
    REQUIRE(dl.size() == 1);
    auto& pl = std::get<prism::Polyline>(dl.commands[0]);
    CHECK(pl.points.size() == 3);
    CHECK(pl.points[1].x.raw() == 50.f);
    CHECK(pl.points[1].y.raw() == 25.f);
    CHECK(pl.thickness == 1.5f);
}

TEST_CASE("clip_push offsets polyline points") {
    prism::DrawList dl;
    dl.clip_push(P(5.f, 10.f), S(200.f, 200.f));
    std::vector<prism::Point> pts = {P(0, 0), P(20, 30)};
    dl.polyline(pts, prism::Color::rgba(0, 0, 0), 1.f);
    dl.clip_pop();
    auto& pl = std::get<prism::Polyline>(dl.commands[1]);
    CHECK(pl.points[0].x.raw() == 5.f);
    CHECK(pl.points[0].y.raw() == 10.f);
    CHECK(pl.points[1].x.raw() == 25.f);
    CHECK(pl.points[1].y.raw() == 40.f);
}

TEST_CASE("bounding_box includes all polyline points") {
    prism::DrawList dl;
    std::vector<prism::Point> pts = {P(10, 50), P(90, 10), P(50, 80)};
    dl.polyline(pts, prism::Color::rgba(0, 0, 0), 1.f);
    auto bb = dl.bounding_box();
    CHECK(bb.origin.x.raw() == 10.f);
    CHECK(bb.origin.y.raw() == 10.f);
    CHECK(bb.extent.w.raw() == 80.f);
    CHECK(bb.extent.h.raw() == 70.f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test test_draw_list -C builddir --print-errorlogs`
Expected: FAIL — `Polyline` not defined

- [ ] **Step 3: Implement Polyline**

In `include/prism/core/draw_list.hpp`, add the struct after `Line`:

```cpp
struct Polyline {
    std::vector<Point> points;
    Color color;
    float thickness;
};
```

Update variant:

```cpp
using DrawCmd = std::variant<FilledRect, RectOutline, TextCmd, ClipPush, ClipPop,
                             RoundedRect, Line, Polyline>;
```

Add DrawList method:

```cpp
void polyline(std::vector<Point> pts, Color c, float thickness = 1.f)
{
    auto o = current_offset();
    for (auto& p : pts)
        p = Point{p.x + o.dx, p.y + o.dy};
    commands.emplace_back(Polyline{std::move(pts), c, thickness});
}
```

Update `bounding_box()` — add a branch for Polyline:

```cpp
else if constexpr (requires { c.points; }) {
    for (const auto& p : c.points)
        expand({p, Size{Width{0}, Height{0}}});
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test test_draw_list -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/draw_list.hpp tests/test_draw_list.cpp
git commit -m "feat(draw): add Polyline command to DrawList"
```

---

### Task 4: Add Circle command

**Files:**
- Modify: `include/prism/core/draw_list.hpp`
- Modify: `tests/test_draw_list.cpp`

- [ ] **Step 1: Write failing test for Circle**

Add to `tests/test_draw_list.cpp`:

```cpp
TEST_CASE("DrawList circle command") {
    prism::DrawList dl;
    dl.circle(P(50, 50), 25.f, prism::Color::rgba(0, 0, 255));
    REQUIRE(dl.size() == 1);
    auto& ci = std::get<prism::Circle>(dl.commands[0]);
    CHECK(ci.center.x.raw() == 50.f);
    CHECK(ci.center.y.raw() == 50.f);
    CHECK(ci.radius == 25.f);
    CHECK(ci.thickness == 0.f);
}

TEST_CASE("DrawList circle stroke") {
    prism::DrawList dl;
    dl.circle(P(50, 50), 25.f, prism::Color::rgba(0, 0, 0), 2.f);
    auto& ci = std::get<prism::Circle>(dl.commands[0]);
    CHECK(ci.thickness == 2.f);
}

TEST_CASE("clip_push offsets circle center") {
    prism::DrawList dl;
    dl.clip_push(P(10.f, 20.f), S(200.f, 200.f));
    dl.circle(P(0, 0), 15.f, prism::Color::rgba(0, 0, 0));
    dl.clip_pop();
    auto& ci = std::get<prism::Circle>(dl.commands[1]);
    CHECK(ci.center.x.raw() == 10.f);
    CHECK(ci.center.y.raw() == 20.f);
}

TEST_CASE("bounding_box includes circle") {
    prism::DrawList dl;
    dl.circle(P(50, 50), 20.f, prism::Color::rgba(0, 0, 0));
    auto bb = dl.bounding_box();
    CHECK(bb.origin.x.raw() == 30.f);
    CHECK(bb.origin.y.raw() == 30.f);
    CHECK(bb.extent.w.raw() == 40.f);
    CHECK(bb.extent.h.raw() == 40.f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test test_draw_list -C builddir --print-errorlogs`
Expected: FAIL — `Circle` not defined

- [ ] **Step 3: Implement Circle**

In `include/prism/core/draw_list.hpp`, add the struct after `Polyline`:

```cpp
struct Circle {
    Point center;
    float radius;
    Color color;
    float thickness;  // 0 = filled, >0 = stroke only
};
```

Update variant:

```cpp
using DrawCmd = std::variant<FilledRect, RectOutline, TextCmd, ClipPush, ClipPop,
                             RoundedRect, Line, Polyline, Circle>;
```

Add DrawList method:

```cpp
void circle(Point center, float radius, Color c, float thickness = 0.f)
{
    auto o = current_offset();
    commands.emplace_back(Circle{
        Point{center.x + o.dx, center.y + o.dy}, radius, c, thickness});
}
```

Update `bounding_box()` — add a branch for Circle (has `.center` and `.radius` but no `.rect`):

```cpp
else if constexpr (requires { c.center; c.radius; }) {
    expand({Point{X{c.center.x.raw() - c.radius}, Y{c.center.y.raw() - c.radius}},
            Size{Width{2 * c.radius}, Height{2 * c.radius}}});
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test test_draw_list -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/draw_list.hpp tests/test_draw_list.cpp
git commit -m "feat(draw): add Circle command to DrawList"
```

---

### Task 5: SVG export for DrawList

**Files:**
- Create: `include/prism/core/svg_export.hpp`
- Create: `tests/test_svg_export.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write failing tests for SVG export**

Create `tests/test_svg_export.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/svg_export.hpp>

namespace {
prism::Rect R(float x, float y, float w, float h) {
    return {prism::Point{prism::X{x}, prism::Y{y}}, prism::Size{prism::Width{w}, prism::Height{h}}};
}
prism::Point P(float x, float y) {
    return {prism::X{x}, prism::Y{y}};
}
prism::Size S(float w, float h) {
    return {prism::Width{w}, prism::Height{h}};
}
}

TEST_CASE("to_svg empty DrawList") {
    prism::DrawList dl;
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<svg") != std::string::npos);
    CHECK(svg.find("</svg>") != std::string::npos);
}

TEST_CASE("to_svg FilledRect") {
    prism::DrawList dl;
    dl.filled_rect(R(10, 20, 100, 50), prism::Color::rgba(255, 0, 0, 128));
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<rect") != std::string::npos);
    CHECK(svg.find("x=\"10") != std::string::npos);
    CHECK(svg.find("y=\"20") != std::string::npos);
    CHECK(svg.find("width=\"100") != std::string::npos);
    CHECK(svg.find("height=\"50") != std::string::npos);
    CHECK(svg.find("fill=\"rgba(255,0,0,") != std::string::npos);
}

TEST_CASE("to_svg RectOutline") {
    prism::DrawList dl;
    dl.rect_outline(R(0, 0, 80, 40), prism::Color::rgba(0, 0, 0), 2.f);
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("fill=\"none\"") != std::string::npos);
    CHECK(svg.find("stroke=") != std::string::npos);
    CHECK(svg.find("stroke-width=\"2") != std::string::npos);
}

TEST_CASE("to_svg RoundedRect filled") {
    prism::DrawList dl;
    dl.rounded_rect(R(5, 5, 60, 30), prism::Color::rgba(0, 128, 255), 8.f);
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("rx=\"8") != std::string::npos);
    CHECK(svg.find("fill=\"rgba(0,128,255,") != std::string::npos);
}

TEST_CASE("to_svg RoundedRect stroke") {
    prism::DrawList dl;
    dl.rounded_rect(R(0, 0, 50, 50), prism::Color::rgba(0, 0, 0), 5.f, 2.f);
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("fill=\"none\"") != std::string::npos);
    CHECK(svg.find("stroke-width=\"2") != std::string::npos);
    CHECK(svg.find("rx=\"5") != std::string::npos);
}

TEST_CASE("to_svg TextCmd") {
    prism::DrawList dl;
    dl.text("hello", P(10, 20), 14.f, prism::Color::rgba(0, 0, 0));
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<text") != std::string::npos);
    CHECK(svg.find("x=\"10") != std::string::npos);
    CHECK(svg.find("y=\"20") != std::string::npos);
    CHECK(svg.find("font-size=\"14") != std::string::npos);
    CHECK(svg.find(">hello</text>") != std::string::npos);
}

TEST_CASE("to_svg Line") {
    prism::DrawList dl;
    dl.line(P(0, 0), P(100, 50), prism::Color::rgba(255, 0, 0), 2.f);
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<line") != std::string::npos);
    CHECK(svg.find("x1=\"0") != std::string::npos);
    CHECK(svg.find("y1=\"0") != std::string::npos);
    CHECK(svg.find("x2=\"100") != std::string::npos);
    CHECK(svg.find("y2=\"50") != std::string::npos);
    CHECK(svg.find("stroke-width=\"2") != std::string::npos);
}

TEST_CASE("to_svg Polyline") {
    prism::DrawList dl;
    std::vector<prism::Point> pts = {P(0, 0), P(50, 25), P(100, 0)};
    dl.polyline(pts, prism::Color::rgba(0, 255, 0), 1.5f);
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<polyline") != std::string::npos);
    CHECK(svg.find("points=\"") != std::string::npos);
    CHECK(svg.find("fill=\"none\"") != std::string::npos);
    CHECK(svg.find("stroke-width=\"1.5") != std::string::npos);
}

TEST_CASE("to_svg Circle filled") {
    prism::DrawList dl;
    dl.circle(P(50, 50), 25.f, prism::Color::rgba(0, 0, 255));
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<circle") != std::string::npos);
    CHECK(svg.find("cx=\"50") != std::string::npos);
    CHECK(svg.find("cy=\"50") != std::string::npos);
    CHECK(svg.find("r=\"25") != std::string::npos);
}

TEST_CASE("to_svg Circle stroke") {
    prism::DrawList dl;
    dl.circle(P(50, 50), 25.f, prism::Color::rgba(0, 0, 0), 3.f);
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("fill=\"none\"") != std::string::npos);
    CHECK(svg.find("stroke-width=\"3") != std::string::npos);
}

TEST_CASE("to_svg ClipPush/ClipPop wraps in group") {
    prism::DrawList dl;
    dl.clip_push(P(10, 10), S(80, 40));
    dl.filled_rect(R(10, 10, 30, 20), prism::Color::rgba(255, 0, 0));
    dl.clip_pop();
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<clipPath") != std::string::npos);
    CHECK(svg.find("<g clip-path=") != std::string::npos);
    CHECK(svg.find("</g>") != std::string::npos);
}

TEST_CASE("to_svg text escapes XML entities") {
    prism::DrawList dl;
    dl.text("<b>&\"test\"</b>", P(0, 0), 14.f, prism::Color::rgba(0, 0, 0));
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("&lt;b&gt;") != std::string::npos);
    CHECK(svg.find("&amp;") != std::string::npos);
    CHECK(svg.find("&quot;") != std::string::npos);
}
```

- [ ] **Step 2: Register test in meson.build**

In `tests/meson.build`, add to the `headless_tests` dict (line 34, before the closing `}`):

```
  'svg_export' : files('test_svg_export.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test test_svg_export -C builddir --print-errorlogs`
Expected: FAIL — `svg_export.hpp` not found

- [ ] **Step 4: Implement `to_svg(const DrawList&)`**

Create `include/prism/core/svg_export.hpp`:

```cpp
#pragma once

#include <prism/core/draw_list.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <cstdio>
#include <sstream>
#include <string>

namespace prism {

namespace detail {

inline std::string fmt_float(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

inline std::string fmt_color(Color c) {
    return "rgba(" + std::to_string(c.r) + "," + std::to_string(c.g) + ","
         + std::to_string(c.b) + "," + fmt_float(c.a / 255.f) + ")";
}

inline std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;
        }
    }
    return out;
}

struct SvgEmitter {
    std::ostringstream out;
    int clip_id = 0;

    void emit(const FilledRect& cmd) {
        out << "<rect x=\"" << fmt_float(cmd.rect.origin.x.raw())
            << "\" y=\"" << fmt_float(cmd.rect.origin.y.raw())
            << "\" width=\"" << fmt_float(cmd.rect.extent.w.raw())
            << "\" height=\"" << fmt_float(cmd.rect.extent.h.raw())
            << "\" fill=\"" << fmt_color(cmd.color) << "\"/>\n";
    }

    void emit(const RectOutline& cmd) {
        out << "<rect x=\"" << fmt_float(cmd.rect.origin.x.raw())
            << "\" y=\"" << fmt_float(cmd.rect.origin.y.raw())
            << "\" width=\"" << fmt_float(cmd.rect.extent.w.raw())
            << "\" height=\"" << fmt_float(cmd.rect.extent.h.raw())
            << "\" fill=\"none\" stroke=\"" << fmt_color(cmd.color)
            << "\" stroke-width=\"" << fmt_float(cmd.thickness) << "\"/>\n";
    }

    void emit(const RoundedRect& cmd) {
        out << "<rect x=\"" << fmt_float(cmd.rect.origin.x.raw())
            << "\" y=\"" << fmt_float(cmd.rect.origin.y.raw())
            << "\" width=\"" << fmt_float(cmd.rect.extent.w.raw())
            << "\" height=\"" << fmt_float(cmd.rect.extent.h.raw())
            << "\" rx=\"" << fmt_float(cmd.radius) << "\"";
        if (cmd.thickness > 0.f) {
            out << " fill=\"none\" stroke=\"" << fmt_color(cmd.color)
                << "\" stroke-width=\"" << fmt_float(cmd.thickness) << "\"";
        } else {
            out << " fill=\"" << fmt_color(cmd.color) << "\"";
        }
        out << "/>\n";
    }

    void emit(const TextCmd& cmd) {
        out << "<text x=\"" << fmt_float(cmd.origin.x.raw())
            << "\" y=\"" << fmt_float(cmd.origin.y.raw())
            << "\" font-family=\"monospace\" font-size=\"" << fmt_float(cmd.size)
            << "\" fill=\"" << fmt_color(cmd.color) << "\">"
            << xml_escape(cmd.text) << "</text>\n";
    }

    void emit(const Line& cmd) {
        out << "<line x1=\"" << fmt_float(cmd.from.x.raw())
            << "\" y1=\"" << fmt_float(cmd.from.y.raw())
            << "\" x2=\"" << fmt_float(cmd.to.x.raw())
            << "\" y2=\"" << fmt_float(cmd.to.y.raw())
            << "\" stroke=\"" << fmt_color(cmd.color)
            << "\" stroke-width=\"" << fmt_float(cmd.thickness) << "\"/>\n";
    }

    void emit(const Polyline& cmd) {
        out << "<polyline points=\"";
        for (size_t i = 0; i < cmd.points.size(); ++i) {
            if (i > 0) out << " ";
            out << fmt_float(cmd.points[i].x.raw()) << ","
                << fmt_float(cmd.points[i].y.raw());
        }
        out << "\" fill=\"none\" stroke=\"" << fmt_color(cmd.color)
            << "\" stroke-width=\"" << fmt_float(cmd.thickness) << "\"/>\n";
    }

    void emit(const Circle& cmd) {
        out << "<circle cx=\"" << fmt_float(cmd.center.x.raw())
            << "\" cy=\"" << fmt_float(cmd.center.y.raw())
            << "\" r=\"" << fmt_float(cmd.radius) << "\"";
        if (cmd.thickness > 0.f) {
            out << " fill=\"none\" stroke=\"" << fmt_color(cmd.color)
                << "\" stroke-width=\"" << fmt_float(cmd.thickness) << "\"";
        } else {
            out << " fill=\"" << fmt_color(cmd.color) << "\"";
        }
        out << "/>\n";
    }

    void emit(const ClipPush& cmd) {
        int id = clip_id++;
        out << "<clipPath id=\"clip-" << id << "\"><rect x=\""
            << fmt_float(cmd.rect.origin.x.raw())
            << "\" y=\"" << fmt_float(cmd.rect.origin.y.raw())
            << "\" width=\"" << fmt_float(cmd.rect.extent.w.raw())
            << "\" height=\"" << fmt_float(cmd.rect.extent.h.raw())
            << "\"/></clipPath>\n";
        out << "<g clip-path=\"url(#clip-" << id << ")\">\n";
    }

    void emit(const ClipPop&) {
        out << "</g>\n";
    }

    void emit_draw_list(const DrawList& dl) {
        for (const auto& cmd : dl.commands)
            std::visit([this](const auto& c) { emit(c); }, cmd);
    }
};

} // namespace detail

inline std::string to_svg(const DrawList& dl) {
    auto bb = dl.bounding_box();
    detail::SvgEmitter e;
    e.out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""
          << detail::fmt_float(bb.origin.x.raw()) << " "
          << detail::fmt_float(bb.origin.y.raw()) << " "
          << detail::fmt_float(bb.extent.w.raw()) << " "
          << detail::fmt_float(bb.extent.h.raw()) << "\">\n";
    e.emit_draw_list(dl);
    e.out << "</svg>\n";
    return e.out.str();
}

inline std::string to_svg(const SceneSnapshot& snap) {
    // Compute viewBox from union of all geometry
    float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    bool first = true;
    for (const auto& [id, rect] : snap.geometry) {
        float rx = rect.origin.x.raw(), ry = rect.origin.y.raw();
        float rw = rect.extent.w.raw(), rh = rect.extent.h.raw();
        if (first) {
            min_x = rx; min_y = ry;
            max_x = rx + rw; max_y = ry + rh;
            first = false;
        } else {
            if (rx < min_x) min_x = rx;
            if (ry < min_y) min_y = ry;
            if (rx + rw > max_x) max_x = rx + rw;
            if (ry + rh > max_y) max_y = ry + rh;
        }
    }

    detail::SvgEmitter e;
    e.out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""
          << detail::fmt_float(min_x) << " " << detail::fmt_float(min_y) << " "
          << detail::fmt_float(max_x - min_x) << " " << detail::fmt_float(max_y - min_y)
          << "\">\n";

    for (uint16_t idx : snap.z_order)
        e.emit_draw_list(snap.draw_lists[idx]);

    if (!snap.overlay.empty())
        e.emit_draw_list(snap.overlay);

    e.out << "</svg>\n";
    return e.out.str();
}

} // namespace prism
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson test test_svg_export -C builddir --print-errorlogs`
Expected: ALL PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/svg_export.hpp tests/test_svg_export.cpp tests/meson.build
git commit -m "feat(svg): add header-only SVG export for DrawList and SceneSnapshot"
```

---

### Task 6: SDL backend support for new commands

**Files:**
- Modify: `include/prism/backends/sdl_window.hpp:80-84`
- Modify: `src/backends/sdl_window.cpp:200-260`

- [ ] **Step 1: Add render_cmd declarations**

In `include/prism/backends/sdl_window.hpp`, add after the `render_cmd(const ClipPop&)` declaration (line 84):

```cpp
    void render_cmd(const RoundedRect& cmd);
    void render_cmd(const Line& cmd);
    void render_cmd(const Polyline& cmd);
    void render_cmd(const Circle& cmd);
```

- [ ] **Step 2: Update render_draw_list visitor**

In `src/backends/sdl_window.cpp`, the current visitor (lines 201-208) uses `if constexpr` to special-case `TextCmd`. The new commands don't need `font`, so the existing pattern works — they'll hit the `else` branch which calls `render_cmd(c)`. No change needed here.

- [ ] **Step 3: Implement render_cmd overloads**

In `src/backends/sdl_window.cpp`, add after the `render_cmd(const ClipPop&)` implementation (after line 259):

```cpp
void SdlWindow::render_cmd(const RoundedRect& cmd) {
    SDL_SetRenderDrawColor(renderer_, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    SDL_FRect r = to_sdl(cmd.rect);
    // SDL3 doesn't have a built-in rounded rect — fall back to regular rect
    if (cmd.thickness > 0.f)
        SDL_RenderRect(renderer_, &r);
    else
        SDL_RenderFillRect(renderer_, &r);
}

void SdlWindow::render_cmd(const Line& cmd) {
    SDL_SetRenderDrawColor(renderer_, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    SDL_RenderLine(renderer_, cmd.from.x.raw(), cmd.from.y.raw(),
                   cmd.to.x.raw(), cmd.to.y.raw());
}

void SdlWindow::render_cmd(const Polyline& cmd) {
    if (cmd.points.size() < 2) return;
    SDL_SetRenderDrawColor(renderer_, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    std::vector<SDL_FPoint> sdl_pts(cmd.points.size());
    for (size_t i = 0; i < cmd.points.size(); ++i)
        sdl_pts[i] = {cmd.points[i].x.raw(), cmd.points[i].y.raw()};
    SDL_RenderLines(renderer_, sdl_pts.data(), static_cast<int>(sdl_pts.size()));
}

void SdlWindow::render_cmd(const Circle& cmd) {
    SDL_SetRenderDrawColor(renderer_, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    // Midpoint circle algorithm
    float cx = cmd.center.x.raw(), cy = cmd.center.y.raw();
    int r = static_cast<int>(cmd.radius);
    int x = r, y = 0, d = 1 - r;
    while (x >= y) {
        if (cmd.thickness > 0.f) {
            // Stroke: draw 8 symmetric points
            SDL_RenderPoint(renderer_, cx + x, cy + y);
            SDL_RenderPoint(renderer_, cx - x, cy + y);
            SDL_RenderPoint(renderer_, cx + x, cy - y);
            SDL_RenderPoint(renderer_, cx - x, cy - y);
            SDL_RenderPoint(renderer_, cx + y, cy + x);
            SDL_RenderPoint(renderer_, cx - y, cy + x);
            SDL_RenderPoint(renderer_, cx + y, cy - x);
            SDL_RenderPoint(renderer_, cx - y, cy - x);
        } else {
            // Fill: draw horizontal spans
            SDL_RenderLine(renderer_, cx - x, cy + y, cx + x, cy + y);
            SDL_RenderLine(renderer_, cx - x, cy - y, cx + x, cy - y);
            SDL_RenderLine(renderer_, cx - y, cy + x, cx + y, cy + x);
            SDL_RenderLine(renderer_, cx - y, cy - x, cx + y, cy - x);
        }
        ++y;
        if (d < 0) {
            d += 2 * y + 1;
        } else {
            --x;
            d += 2 * (y - x) + 1;
        }
    }
}
```

- [ ] **Step 4: Build to verify compilation**

Run: `meson compile -C builddir`
Expected: compiles without errors

- [ ] **Step 5: Commit**

```bash
git add include/prism/backends/sdl_window.hpp src/backends/sdl_window.cpp
git commit -m "feat(sdl): render RoundedRect, Line, Polyline, Circle commands"
```

---

### Task 7: Run full test suite

- [ ] **Step 1: Run all tests**

Run: `meson test -C builddir --print-errorlogs`
Expected: ALL PASS

- [ ] **Step 2: Fix any failures and recommit if needed**
