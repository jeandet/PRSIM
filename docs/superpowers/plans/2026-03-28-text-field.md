# TextField Widget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Add a single-line editable text field widget to PRISM, activated via the `TextField<T>` sentinel type.

**Architecture:** New `TextInput` event type forwarding SDL text input. `TextField<T>` sentinel with `Delegate` specialization. Ephemeral cursor state in `std::any edit_state` on `WidgetNode`. All existing delegates updated to receive `WidgetNode&` instead of `WidgetVisualState&`. Monospace `char_width()` helper for cursor positioning.

**Tech Stack:** C++26 with `-freflection`, doctest, SDL3 (`SDL_StartTextInput`, `SDL_EVENT_TEXT_INPUT`), Meson build system.

**Spec:** `docs/superpowers/specs/2026-03-28-text-field-design.md`

---

## File Structure

| File | Role |
|------|------|
| `include/prism/core/input_event.hpp` | Add `TextInput` struct, new key constants, add to `InputEvent` variant |
| `include/prism/core/delegate.hpp` | Add `TextField<T>` sentinel, `TextEditState`, `TextEditable` concept, `char_width()`, `Delegate<TextField<T>>`. Change all delegate signatures to `WidgetNode&`. |
| `include/prism/core/widget_tree.hpp` | Add `std::any edit_state` to `WidgetNode`. Update `build_leaf` to pass `node` to delegates. |
| `include/prism/core/model_app.hpp` | Forward `TextInput` and additional `KeyPress` events to focused widget. |
| `src/backends/software_backend.cpp` | Call `SDL_StartTextInput()`, forward `SDL_EVENT_TEXT_INPUT`. |
| `tests/test_delegate.cpp` | Update existing tests for `WidgetNode&` signature. |
| `tests/test_text_field.cpp` | New test file for TextField delegate. |
| `tests/meson.build` | Register `test_text_field.cpp`. |
| `examples/model_dashboard.cpp` | Add `Field<TextField<>>` demo field. |

---

### Task 1: Add `TextInput` event and new key constants

**Files:**
- Modify: `include/prism/core/input_event.hpp`
- Modify: `tests/test_input_event.cpp`

- [x] **Step 1: Write test for new event types and key constants**

Add to `tests/test_input_event.cpp`:

```cpp
TEST_CASE("TextInput event holds text") {
    prism::InputEvent ev = prism::TextInput{"abc"};
    auto* ti = std::get_if<prism::TextInput>(&ev);
    REQUIRE(ti != nullptr);
    CHECK(ti->text == "abc");
}

TEST_CASE("Key constants match SDL keycodes") {
    CHECK(prism::keys::backspace == 0x08);
    CHECK(prism::keys::delete_   == 0x7F);
    CHECK(prism::keys::right     == 0x4000'004F);
    CHECK(prism::keys::left      == 0x4000'0050);
    CHECK(prism::keys::home      == 0x4000'004A);
    CHECK(prism::keys::end       == 0x4000'004D);
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `meson test input_event -C builddir -v`
Expected: FAIL — `TextInput` not defined, key constants not defined.

- [x] **Step 3: Add `TextInput` struct and key constants to `input_event.hpp`**

In `include/prism/core/input_event.hpp`, add `TextInput` struct before the `InputEvent` variant, add it to the variant, and add key constants:

```cpp
struct TextInput  { std::string text; };
```

Update the variant:

```cpp
using InputEvent = std::variant<
    MouseMove, MouseButton, MouseScroll,
    KeyPress, KeyRelease, TextInput,
    WindowResize, WindowClose
>;
```

Add to the `keys` namespace:

```cpp
inline constexpr int32_t backspace = 0x08;       // SDLK_BACKSPACE
inline constexpr int32_t delete_   = 0x7F;       // SDLK_DELETE
inline constexpr int32_t right     = 0x4000'004F; // SDLK_RIGHT
inline constexpr int32_t left      = 0x4000'0050; // SDLK_LEFT
inline constexpr int32_t home      = 0x4000'004A; // SDLK_HOME
inline constexpr int32_t end       = 0x4000'004D; // SDLK_END
```

- [x] **Step 4: Run test to verify it passes**

Run: `meson test input_event -C builddir -v`
Expected: PASS

- [x] **Step 5: Run full test suite to check for breakage**

Run: `meson test -C builddir`
Expected: All tests pass. The new variant member may require updates if any code does exhaustive `std::visit` — check and fix if needed.

- [x] **Step 6: Commit**

```bash
git add include/prism/core/input_event.hpp tests/test_input_event.cpp
git commit -m "feat: add TextInput event and navigation key constants"
```

---

### Task 2: Change delegate signatures from `WidgetVisualState&` to `WidgetNode&`

This is a mechanical refactor that must happen before adding `edit_state` to `WidgetNode`, because all delegates need the new signature.

**Files:**
- Modify: `include/prism/core/delegate.hpp`
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_delegate.cpp`

