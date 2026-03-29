# clip_push Implicit Local Coordinates — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `clip_push` establish a local coordinate system so `{0,0}` inside a clip means top-left of the clipped region, eliminating manual offset bugs.

**Architecture:** DrawList gains a private `origin_stack_` (`std::vector<Point>`). `clip_push(Point origin, Size extent)` pushes the cumulative offset; all draw methods apply the offset before appending commands. Backend receives absolute coordinates — no backend changes needed.

**Tech Stack:** C++26, doctest, Meson

**Spec:** `docs/superpowers/specs/2026-03-29-clip-local-coords-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `include/prism/core/draw_list.hpp` | Modify | Add `Size` type, `origin_stack_`, change `clip_push` signature, offset all draw methods |
| `tests/test_draw_list.cpp` | Modify | Add tests for offset behavior, nested clips, no-clip passthrough |
| `include/prism/core/widget_tree.hpp` | Modify | Migrate `text_field_record` and `text_area_record` to use local coordinates |
| `tests/test_text_field.cpp` | Modify | Update clip_push test to use new signature check |
| `tests/test_password.cpp` | Modify | Update clip_push test to use new signature check |
| `tests/test_text_area.cpp` | Modify | Update clip_push test to use new signature check |

---

### Task 1: Add `Size` type and change `clip_push` signature with offset stack

**Files:**
- Modify: `include/prism/core/draw_list.hpp`
- Modify: `tests/test_draw_list.cpp`

- [ ] **Step 1: Write failing tests for the new clip_push offset behavior**

Add these test cases to `tests/test_draw_list.cpp`:

```cpp
TEST_CASE("clip_push offsets filled_rect coordinates") {
    prism::DrawList dl;
    dl.clip_push({10.f, 20.f}, {80.f, 40.f});
    dl.filled_rect({0, 0, 30, 15}, prism::Color::rgba(255, 0, 0));
    dl.clip_pop();

    // ClipPush should have absolute rect {10,20,80,40}
    auto& clip = std::get<prism::ClipPush>(dl.commands[0]);
    CHECK(clip.rect.x == 10.f);
    CHECK(clip.rect.y == 20.f);
    CHECK(clip.rect.w == 80.f);
    CHECK(clip.rect.h == 40.f);

    // FilledRect at {0,0} inside clip should be stored as {10,20}
    auto& fr = std::get<prism::FilledRect>(dl.commands[1]);
    CHECK(fr.rect.x == 10.f);
    CHECK(fr.rect.y == 20.f);
    CHECK(fr.rect.w == 30.f);
    CHECK(fr.rect.h == 15.f);
}

TEST_CASE("clip_push offsets text origin") {
    prism::DrawList dl;
    dl.clip_push({5.f, 10.f}, {100.f, 50.f});
    dl.text("hi", {0, 0}, 14.f, prism::Color::rgba(0, 0, 0));
    dl.clip_pop();

    auto& t = std::get<prism::TextCmd>(dl.commands[1]);
    CHECK(t.origin.x == 5.f);
    CHECK(t.origin.y == 10.f);
}

TEST_CASE("clip_push offsets rect_outline") {
    prism::DrawList dl;
    dl.clip_push({3.f, 7.f}, {50.f, 50.f});
    dl.rect_outline({0, 0, 20, 20}, prism::Color::rgba(0, 0, 0), 1.f);
    dl.clip_pop();

    auto& ro = std::get<prism::RectOutline>(dl.commands[1]);
    CHECK(ro.rect.x == 3.f);
    CHECK(ro.rect.y == 7.f);
}

TEST_CASE("nested clip_push offsets compose additively") {
    prism::DrawList dl;
    dl.clip_push({10.f, 10.f}, {100.f, 100.f});
    dl.clip_push({5.f, 5.f}, {80.f, 80.f});
    dl.filled_rect({0, 0, 10, 10}, prism::Color::rgba(255, 0, 0));
    dl.clip_pop();
    dl.filled_rect({0, 0, 10, 10}, prism::Color::rgba(0, 255, 0));
    dl.clip_pop();

    // Inner clip absolute origin: {10+5, 10+5} = {15, 15}
    auto& inner_clip = std::get<prism::ClipPush>(dl.commands[1]);
    CHECK(inner_clip.rect.x == 15.f);
    CHECK(inner_clip.rect.y == 15.f);

    // FilledRect inside inner clip: offset = {15, 15}
    auto& inner_rect = std::get<prism::FilledRect>(dl.commands[2]);
    CHECK(inner_rect.rect.x == 15.f);
    CHECK(inner_rect.rect.y == 15.f);

    // After inner clip_pop, offset back to {10, 10}
    auto& outer_rect = std::get<prism::FilledRect>(dl.commands[4]);
    CHECK(outer_rect.rect.x == 10.f);
    CHECK(outer_rect.rect.y == 10.f);
}

