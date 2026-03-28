# Password Field Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `Password<T>` sentinel that masks displayed text with bullet characters while sharing all editing logic with `TextField<T>`.

**Architecture:** Extract `Delegate<TextField<T>>`'s record/handle_input bodies into `detail::` helpers parameterized by a display transform. `Delegate<Password<T>>` calls the same helpers with a masking function. No new infrastructure — purely additive sentinel + refactor for sharing.

**Tech Stack:** C++26 with `-freflection`, doctest, Meson build system

---

### Task 1: Add Password<T> sentinel type and Delegate declaration

**Files:**
- Modify: `include/prism/core/delegate.hpp`

- [ ] **Step 1: Write the Password<T> sentinel**

Add after the `TextField<T>` sentinel (after line 54 in `delegate.hpp`):

```cpp
// Sentinel: masked text field (displays bullets instead of actual text)
template <StringLike T = std::string>
struct Password {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;
    bool operator==(const Password&) const = default;
};
```

- [ ] **Step 2: Declare Delegate<Password<T>>**

Add after the `Delegate<TextField<T>>` declaration (after line 391):

```cpp
template <StringLike T>
struct Delegate<Password<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr float widget_w = 200.f;
    static constexpr float widget_h = 30.f;
    static constexpr float padding = 4.f;
    static constexpr float font_size = 14.f;
    static constexpr float cursor_w = 2.f;

    static const TextEditState& get_edit_state(const WidgetNode& node);
    static TextEditState& ensure_edit_state(WidgetNode& node);
    static void record(DrawList& dl, const Field<Password<T>>& field, const WidgetNode& node);
    static void handle_input(Field<Password<T>>& field, const InputEvent& ev, WidgetNode& node);
};
```

- [ ] **Step 3: Verify compilation**

Run: `meson compile -C builddir 2>&1 | tail -5`
Expected: Compiles (Delegate<Password<T>> bodies not yet defined — only declared, not instantiated)

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/delegate.hpp
git commit -m "feat: add Password<T> sentinel type and Delegate declaration"
```

---

### Task 2: Extract shared text field helpers into detail namespace

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`

This is the key refactoring step. Move the TextField delegate method bodies into `detail::` free functions so both TextField and Password can share them.

- [ ] **Step 1: Run existing tests to establish baseline**

Run: `meson test -C builddir --print-errorlogs 2>&1 | tail -5`
Expected: 23/23 pass

- [ ] **Step 2: Add detail helpers before the existing TextField delegate bodies**

Insert a new `detail` block after the `node_vs()` definition (after line 41 in `widget_tree.hpp`), replacing the existing `Delegate<TextField<T>>` method bodies (lines 47-164). The new code:

```cpp
// --- Shared text field helpers (used by TextField and Password delegates) ---

namespace detail {

inline const TextEditState& get_text_edit_state(const WidgetNode& node) {
    static const TextEditState default_state;
    if (!node.edit_state.has_value()) return default_state;
    return std::any_cast<const TextEditState&>(node.edit_state);
}

inline TextEditState& ensure_text_edit_state(WidgetNode& node) {
    if (!node.edit_state.has_value())
        node.edit_state = TextEditState{};
    return std::any_cast<TextEditState&>(node.edit_state);
}

inline std::string mask_string(size_t len) {
    std::string result;
    result.reserve(len * 3);
    for (size_t i = 0; i < len; ++i)
        result += "\xe2\x97\x8f";
    return result;
}

constexpr float tf_widget_w = 200.f;
constexpr float tf_widget_h = 30.f;
constexpr float tf_padding = 4.f;
constexpr float tf_font_size = 14.f;
constexpr float tf_cursor_w = 2.f;

template <typename Sentinel, typename DisplayFn>
void text_field_record(DrawList& dl, const Field<Sentinel>& field, const WidgetNode& node,
                       DisplayFn display_fn) {
    auto& vs = node_vs(node);
    auto& sf = field.get();
    auto& es = get_text_edit_state(node);
    float cw = char_width(tf_font_size);

    auto bg = vs.focused ? Color::rgba(65, 65, 78)
            : vs.hovered ? Color::rgba(55, 55, 68)
            : Color::rgba(45, 45, 55);
    dl.filled_rect({0, 0, tf_widget_w, tf_widget_h}, bg);

    if (vs.focused)
        dl.rect_outline({-1, -1, tf_widget_w + 2, tf_widget_h + 2},
                        Color::rgba(80, 160, 240), 2.0f);

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
}

template <typename Sentinel>
void text_field_handle_input(Field<Sentinel>& field, const InputEvent& ev, WidgetNode& node) {
    auto& es = ensure_text_edit_state(node);
    auto sf = field.get();
    auto len = sf.value.size();

    if (auto* ti = std::get_if<TextInput>(&ev)) {
        std::string to_insert = ti->text;
        if (sf.max_length > 0 && len + to_insert.size() > sf.max_length) {
            size_t room = (len < sf.max_length) ? sf.max_length - len : 0;
            to_insert = to_insert.substr(0, room);
        }
        if (!to_insert.empty()) {
            std::string v(sf.value.data(), sf.value.size());
            v.insert(es.cursor, to_insert);
            es.cursor += to_insert.size();
            sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
            field.set(sf);
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (kp->key == keys::backspace && es.cursor > 0) {
            std::string v(sf.value.data(), sf.value.size());
            v.erase(es.cursor - 1, 1);
            es.cursor--;
            sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
            field.set(sf);
        } else if (kp->key == keys::delete_ && es.cursor < len) {
            std::string v(sf.value.data(), sf.value.size());
            v.erase(es.cursor, 1);
            sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
            field.set(sf);
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
        float cw = char_width(tf_font_size);
        float rel_x = mb->position.x - tf_padding + es.scroll_offset;
        size_t pos = static_cast<size_t>(
            std::clamp(rel_x / cw + 0.5f, 0.f, static_cast<float>(len)));
        if (pos != es.cursor) {
            es.cursor = pos;
            node.dirty = true;
        }
    }

    float cw = char_width(tf_font_size);
    float text_area_w = tf_widget_w - 2 * tf_padding;
    float cursor_px = es.cursor * cw;
    if (cursor_px - es.scroll_offset > text_area_w)
        es.scroll_offset = cursor_px - text_area_w;
    if (cursor_px < es.scroll_offset)
        es.scroll_offset = cursor_px;
}

} // namespace detail
```

- [ ] **Step 3: Rewrite Delegate<TextField<T>> methods to forward to detail helpers**

Replace the old `Delegate<TextField<T>>` method bodies (the block that was at lines 47-164) with thin forwards:

```cpp
// --- Delegate<TextField<T>> method bodies ---

template <StringLike T>
const TextEditState& Delegate<TextField<T>>::get_edit_state(const WidgetNode& node) {
    return detail::get_text_edit_state(node);
}

template <StringLike T>
TextEditState& Delegate<TextField<T>>::ensure_edit_state(WidgetNode& node) {
    return detail::ensure_text_edit_state(node);
}

template <StringLike T>
void Delegate<TextField<T>>::record(DrawList& dl, const Field<TextField<T>>& field,
                                    const WidgetNode& node) {
    detail::text_field_record(dl, field, node,
        [](const std::string& v) { return v; });
}

template <StringLike T>
void Delegate<TextField<T>>::handle_input(Field<TextField<T>>& field, const InputEvent& ev,
                                          WidgetNode& node) {
    detail::text_field_handle_input(field, ev, node);
}
```

- [ ] **Step 4: Run all tests to verify refactoring is behavior-preserving**

Run: `meson test -C builddir --print-errorlogs 2>&1 | tail -5`
Expected: 23/23 pass (all existing tests including text_field)

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp
git commit -m "refactor: extract shared text field helpers into detail namespace"
```

---

### Task 3: Implement Delegate<Password<T>> method bodies

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`

- [ ] **Step 1: Add Delegate<Password<T>> method bodies**

Add after the `Delegate<TextField<T>>` forwarding methods (before the dropdown detail block):