- [x] **Step 1: Add forward declaration and `std::any` to `WidgetNode`**

In `include/prism/core/widget_tree.hpp`, add `#include <any>` to the includes, and add to `WidgetNode`:

```cpp
std::any edit_state;
```

Place it after the `visual_state` member.

- [x] **Step 2: Add forward declaration of `WidgetNode` in `delegate.hpp`**

In `include/prism/core/delegate.hpp`, add before the `Delegate` template:

```cpp
struct WidgetNode;  // forward-declared; defined in widget_tree.hpp
```

Note: `delegate.hpp` cannot include `widget_tree.hpp` (circular dependency). The forward declaration is sufficient because delegates take `WidgetNode&` (reference, not value).

- [x] **Step 3: Update all delegate signatures**

In `include/prism/core/delegate.hpp`, change every `record` and `handle_input` signature.

**Primary template:**
```cpp
static void record(DrawList& dl, const Field<T>&, const WidgetNode& node) {
    auto bg = node.visual_state.hovered ? Color::rgba(60, 60, 72) : Color::rgba(50, 50, 60);
    dl.filled_rect({0, 0, 200, 30}, bg);
}

static void handle_input(Field<T>&, const InputEvent&, WidgetNode&) {}
```

**StringLike:**
```cpp
static void record(DrawList& dl, const Field<T>& field, const WidgetNode& node) {
    auto bg = node.visual_state.hovered ? Color::rgba(60, 60, 72) : Color::rgba(50, 50, 60);
    dl.filled_rect({0, 0, 200, 30}, bg);
    dl.text(std::string(field.get().data(), field.get().size()),
            {4, 4}, 14, Color::rgba(220, 220, 220));
}

static void handle_input(Field<T>&, const InputEvent&, WidgetNode&) {}
```

**bool:**
```cpp
static void record(DrawList& dl, const Field<bool>& field, const WidgetNode& node) {
    auto& vs = node.visual_state;
    Color bg;
    if (field.get()) {
        bg = vs.pressed ? Color::rgba(0, 100, 65)
           : vs.hovered ? Color::rgba(0, 140, 95)
           : Color::rgba(0, 120, 80);
    } else {
        bg = vs.pressed ? Color::rgba(40, 40, 48)
           : vs.hovered ? Color::rgba(60, 60, 72)
           : Color::rgba(50, 50, 60);
    }
    dl.filled_rect({0, 0, 200, 30}, bg);
    if (vs.focused)
        dl.rect_outline({-1, -1, 202, 32}, Color::rgba(80, 160, 240), 2.0f);
}

static void handle_input(Field<bool>& field, const InputEvent& ev, WidgetNode&) {
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        field.set(!field.get());
    } else if (auto* kp = std::get_if<KeyPress>(&ev);
               kp && (kp->key == keys::space || kp->key == keys::enter)) {
        field.set(!field.get());
    }
}
```

**Label<T>:**
```cpp
static void record(DrawList& dl, const Field<Label<T>>& field, const WidgetNode&) {
    dl.filled_rect({0, 0, 200, 24}, Color::rgba(40, 40, 48));
    dl.text(std::string(field.get().value.data(), field.get().value.size()),
            {4, 4}, 14, Color::rgba(180, 180, 190));
}

static void handle_input(Field<Label<T>>&, const InputEvent&, WidgetNode&) {}
```