TEST_CASE("no clip_push means no offset") {
    prism::DrawList dl;
    dl.filled_rect({5, 5, 10, 10}, prism::Color::rgba(255, 0, 0));
    dl.text("hi", {3, 7}, 14.f, prism::Color::rgba(0, 0, 0));

    auto& fr = std::get<prism::FilledRect>(dl.commands[0]);
    CHECK(fr.rect.x == 5.f);
    CHECK(fr.rect.y == 5.f);

    auto& t = std::get<prism::TextCmd>(dl.commands[1]);
    CHECK(t.origin.x == 3.f);
    CHECK(t.origin.y == 7.f);
}

TEST_CASE("clip_pop restores previous offset") {
    prism::DrawList dl;
    dl.clip_push({10.f, 10.f}, {100.f, 100.f});
    dl.text("inside", {0, 0}, 14.f, prism::Color::rgba(0, 0, 0));
    dl.clip_pop();
    dl.text("outside", {0, 0}, 14.f, prism::Color::rgba(0, 0, 0));

    auto& inside = std::get<prism::TextCmd>(dl.commands[1]);
    CHECK(inside.origin.x == 10.f);
    CHECK(inside.origin.y == 10.f);

    auto& outside = std::get<prism::TextCmd>(dl.commands[3]);
    CHECK(outside.origin.x == 0.f);
    CHECK(outside.origin.y == 0.f);
}
```

Also update the existing `"DrawList clip push/pop"` test to use the new signature:

```cpp
TEST_CASE("DrawList clip push/pop")
{
    prism::DrawList dl;
    dl.clip_push({10.f, 10.f}, {80.f, 40.f});
    dl.filled_rect({10, 10, 60, 20}, prism::Color::rgba(0, 255, 0));
    dl.clip_pop();

    CHECK(dl.size() == 3);
    CHECK(std::holds_alternative<prism::ClipPush>(dl.commands[0]));
    CHECK(std::holds_alternative<prism::ClipPop>(dl.commands[2]));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson test -C builddir draw_list --verbose`
Expected: Compilation error — `clip_push` doesn't accept `Point, Size` yet.

- [ ] **Step 3: Implement Size type, origin_stack_, and new clip_push**

In `include/prism/core/draw_list.hpp`, add `Size` after `Point`:

```cpp
struct Size {
    float w, h;
};
```

Replace the `DrawList` struct with:

```cpp
struct DrawList {
    std::vector<DrawCmd> commands;

    void filled_rect(Rect r, Color c)
    {
        auto o = current_offset();
        commands.emplace_back(FilledRect{{r.x + o.x, r.y + o.y, r.w, r.h}, c});
    }

    void rect_outline(Rect r, Color c, float thickness = 1.0f)
    {
        auto o = current_offset();
        commands.emplace_back(RectOutline{{r.x + o.x, r.y + o.y, r.w, r.h}, c, thickness});
    }

    void text(std::string s, Point origin, float size, Color c)
    {
        auto o = current_offset();
        commands.emplace_back(
            TextCmd{std::move(s), {origin.x + o.x, origin.y + o.y}, size, c});
    }

    void clip_push(Point origin, Size extent)
    {
        auto o = current_offset();
        float abs_x = o.x + origin.x;
        float abs_y = o.y + origin.y;
        origin_stack_.push_back({abs_x, abs_y});
        commands.emplace_back(ClipPush{{abs_x, abs_y, extent.w, extent.h}});
    }

    void clip_pop()
    {
        if (!origin_stack_.empty()) origin_stack_.pop_back();
        commands.emplace_back(ClipPop{});
    }

    void clear()
    {
        commands.clear();
        origin_stack_.clear();
    }

    [[nodiscard]] bool empty() const { return commands.empty(); }
    [[nodiscard]] std::size_t size() const { return commands.size(); }

    [[nodiscard]] Rect bounding_box() const {
        if (commands.empty()) return {0, 0, 0, 0};
        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float max_y = std::numeric_limits<float>::lowest();
        auto expand = [&](Rect r) {
            min_x = std::min(min_x, r.x);
            min_y = std::min(min_y, r.y);
            max_x = std::max(max_x, r.x + r.w);
            max_y = std::max(max_y, r.y + r.h);
        };
        for (const auto& cmd : commands) {
            std::visit([&](const auto& c) {
                if constexpr (requires { c.rect; })
                    expand(c.rect);
                else if constexpr (requires { c.origin; })
                    expand({c.origin.x, c.origin.y, 0, c.size});
            }, cmd);
        }
        return {min_x, min_y, max_x - min_x, max_y - min_y};
    }

  private:
    std::vector<Point> origin_stack_;

    [[nodiscard]] Point current_offset() const
    {
        return origin_stack_.empty() ? Point{0.f, 0.f} : origin_stack_.back();
    }
};
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson test -C builddir draw_list --verbose`
Expected: All tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/draw_list.hpp tests/test_draw_list.cpp
git commit -m "feat: clip_push establishes local coordinate system with implicit offset

Add Size type. clip_push(Point, Size) pushes origin onto offset stack.
All draw methods apply cumulative offset before appending commands.
Backend receives absolute coordinates — no backend changes needed."
```

---

### Task 2: Migrate TextField/Password delegate to local coordinates

**Files:**
- Modify: `include/prism/core/widget_tree.hpp:80-112` (function `detail::text_field_record`)
- Modify: `tests/test_text_field.cpp` (clip_push test)
- Modify: `tests/test_password.cpp` (clip_push test)

- [ ] **Step 1: Run existing TextField and Password tests to establish baseline**

Run: `meson test -C builddir text_field password --verbose`
Expected: Compilation error — `clip_push` no longer accepts `Rect`.

- [ ] **Step 2: Migrate text_field_record in widget_tree.hpp**

In `include/prism/core/widget_tree.hpp`, in function `detail::text_field_record`, change the clip_push call and remove manual `tf_padding` offsets from coordinates inside the clip region.

Replace this block (lines ~93-111):

```cpp
    float text_area_w = tf_widget_w - 2 * tf_padding;
    dl.clip_push({tf_padding, 0, text_area_w, tf_widget_h});

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, {tf_padding, tf_padding + 2}, tf_font_size,
                Color::rgba(120, 120, 130));
    } else {
        float text_x = tf_padding - es.scroll_offset;
        std::string display_text = display_fn(std::string(sf.value.data(), sf.value.size()));
        dl.text(display_text, {text_x, tf_padding + 2}, tf_font_size,
                Color::rgba(220, 220, 220));
    }

    if (vs.focused) {
        float cursor_x = tf_padding + es.cursor * cw - es.scroll_offset;
        dl.filled_rect({cursor_x, tf_padding, tf_cursor_w, tf_widget_h - 2 * tf_padding},
                       Color::rgba(220, 220, 240));
    }

    dl.clip_pop();
```

With:

```cpp
    float text_area_w = tf_widget_w - 2 * tf_padding;
    dl.clip_push({tf_padding, 0}, {text_area_w, tf_widget_h});

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, {0, tf_padding + 2}, tf_font_size,
                Color::rgba(120, 120, 130));
    } else {
        float text_x = -es.scroll_offset;
        std::string display_text = display_fn(std::string(sf.value.data(), sf.value.size()));
        dl.text(display_text, {text_x, tf_padding + 2}, tf_font_size,
                Color::rgba(220, 220, 220));
    }

    if (vs.focused) {
        float cursor_x = es.cursor * cw - es.scroll_offset;
        dl.filled_rect({cursor_x, tf_padding, tf_cursor_w, tf_widget_h - 2 * tf_padding},
                       Color::rgba(220, 220, 240));
    }

    dl.clip_pop();
```

Changes:
- `clip_push({tf_padding, 0, text_area_w, tf_widget_h})` → `clip_push({tf_padding, 0}, {text_area_w, tf_widget_h})`
- Placeholder x: `{tf_padding, ...}` → `{0, ...}`
- text_x: `tf_padding - es.scroll_offset` → `-es.scroll_offset`
- cursor_x: `tf_padding + es.cursor * cw - es.scroll_offset` → `es.cursor * cw - es.scroll_offset`

- [ ] **Step 3: Run TextField and Password tests**

Run: `meson test -C builddir text_field password --verbose`
Expected: All tests PASS. Existing tests only check for ClipPush/ClipPop variant presence, not coordinate values — the absolute coordinates stored in commands are identical because the offset is applied by DrawList.

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/widget_tree.hpp
git commit -m "refactor: migrate TextField/Password to clip_push local coordinates

Remove manual tf_padding offsets from text, placeholder, and cursor x
positions inside the clipped region. clip_push now handles the offset."
```

---

### Task 3: Migrate TextArea delegate to local coordinates

**Files:**
- Modify: `include/prism/core/widget_tree.hpp:278-304` (function `detail::text_area_record`)
- Modify: `tests/test_text_area.cpp` (clip_push test)

- [ ] **Step 1: Run existing TextArea tests to establish baseline**

Run: `meson test -C builddir text_area --verbose`
Expected: Compilation error — `clip_push` no longer accepts `Rect`.

- [ ] **Step 2: Migrate text_area_record in widget_tree.hpp**

In `include/prism/core/widget_tree.hpp`, in function `detail::text_area_record`, change the clip_push call and remove manual `ta_padding` offsets from coordinates inside the clip region.

Replace this block (lines ~278-304):

```cpp
    dl.clip_push({ta_padding, ta_padding, text_area_w, text_area_h});

    auto wrapped = wrap_lines(std::string_view(sf.value.data(), sf.value.size()),
                              text_area_w, cw);

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, {ta_padding, 2}, ta_font_size, Color::rgba(120, 120, 130));
    } else {
        for (size_t i = 0; i < wrapped.size(); ++i) {
            float y = i * ta_line_height - es.scroll_y;
            if (y + ta_line_height < 0) continue;
            if (y > text_area_h) break;
            if (wrapped[i].length > 0) {
                std::string line_text(sf.value.data() + wrapped[i].start, wrapped[i].length);
                dl.text(line_text, {ta_padding, y + 2}, ta_font_size, Color::rgba(220, 220, 220));
            }
        }
    }

    if (vs.focused) {
        auto [line, col] = cursor_to_line_col(es.cursor, wrapped);
        float cx = ta_padding + col * cw;
        float cy = line * ta_line_height - es.scroll_y;
        dl.filled_rect({cx, cy, ta_cursor_w, ta_line_height}, Color::rgba(220, 220, 240));
    }

    dl.clip_pop();