```cpp
// --- Delegate<Password<T>> method bodies ---

template <StringLike T>
const TextEditState& Delegate<Password<T>>::get_edit_state(const WidgetNode& node) {
    return detail::get_text_edit_state(node);
}

template <StringLike T>
TextEditState& Delegate<Password<T>>::ensure_edit_state(WidgetNode& node) {
    return detail::ensure_text_edit_state(node);
}

template <StringLike T>
void Delegate<Password<T>>::record(DrawList& dl, const Field<Password<T>>& field,
                                    const WidgetNode& node) {
    detail::text_field_record(dl, field, node,
        [](const std::string& v) { return detail::mask_string(v.size()); });
}

template <StringLike T>
void Delegate<Password<T>>::handle_input(Field<Password<T>>& field, const InputEvent& ev,
                                          WidgetNode& node) {
    detail::text_field_handle_input(field, ev, node);
}
```

- [ ] **Step 2: Verify compilation**

Run: `meson compile -C builddir 2>&1 | tail -5`
Expected: Compiles successfully

- [ ] **Step 3: Commit**

```bash
git add include/prism/core/widget_tree.hpp
git commit -m "feat: implement Delegate<Password<T>> via shared text field helpers"
```

---

### Task 4: Write Password<T> tests

**Files:**
- Create: `tests/test_password.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write test file**

Create `tests/test_password.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/widget_tree.hpp>

namespace {
prism::WidgetNode make_node(prism::WidgetVisualState vs = {}) {
    prism::WidgetNode node;
    node.visual_state = vs;
    return node;
}
}

TEST_CASE("Password default-constructs with empty value") {
    prism::Password<> pw;
    CHECK(pw.value.empty());
    CHECK(pw.placeholder.empty());
    CHECK(pw.max_length == 0);
}

TEST_CASE("Password equality comparison") {
    prism::Password<> a{.value = "secret"};
    prism::Password<> b{.value = "secret"};
    prism::Password<> c{.value = "other"};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("TextEditable concept matches Password") {
    static_assert(prism::TextEditable<prism::Password<>>);
}

TEST_CASE("Delegate<Password<>> has tab_and_click focus policy") {
    CHECK(prism::Delegate<prism::Password<>>::focus_policy == prism::FocusPolicy::tab_and_click);
}

// --- record() tests ---

TEST_CASE("Password record renders masked text, not actual value") {
    prism::Field<prism::Password<>> field{{.value = "abc"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Password<>>::record(dl, field, node);

    bool has_actual_value = false;
    bool has_bullets = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "abc") has_actual_value = true;
            if (t->text.find("\xe2\x97\x8f") != std::string::npos) has_bullets = true;
        }
    }
    CHECK_FALSE(has_actual_value);
    CHECK(has_bullets);
}

TEST_CASE("Password record shows placeholder when empty and unfocused") {
    prism::Field<prism::Password<>> field{{.placeholder = "Enter password"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Password<>>::record(dl, field, node);

    bool has_placeholder = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Enter password") has_placeholder = true;
        }
    }
    CHECK(has_placeholder);
}

TEST_CASE("Password record hides placeholder when focused") {
    prism::Field<prism::Password<>> field{{.placeholder = "Enter password"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<prism::Password<>>::record(dl, field, node);

    bool has_placeholder = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Enter password") has_placeholder = true;
        }
    }
    CHECK_FALSE(has_placeholder);
}