**Slider<T>:**
```cpp
static void record(DrawList& dl, const Field<Slider<T>>& field, const WidgetNode& node) {
    auto& vs = node.visual_state;
    auto& s = field.get();
    float r = ratio(s);
    float track_y = (widget_h - track_h) / 2.f;

    auto track_bg = vs.hovered ? Color::rgba(70, 70, 82) : Color::rgba(60, 60, 70);
    dl.filled_rect({0, track_y, track_w, track_h}, track_bg);

    auto thumb_color = vs.pressed ? Color::rgba(0, 120, 180)
                     : vs.hovered ? Color::rgba(0, 160, 220)
                     : Color::rgba(0, 140, 200);
    float thumb_x = r * (track_w - thumb_w);
    dl.filled_rect({thumb_x, 0, thumb_w, widget_h}, thumb_color);
    if (vs.focused)
        dl.rect_outline({-1, -1, track_w + 2, widget_h + 2}, Color::rgba(80, 160, 240), 2.0f);
}

static void handle_input(Field<Slider<T>>& field, const InputEvent& ev, WidgetNode&) {
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        float t = std::clamp(mb->position.x / track_w, 0.f, 1.f);
        auto& s = field.get();
        T raw = static_cast<T>(s.min + t * (s.max - s.min));
        Slider<T> updated = s;
        if (s.step != T{0}) {
            T steps = static_cast<T>((raw - s.min + s.step / T{2}) / s.step);
            updated.value = std::clamp(static_cast<T>(s.min + steps * s.step), s.min, s.max);
        } else {
            updated.value = raw;
        }
        field.set(updated);
    }
}
```

**Button:**
```cpp
static void record(DrawList& dl, const Field<Button>& field, const WidgetNode& node) {
    auto& vs = node.visual_state;
    Color bg = vs.pressed ? Color::rgba(30, 90, 160)
             : vs.hovered ? Color::rgba(50, 120, 200)
             : Color::rgba(40, 105, 180);
    dl.filled_rect({0, 0, 200, 32}, bg);
    dl.rect_outline({0, 0, 200, 32}, Color::rgba(60, 140, 220), 1.0f);
    dl.text(field.get().text, {8, 7}, 14, Color::rgba(240, 240, 240));
    if (vs.focused)
        dl.rect_outline({-2, -2, 204, 36}, Color::rgba(80, 160, 240), 2.0f);
}

static void handle_input(Field<Button>& field, const InputEvent& ev, WidgetNode&) {
    bool activate = false;
    if (auto* mb = std::get_if<MouseButton>(&ev))
        activate = mb->pressed;
    else if (auto* kp = std::get_if<KeyPress>(&ev))
        activate = (kp->key == keys::space || kp->key == keys::enter);

    if (activate) {
        auto btn = field.get();
        btn.click_count++;
        field.set(btn);
    }
}
```

- [x] **Step 4: Update `build_leaf` in `widget_tree.hpp`**

In `include/prism/core/widget_tree.hpp`, update the `build_leaf` method. Change the `record` lambda (line 246-249):

```cpp
node.record = [&field](WidgetNode& n) {
    n.draws.clear();
    Delegate<T>::record(n.draws, field, n);
};
```

And the `wire` lambda (line 252-257):

```cpp
node.wire = [&field](WidgetNode& n) {
    n.connections.push_back(
        n.on_input.connect([&field, &n](const InputEvent& ev) {
            Delegate<T>::handle_input(field, ev, n);
        })
    );
};
```

- [x] **Step 5: Update test_delegate.cpp**

Every test that calls `Delegate<T>::record()` or `Delegate<T>::handle_input()` now needs a `WidgetNode` instead of `WidgetVisualState`. Add at the top of `tests/test_delegate.cpp`:

```cpp
#include <prism/core/widget_tree.hpp>
```

Then create a helper to build a minimal `WidgetNode` with a given `WidgetVisualState`:

```cpp
namespace {
prism::WidgetNode make_node(prism::WidgetVisualState vs = {}) {
    prism::WidgetNode node;
    node.visual_state = vs;
    return node;
}
}
```

Replace every test. The pattern is:

Before:
```cpp
prism::WidgetVisualState vs;
prism::Delegate<bool>::record(dl, field, vs);
```

After:
```cpp
auto node = make_node();
prism::Delegate<bool>::record(dl, field, node);
```

And for `handle_input`:

Before:
```cpp
prism::WidgetVisualState vs;
prism::Delegate<bool>::handle_input(field, ev, vs);
```

After:
```cpp
auto node = make_node();
prism::Delegate<bool>::handle_input(field, ev, node);
```

For tests that set specific visual state:

Before:
```cpp
prism::WidgetVisualState hovered{.hovered = true};
prism::Delegate<bool>::record(dl, field, hovered);
```

After:
```cpp
auto node = make_node({.hovered = true});
prism::Delegate<bool>::record(dl, field, node);
```

Apply this transformation to **all** tests in the file (every `record()` and `handle_input()` call).

- [x] **Step 6: Run tests to verify everything passes**

Run: `meson test -C builddir`
Expected: All tests pass. This is a signature-only refactor — behavior is unchanged.

- [x] **Step 7: Commit**