```

With:

```cpp
    dl.clip_push({ta_padding, ta_padding}, {text_area_w, text_area_h});

    auto wrapped = wrap_lines(std::string_view(sf.value.data(), sf.value.size()),
                              text_area_w, cw);

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, {0, 2}, ta_font_size, Color::rgba(120, 120, 130));
    } else {
        for (size_t i = 0; i < wrapped.size(); ++i) {
            float y = i * ta_line_height - es.scroll_y;
            if (y + ta_line_height < 0) continue;
            if (y > text_area_h) break;
            if (wrapped[i].length > 0) {
                std::string line_text(sf.value.data() + wrapped[i].start, wrapped[i].length);
                dl.text(line_text, {0, y + 2}, ta_font_size, Color::rgba(220, 220, 220));
            }
        }
    }

    if (vs.focused) {
        auto [line, col] = cursor_to_line_col(es.cursor, wrapped);
        float cx = col * cw;
        float cy = line * ta_line_height - es.scroll_y;
        dl.filled_rect({cx, cy, ta_cursor_w, ta_line_height}, Color::rgba(220, 220, 240));
    }

    dl.clip_pop();
```

Changes:
- `clip_push({ta_padding, ta_padding, text_area_w, text_area_h})` → `clip_push({ta_padding, ta_padding}, {text_area_w, text_area_h})`
- Placeholder x: `{ta_padding, 2}` → `{0, 2}`
- Line text x: `{ta_padding, y + 2}` → `{0, y + 2}`
- Cursor cx: `ta_padding + col * cw` → `col * cw`

- [ ] **Step 3: Run TextArea tests**

Run: `meson test -C builddir text_area --verbose`
Expected: All tests PASS.

- [ ] **Step 4: Run full test suite to confirm no regressions**

Run: `meson test -C builddir --verbose`
Expected: All tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp
git commit -m "refactor: migrate TextArea to clip_push local coordinates

Remove manual ta_padding offsets from text, placeholder, and cursor
positions inside the clipped region. clip_push now handles the offset."
```