TEST_CASE("Password record shows cursor when focused") {
    prism::Field<prism::Password<>> field{{.value = "hi"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<prism::Password<>>::record(dl, field, node);

    int thin_rects = 0;
    for (auto& cmd : dl.commands) {
        if (auto* fr = std::get_if<prism::FilledRect>(&cmd)) {
            if (fr->rect.w <= 3.f) thin_rects++;
        }
    }
    CHECK(thin_rects >= 1);
}

TEST_CASE("Password record uses clip_push/pop") {
    prism::Field<prism::Password<>> field{{.value = "test"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Password<>>::record(dl, field, node);

    int clips = 0;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::ClipPush>(cmd)) clips++;
        if (std::holds_alternative<prism::ClipPop>(cmd)) clips++;
    }
    CHECK(clips == 2);
}

TEST_CASE("Password record renders focus ring when focused") {
    prism::Field<prism::Password<>> field{{.value = "test"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<prism::Password<>>::record(dl, field, node);

    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK(has_outline);
}

// --- handle_input() tests ---

TEST_CASE("Password TextInput inserts text") {
    prism::Field<prism::Password<>> field{{.value = "ab"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::Password<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Delegate<prism::Password<>>::handle_input(field, prism::TextInput{"X"}, node);
    CHECK(field.get().value == "aXb");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);
}

TEST_CASE("Password backspace deletes char before cursor") {
    prism::Field<prism::Password<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::Password<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::Password<>>::handle_input(
        field, prism::KeyPress{prism::keys::backspace, 0}, node);
    CHECK(field.get().value == "ac");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 1);
}

TEST_CASE("Password arrow keys move cursor") {
    prism::Field<prism::Password<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::Password<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Delegate<prism::Password<>>::handle_input(
        field, prism::KeyPress{prism::keys::right, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);

    prism::Delegate<prism::Password<>>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 1);
}

TEST_CASE("Password Home/End move cursor") {
    prism::Field<prism::Password<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::Password<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Delegate<prism::Password<>>::handle_input(
        field, prism::KeyPress{prism::keys::end, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 3);

    prism::Delegate<prism::Password<>>::handle_input(
        field, prism::KeyPress{prism::keys::home, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);
}

TEST_CASE("Password max_length enforced") {
    prism::Field<prism::Password<>> field{{.value = "ab", .max_length = 3}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::Password<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::Password<>>::handle_input(field, prism::TextInput{"XY"}, node);
    CHECK(field.get().value == "abX");
}

// --- WidgetTree integration ---

struct PasswordModel {
    prism::Field<prism::Password<>> secret{{.value = "pass"}};
    prism::Field<bool> flag{false};
};

TEST_CASE("Password in WidgetTree creates focusable leaf") {
    PasswordModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);

    auto focus = tree.focus_order();
    CHECK(focus.size() == 2);
}

TEST_CASE("Password dispatch TextInput updates field value") {
    PasswordModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    tree.set_focused(focus[0]);

    tree.dispatch(focus[0], prism::KeyPress{prism::keys::end, 0});
    tree.dispatch(focus[0], prism::TextInput{"!"});
    CHECK(model.secret.get().value == "pass!");
}

TEST_CASE("Password snapshot renders masked text") {
    PasswordModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);

    bool has_actual = false;
    bool has_bullets = false;
    for (auto& dl : snap->draw_lists) {
        for (auto& cmd : dl.commands) {
            if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
                if (t->text == "pass") has_actual = true;
                if (t->text.find("\xe2\x97\x8f") != std::string::npos) has_bullets = true;
            }
        }
    }
    CHECK_FALSE(has_actual);
    CHECK(has_bullets);
}
```

- [ ] **Step 2: Register test in meson.build**

Add `'password' : files('test_password.cpp'),` to the `headless_tests` dict in `tests/meson.build`.

- [ ] **Step 3: Run all tests**

Run: `meson test -C builddir --print-errorlogs 2>&1 | tail -10`
Expected: 24/24 pass (23 existing + 1 new password test)

- [ ] **Step 4: Commit**

```bash
git add tests/test_password.cpp tests/meson.build
git commit -m "test: add Password<T> delegate tests"
```

---

### Task 5: Add Password demo to model_dashboard

**Files:**
- Modify: `examples/model_dashboard.cpp`

- [ ] **Step 1: Add a Field<Password<>> to the Settings component**

In the `Settings` struct in `model_dashboard.cpp`, add:

```cpp
Field<Password<>> api_key{{.placeholder = "API key"}};
```

- [ ] **Step 2: Build and verify visually**

Run: `meson compile -C builddir && ./builddir/examples/model_dashboard`
Expected: Password field appears in the dashboard. Typing shows bullets. Placeholder "API key" appears when empty and unfocused.

- [ ] **Step 3: Commit**

```bash
git add examples/model_dashboard.cpp
git commit -m "feat: add Password field demo to model_dashboard"
```

---

### Task 6: Verify all tests pass and no regressions

- [ ] **Step 1: Run full test suite**

Run: `meson test -C builddir --print-errorlogs`
Expected: 24/24 pass

- [ ] **Step 2: Verify text_field tests specifically (refactoring regression check)**

Run: `./builddir/tests/test_text_field --no-colors 2>&1 | tail -5`
Expected: All 26 text_field assertions pass

- [ ] **Step 3: Verify delegate tests (shared infrastructure)**

Run: `./builddir/tests/test_delegate --no-colors 2>&1 | tail -5`
Expected: All delegate tests pass