```bash
git add include/prism/core/delegate.hpp include/prism/core/widget_tree.hpp tests/test_delegate.cpp
git commit -m "refactor: delegate signatures take WidgetNode& instead of WidgetVisualState&

Prepares for text_field edit_state stored on WidgetNode."
```

---

### Task 3: Add `TextField<T>` sentinel, `TextEditable` concept, and `char_width()`

**Files:**
- Modify: `include/prism/core/delegate.hpp`
- Create: `tests/test_text_field.cpp`
- Modify: `tests/meson.build`

- [x] **Step 1: Register test file in meson**

In `tests/meson.build`, add `'text_field'` to the `headless_tests` dict:

```
'text_field' : files('test_text_field.cpp'),
```

- [x] **Step 2: Write initial test for TextField sentinel type and concept**

Create `tests/test_text_field.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/widget_tree.hpp>

TEST_CASE("TextField default-constructs with empty value") {
    prism::TextField<> tf;
    CHECK(tf.value.empty());
    CHECK(tf.placeholder.empty());
    CHECK(tf.max_length == 0);
}

TEST_CASE("TextField equality comparison") {
    prism::TextField<> a{.value = "hello"};
    prism::TextField<> b{.value = "hello"};
    prism::TextField<> c{.value = "world"};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("TextEditable concept matches TextField") {
    static_assert(prism::TextEditable<prism::TextField<>>);
    static_assert(!prism::TextEditable<std::string>);
    static_assert(!prism::TextEditable<int>);
}

TEST_CASE("char_width returns positive value") {
    CHECK(prism::char_width(14.f) > 0.f);
    CHECK(prism::char_width(14.f) == doctest::Approx(0.6f * 14.f));
}

TEST_CASE("Delegate<TextField<>> has tab_and_click focus policy") {
    CHECK(prism::Delegate<prism::TextField<>>::focus_policy == prism::FocusPolicy::tab_and_click);
}
```

- [x] **Step 3: Run test to verify it fails**

Run: `meson test text_field -C builddir -v`
Expected: FAIL — `TextField`, `TextEditable`, `char_width` not defined.

- [x] **Step 4: Add types to `delegate.hpp`**

In `include/prism/core/delegate.hpp`, add after the `Label` sentinel:

```cpp
// Concept: type wrapping a StringLike value (for editable text delegates)
template <typename T>
concept TextEditable = requires(const T& t) {
    { t.value } -> StringLike;
};

// Sentinel: single-line editable text field
template <StringLike T = std::string>
struct TextField {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;
    bool operator==(const TextField&) const = default;
};

// Monospace text measurement — single replacement point for future TextMetrics
inline float char_width(float font_size) { return 0.6f * font_size; }

// Ephemeral cursor state for text editing delegates
struct TextEditState {
    size_t cursor = 0;
    float scroll_offset = 0.f;
};
```

Add a forward declaration of `Delegate<TextField<T>>` (the actual implementation comes in Task 4, but we need at least `focus_policy` for Task 3's test):

```cpp
template <StringLike T>
struct Delegate<TextField<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<TextField<T>>& field, const WidgetNode& node);
    static void handle_input(Field<TextField<T>>& field, const InputEvent& ev, WidgetNode& node);
};
```

- [x] **Step 5: Run test to verify it passes**

Run: `meson test text_field -C builddir -v`
Expected: PASS (linker won't complain because `record` and `handle_input` are not called yet).

- [x] **Step 6: Commit**

```bash
git add include/prism/core/delegate.hpp tests/test_text_field.cpp tests/meson.build
git commit -m "feat: add TextField sentinel type, TextEditable concept, char_width helper"
```

---

### Task 4: Implement `Delegate<TextField<T>>::record()`

**Files:**
- Modify: `include/prism/core/delegate.hpp`
- Modify: `tests/test_text_field.cpp`

- [x] **Step 1: Write rendering tests**

Add to `tests/test_text_field.cpp`:

```cpp
namespace {
prism::WidgetNode make_node(prism::WidgetVisualState vs = {}) {
    prism::WidgetNode node;
    node.visual_state = vs;
    return node;
}
}

TEST_CASE("TextField record produces background and text") {
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::TextField<>>::record(dl, field, node);

    CHECK(dl.size() >= 3);  // background + clip_push + text + clip_pop (minimum)
    bool has_text = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "hello") has_text = true;
        }
    }
    CHECK(has_text);
}

TEST_CASE("TextField record shows placeholder when empty and unfocused") {
    prism::Field<prism::TextField<>> field{{.placeholder = "Enter name"}};
    prism::DrawList dl;
    auto node = make_node();  // unfocused
    prism::Delegate<prism::TextField<>>::record(dl, field, node);

    bool has_placeholder = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Enter name") has_placeholder = true;
        }
    }
    CHECK(has_placeholder);
}

TEST_CASE("TextField record hides placeholder when focused") {
    prism::Field<prism::TextField<>> field{{.placeholder = "Enter name"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::record(dl, field, node);

    bool has_placeholder = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Enter name") has_placeholder = true;
        }
    }
    CHECK_FALSE(has_placeholder);
}

TEST_CASE("TextField record shows cursor when focused") {
    prism::Field<prism::TextField<>> field{{.value = "hi"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::record(dl, field, node);

    // Cursor is a thin FilledRect
    int filled_count = 0;
    for (auto& cmd : dl.commands) {
        if (auto* fr = std::get_if<prism::FilledRect>(&cmd)) {
            if (fr->rect.w <= 3.f) filled_count++;  // cursor is 2px wide
        }
    }
    CHECK(filled_count >= 1);
}

TEST_CASE("TextField record uses clip_push/pop") {
    prism::Field<prism::TextField<>> field{{.value = "test"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::TextField<>>::record(dl, field, node);

    int clips = 0;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::ClipPush>(cmd)) clips++;
        if (std::holds_alternative<prism::ClipPop>(cmd)) clips++;
    }
    CHECK(clips == 2);  // one push + one pop
}

TEST_CASE("TextField record renders focus ring when focused") {
    prism::Field<prism::TextField<>> field{{.value = "test"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::record(dl, field, node);

    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK(has_outline);
}
```

- [x] **Step 2: Run tests to verify they fail**

Run: `meson test text_field -C builddir -v`
Expected: FAIL — linker error, `record` not defined.

- [x] **Step 3: Implement `record()`**

In `include/prism/core/delegate.hpp`, replace the `Delegate<TextField<T>>` declaration with the full implementation:

```cpp
template <StringLike T>
struct Delegate<TextField<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr float widget_w = 200.f;
    static constexpr float widget_h = 30.f;
    static constexpr float padding = 4.f;
    static constexpr float font_size = 14.f;
    static constexpr float cursor_w = 2.f;

    static const TextEditState& get_edit_state(const WidgetNode& node) {
        static const TextEditState default_state;
        if (!node.edit_state.has_value()) return default_state;
        return std::any_cast<const TextEditState&>(node.edit_state);
    }

    static TextEditState& ensure_edit_state(WidgetNode& node) {
        if (!node.edit_state.has_value())
            node.edit_state = TextEditState{};
        return std::any_cast<TextEditState&>(node.edit_state);
    }

    static void record(DrawList& dl, const Field<TextField<T>>& field, const WidgetNode& node) {
        auto& vs = node.visual_state;
        auto& tf = field.get();
        auto& es = get_edit_state(node);
        float cw = char_width(font_size);

        // Background
        auto bg = vs.focused ? Color::rgba(65, 65, 78)
                : vs.hovered ? Color::rgba(55, 55, 68)
                : Color::rgba(45, 45, 55);
        dl.filled_rect({0, 0, widget_w, widget_h}, bg);

        // Border / focus ring
        if (vs.focused)
            dl.rect_outline({-1, -1, widget_w + 2, widget_h + 2},
                            Color::rgba(80, 160, 240), 2.0f);

        // Clip to text area
        float text_area_w = widget_w - 2 * padding;
        dl.clip_push({padding, 0, text_area_w, widget_h});

        if (tf.value.empty() && !vs.focused) {
            // Placeholder
            dl.text(tf.placeholder, {padding, padding + 2}, font_size,
                    Color::rgba(120, 120, 130));
        } else {
            // Value text, offset by scroll
            float text_x = padding - es.scroll_offset;
            std::string text_str(tf.value.data(), tf.value.size());
            dl.text(text_str, {text_x, padding + 2}, font_size,
                    Color::rgba(220, 220, 220));
        }

        // Cursor (only when focused)
        if (vs.focused) {
            float cursor_x = padding + es.cursor * cw - es.scroll_offset;
            dl.filled_rect({cursor_x, padding, cursor_w, widget_h - 2 * padding},
                           Color::rgba(220, 220, 240));
        }

        dl.clip_pop();
    }

    static void handle_input(Field<TextField<T>>& field, const InputEvent& ev, WidgetNode& node);
};
```

- [x] **Step 4: Run tests to verify they pass**

Run: `meson test text_field -C builddir -v`
Expected: PASS (linker may warn about `handle_input` undefined — if so, add a stub: `static void handle_input(Field<TextField<T>>&, const InputEvent&, WidgetNode&) {}` temporarily).

- [x] **Step 5: Commit**

```bash
git add include/prism/core/delegate.hpp tests/test_text_field.cpp
git commit -m "feat: implement TextField record() — background, text, cursor, clip, placeholder"
```

---

### Task 5: Implement `Delegate<TextField<T>>::handle_input()`

**Files:**
- Modify: `include/prism/core/delegate.hpp`
- Modify: `tests/test_text_field.cpp`

- [x] **Step 1: Write input handling tests**

Add to `tests/test_text_field.cpp`:

```cpp
TEST_CASE("TextField TextInput inserts text at cursor") {
    prism::Field<prism::TextField<>> field{{.value = "ab"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 1;  // between 'a' and 'b'

    prism::Delegate<prism::TextField<>>::handle_input(field, prism::TextInput{"X"}, node);
    CHECK(field.get().value == "aXb");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);
}

TEST_CASE("TextField TextInput appends at end") {
    prism::Field<prism::TextField<>> field{{.value = "hi"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(field, prism::TextInput{"!"}, node);
    CHECK(field.get().value == "hi!");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 3);
}

TEST_CASE("TextField backspace deletes char before cursor") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::backspace, 0}, node);
    CHECK(field.get().value == "ac");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 1);
}

TEST_CASE("TextField backspace at position 0 is no-op") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::ensure_edit_state(node);

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::backspace, 0}, node);
    CHECK(field.get().value == "abc");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);
}

TEST_CASE("TextField delete removes char at cursor") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::delete_, 0}, node);
    CHECK(field.get().value == "ac");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 1);
}

TEST_CASE("TextField delete at end is no-op") {
    prism::Field<prism::TextField<>> field{{.value = "ab"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::delete_, 0}, node);
    CHECK(field.get().value == "ab");
}

TEST_CASE("TextField left arrow moves cursor left") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 1);
}

TEST_CASE("TextField left arrow at 0 stays at 0") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::ensure_edit_state(node);

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);
}

TEST_CASE("TextField right arrow moves cursor right") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::right, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);
}

TEST_CASE("TextField right arrow at end stays at end") {
    prism::Field<prism::TextField<>> field{{.value = "ab"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::right, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);
}

TEST_CASE("TextField Home moves cursor to 0") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::home, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);
}

TEST_CASE("TextField End moves cursor to end") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::ensure_edit_state(node);

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::end, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 3);
}

TEST_CASE("TextField max_length enforced on insert") {
    prism::Field<prism::TextField<>> field{{.value = "ab", .max_length = 3}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(field, prism::TextInput{"XY"}, node);
    CHECK(field.get().value == "abX");  // only one char fits
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 3);
}

TEST_CASE("TextField max_length blocks insert when full") {
    prism::Field<prism::TextField<>> field{{.value = "abc", .max_length = 3}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 3;

    prism::Delegate<prism::TextField<>>::handle_input(field, prism::TextInput{"X"}, node);
    CHECK(field.get().value == "abc");  // unchanged
}

TEST_CASE("TextField click positions cursor") {
    prism::Field<prism::TextField<>> field{{.value = "abcdef"}};
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::ensure_edit_state(node);

    float cw = prism::char_width(14.f);
    // Click at x corresponding to between char 2 and 3
    float click_x = 4.f + 2.5f * cw;  // padding + 2.5 chars
    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::MouseButton{{click_x, 15}, 1, true}, node);
    // Should round to nearest char boundary
    auto cursor = std::any_cast<prism::TextEditState>(node.edit_state).cursor;
    CHECK((cursor == 2 || cursor == 3));  // either side of the boundary is acceptable
}
```

- [x] **Step 2: Run tests to verify they fail**

Run: `meson test text_field -C builddir -v`
Expected: FAIL — `handle_input` is a stub or undefined.

- [x] **Step 3: Implement `handle_input()`**

In `include/prism/core/delegate.hpp`, replace the `handle_input` declaration in `Delegate<TextField<T>>` with:

```cpp
static void handle_input(Field<TextField<T>>& field, const InputEvent& ev, WidgetNode& node) {
    auto& es = ensure_edit_state(node);
    auto tf = field.get();
    auto len = tf.value.size();

    if (auto* ti = std::get_if<TextInput>(&ev)) {
        std::string to_insert = ti->text;
        if (tf.max_length > 0 && len + to_insert.size() > tf.max_length) {
            size_t room = (len < tf.max_length) ? tf.max_length - len : 0;
            to_insert = to_insert.substr(0, room);
        }
        if (!to_insert.empty()) {
            std::string v(tf.value.data(), tf.value.size());
            v.insert(es.cursor, to_insert);
            es.cursor += to_insert.size();
            tf.value = T(v);
            field.set(tf);
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (kp->key == keys::backspace && es.cursor > 0) {
            std::string v(tf.value.data(), tf.value.size());
            v.erase(es.cursor - 1, 1);
            es.cursor--;
            tf.value = T(v);
            field.set(tf);
        } else if (kp->key == keys::delete_ && es.cursor < len) {
            std::string v(tf.value.data(), tf.value.size());
            v.erase(es.cursor, 1);
            tf.value = T(v);
            field.set(tf);
        } else if (kp->key == keys::left && es.cursor > 0) {
            es.cursor--;
            node.dirty = true;
        } else if (kp->key == keys::right && es.cursor < len) {
            es.cursor++;
            node.dirty = true;
        } else if (kp->key == keys::home && es.cursor > 0) {
            es.cursor = 0;
            node.dirty = true;
        } else if (kp->key == keys::end && es.cursor < len) {
            es.cursor = len;
            node.dirty = true;
        }
    } else if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        float cw = char_width(font_size);
        float rel_x = mb->position.x - padding + es.scroll_offset;
        size_t pos = static_cast<size_t>(std::clamp(rel_x / cw + 0.5f, 0.f, static_cast<float>(len)));
        if (pos != es.cursor) {
            es.cursor = pos;
            node.dirty = true;
        }
    }

    // Keep cursor visible within text area
    float cw = char_width(font_size);
    float text_area_w = widget_w - 2 * padding;
    float cursor_px = es.cursor * cw;
    if (cursor_px - es.scroll_offset > text_area_w)
        es.scroll_offset = cursor_px - text_area_w;
    if (cursor_px < es.scroll_offset)
        es.scroll_offset = cursor_px;
}
```

- [x] **Step 4: Run tests to verify they pass**

Run: `meson test text_field -C builddir -v`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add include/prism/core/delegate.hpp tests/test_text_field.cpp
git commit -m "feat: implement TextField handle_input — insert, delete, navigate, click, scroll"
```

---

### Task 6: Implement scroll offset tests

**Files:**
- Modify: `tests/test_text_field.cpp`

- [x] **Step 1: Write scroll tests**

Add to `tests/test_text_field.cpp`:

```cpp
TEST_CASE("TextField scroll_offset adjusts when cursor moves past right edge") {
    prism::Field<prism::TextField<>> field{{.value = "abcdefghijklmnopqrstuvwxyz0123456789"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 35;  // at end

    // Press End to trigger scroll recalc
    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::end, 0}, node);

    float cw = prism::char_width(14.f);
    float text_area_w = 200.f - 2 * 4.f;  // widget_w - 2*padding
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).scroll_offset > 0.f);
    float cursor_px = 35 * cw;
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).scroll_offset ==
          doctest::Approx(cursor_px - text_area_w));
}

TEST_CASE("TextField scroll_offset resets when cursor moves to beginning") {
    prism::Field<prism::TextField<>> field{{.value = "abcdefghijklmnopqrstuvwxyz0123456789"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 35;
    es.scroll_offset = 100.f;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::home, 0}, node);

    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).scroll_offset == 0.f);
}
```

- [x] **Step 2: Run tests to verify they pass**

Run: `meson test text_field -C builddir -v`
Expected: PASS (scroll logic already implemented in Task 5).

- [x] **Step 3: Commit**

```bash
git add tests/test_text_field.cpp
git commit -m "test: add scroll offset tests for TextField"
```

---

### Task 7: Forward `TextInput` events in `model_app.hpp`

**Files:**
- Modify: `include/prism/core/model_app.hpp`

- [x] **Step 1: Write integration test**

Add to `tests/test_text_field.cpp` (this test uses WidgetTree dispatch directly, not model_app, so it stays in headless_tests):

```cpp
struct TextFieldModel {
    prism::Field<prism::TextField<>> name{{.value = "hi"}};
    prism::Field<bool> flag{false};
};

TEST_CASE("TextField in WidgetTree creates focusable leaf") {
    TextFieldModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);

    auto focus = tree.focus_order();
    REQUIRE(focus.size() == 2);  // TextField + bool
}

TEST_CASE("TextField dispatch TextInput updates field value") {
    TextFieldModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    tree.set_focused(focus[0]);

    tree.dispatch(focus[0], prism::TextInput{"X"});
    CHECK(model.name.get().value == "hiX");
}

TEST_CASE("TextField dispatch KeyPress backspace updates field value") {
    TextFieldModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    tree.set_focused(focus[0]);

    // First move cursor to end by dispatching End key
    tree.dispatch(focus[0], prism::KeyPress{prism::keys::end, 0});
    tree.dispatch(focus[0], prism::KeyPress{prism::keys::backspace, 0});
    CHECK(model.name.get().value == "h");
}
```

- [x] **Step 2: Run integration tests**

Run: `meson test text_field -C builddir -v`
Expected: PASS

- [x] **Step 3: Update `model_app.hpp` to forward `TextInput` and navigation keys to focused widget**

In `include/prism/core/model_app.hpp`, in the event processing lambda, add handling for `TextInput` and extend `KeyPress` forwarding. After the existing `KeyPress` block (line 76-86), add:

```cpp
if (auto* ti = std::get_if<TextInput>(&ev)) {
    if (tree.focused_id() != 0)
        tree.dispatch(tree.focused_id(), ev);
}
```

And in the existing `KeyPress` handler, extend the forwarding. Currently only Space/Enter are forwarded to focused widget. Change:

```cpp
if (auto* kp = std::get_if<KeyPress>(&ev)) {
    if (kp->key == keys::tab) {
        if (kp->mods & mods::shift)
            tree.focus_prev();
        else
            tree.focus_next();
    } else if (tree.focused_id() != 0) {
        tree.dispatch(tree.focused_id(), ev);
    }
}
```

This forwards **all** non-Tab key presses to the focused widget (not just Space/Enter). This is the correct behavior — each delegate decides what keys it handles.

- [x] **Step 4: Run full test suite**

Run: `meson test -C builddir`
Expected: All tests pass.

- [x] **Step 5: Commit**

```bash
git add include/prism/core/model_app.hpp tests/test_text_field.cpp
git commit -m "feat: forward TextInput and all KeyPress events to focused widget in model_app"
```

---

### Task 8: Forward `SDL_EVENT_TEXT_INPUT` in `software_backend.cpp`

**Files:**
- Modify: `src/backends/software_backend.cpp`

- [x] **Step 1: Add `SDL_StartTextInput` call**

In `src/backends/software_backend.cpp`, after the `ready_.notify_one()` line (line 55), add:

```cpp
SDL_StartTextInput(window_);
```

- [x] **Step 2: Add `SDL_EVENT_TEXT_INPUT` case to the event switch**

In the event switch (around line 62), add a new case after `SDL_EVENT_KEY_UP`:

```cpp
case SDL_EVENT_TEXT_INPUT:
    event_cb(TextInput{ev.text.text});
    break;
```

- [x] **Step 3: Run full test suite**

Run: `meson test -C builddir`
Expected: All tests pass (backend tests don't exercise `SDL_EVENT_TEXT_INPUT` directly, but compilation must succeed).

- [x] **Step 4: Commit**

```bash
git add src/backends/software_backend.cpp
git commit -m "feat: forward SDL_EVENT_TEXT_INPUT as TextInput event in SoftwareBackend"
```

---

### Task 9: Add `Field<TextField<>>` to example dashboard

**Files:**
- Modify: `examples/model_dashboard.cpp`

- [x] **Step 1: Add TextField field to the model**

In `examples/model_dashboard.cpp`, add to the `Settings` struct:

```cpp
prism::Field<prism::TextField<>> search{{.value = "", .placeholder = "Search..."}};
```

- [x] **Step 2: Build and visually verify**

Run: `meson compile -C builddir && ./builddir/model_dashboard`
Expected: A text field appears in the dashboard. Click on it to focus. Type text. Press arrows/Home/End to navigate. Press backspace to delete. Placeholder text "Search..." shows when empty and unfocused.

- [x] **Step 3: Commit**

```bash
git add examples/model_dashboard.cpp
git commit -m "feat: add TextField<> demo to model_dashboard example"
```

---

### Task 10: Final verification

- [x] **Step 1: Run full test suite**

Run: `meson test -C builddir -v`
Expected: All tests pass, including new text_field tests.

- [x] **Step 2: Run with sanitizers if available**

Run: `meson test -C builddir --setup=sanitize` (if configured), or rebuild with `-Db_sanitize=address,undefined` and re-run tests.
Expected: No sanitizer warnings.

- [x] **Step 3: Verify no regressions in existing delegate behavior**

Run: `meson test delegate widget_tree model_app -C builddir -v`
Expected: All pass.
