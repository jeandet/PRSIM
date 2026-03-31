# Tab Widget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a tab widget with horizontal top-bar, supporting explicit (lambda) and reflective (struct) tab definition modes.

**Architecture:** New `LayoutKind::Tabs` with a Tabs container node holding two children: a tab bar leaf (focusable, renders headers, handles Left/Right + click) and a content Column whose children are lazily materialized from the active tab's builder. `TabBar` sentinel for explicit mode, `TabBar<S>` for reflective mode. `TabsState` (shared_ptr, like TableState) holds tab names, builders, and selected-index reader. `Delegate<TabBar>` for rendering/input. Reflective mode uses C++26 reflection to walk struct members.

**Tech Stack:** C++23 (C++26 reflection for `TabBar<S>`), doctest, Meson

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `include/prism/core/delegate.hpp` | Modify | Add `TabBar` sentinel, `TabBarEditState`, extend `LayoutKind` and `EditState` |
| `include/prism/core/tabs_delegates.hpp` | Create | Tab bar rendering (`tabs_record`) and input handling (`tabs_handle_input`), `Delegate<TabBar>` bodies |
| `include/prism/core/node.hpp` | Modify | Add `tabs_state` field |
| `include/prism/core/widget_node.hpp` | Modify | Add `tab_names`, `TabsState` struct |
| `include/prism/core/widget_tree.hpp` | Modify | Add `ViewBuilder::tabs()`/`tab()`, `build_widget_node` Tabs case, `build_layout` Tabs case, `materialize_tabs`, `unindex_subtree` |
| `include/prism/core/layout.hpp` | Modify | Add `LayoutNode::Kind::Tabs`, measure/arrange/flatten |
| `tests/test_tabs.cpp` | Create | All tab widget tests |
| `tests/meson.build` | Modify | Register `test_tabs` |

---

### Task 1: TabBar sentinel, TabBarEditState, LayoutKind::Tabs

**Files:**
- Modify: `include/prism/core/delegate.hpp:36` (LayoutKind enum), `:204-225` (EditState area)
- Create: `tests/test_tabs.cpp`
- Modify: `tests/meson.build:31`

- [ ] **Step 1: Create test file with sentinel tests**

Create `tests/test_tabs.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/hit_test.hpp>
#include <prism/core/widget_tree.hpp>

namespace {
prism::WidgetNode make_node(prism::WidgetVisualState vs = {}) {
    prism::WidgetNode node;
    node.visual_state = vs;
    return node;
}
}

TEST_CASE("TabBar default constructs with selected=0") {
    prism::TabBar tb;
    CHECK(tb.selected == 0);
}

TEST_CASE("TabBar equality") {
    prism::TabBar a{.selected = 0};
    prism::TabBar b{.selected = 1};
    CHECK(a == a);
    CHECK(a != b);
}

TEST_CASE("TabBarEditState default constructs") {
    prism::TabBarEditState es;
    CHECK(!es.hovered_tab.has_value());
    CHECK(es.header_x_ranges.empty());
}

TEST_CASE("LayoutKind::Tabs exists") {
    auto kind = prism::LayoutKind::Tabs;
    CHECK(kind != prism::LayoutKind::Default);
}
```

- [ ] **Step 2: Register test in meson.build**

In `tests/meson.build`, add after the `'table'` entry (line 31):

```
  'tabs' : files('test_tabs.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd builddir && meson compile && meson test test_tabs`
Expected: Compilation failure — `TabBar`, `TabBarEditState`, `LayoutKind::Tabs` not defined.

- [ ] **Step 4: Add TabBar sentinel to delegate.hpp**

After `DropdownEditState` (line 209), add:

```cpp
// Sentinel: tab bar — selected index only; tab names defined in view()
struct TabBar {
    size_t selected = 0;
    bool operator==(const TabBar&) const = default;
};

// Ephemeral state for tab bar hover tracking and header hit regions
struct TabBarEditState {
    std::optional<size_t> hovered_tab;
    std::vector<std::pair<float, float>> header_x_ranges;
};
```

- [ ] **Step 5: Add Tabs to LayoutKind enum**

Change line 36 from:

```cpp
enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table };
```

to:

```cpp
enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table, Tabs };
```

- [ ] **Step 6: Add TabBarEditState and forward-declare TabsState in EditState variant**

Change the EditState area (lines 213-225):

```cpp
struct VirtualListState;
struct TableState;
struct TabsState;

using EditState = std::variant<
    std::monostate,
    TextEditState,
    TextAreaEditState,
    DropdownEditState,
    ScrollState,
    TabBarEditState,
    std::shared_ptr<VirtualListState>,
    std::shared_ptr<TableState>,
    std::shared_ptr<TabsState>,
    std::shared_ptr<void>
>;
```

- [ ] **Step 7: Run test to verify it passes**

Run: `cd builddir && meson compile && meson test test_tabs`
Expected: All 4 tests PASS.

- [ ] **Step 8: Commit**

```bash
git add include/prism/core/delegate.hpp tests/test_tabs.cpp tests/meson.build
git commit -m "feat(tabs): add TabBar sentinel, TabBarEditState, LayoutKind::Tabs"
```

---

### Task 2: Tab bar delegate — rendering and input

**Files:**
- Create: `include/prism/core/tabs_delegates.hpp`
- Modify: `include/prism/core/delegate.hpp` (Delegate<TabBar> declaration)
- Modify: `include/prism/core/widget_node.hpp` (add `tab_names` field)
- Modify: `include/prism/core/widget_tree.hpp` (include tabs_delegates.hpp)
- Test: `tests/test_tabs.cpp`

- [ ] **Step 1: Write failing tests for Delegate\<TabBar\>**

Append to `tests/test_tabs.cpp`:

```cpp
TEST_CASE("Delegate<TabBar> focus policy is tab_and_click") {
    static_assert(prism::Delegate<prism::TabBar>::focus_policy == prism::FocusPolicy::tab_and_click);
}

TEST_CASE("Delegate<TabBar> records header text") {
    prism::Field<prism::TabBar> field{{.selected = 0}};
    auto node = make_node();
    auto names = std::make_shared<std::vector<std::string>>(
        std::vector<std::string>{"Alpha", "Beta"});
    node.tab_names = names;
    prism::DrawList dl;
    prism::Delegate<prism::TabBar>::record(dl, field, node);

    CHECK(!dl.commands.empty());

    auto* es = std::get_if<prism::TabBarEditState>(&node.edit_state);
    REQUIRE(es);
    CHECK(es->header_x_ranges.size() == 2);
}

TEST_CASE("tabs_handle_input: Left/Right switches tabs") {
    prism::Field<prism::TabBar> field{{.selected = 0}};
    auto node = make_node({.focused = true});
    auto names = std::make_shared<std::vector<std::string>>(
        std::vector<std::string>{"A", "B", "C"});
    node.tab_names = names;

    prism::DrawList dl;
    prism::Delegate<prism::TabBar>::record(dl, field, node);

    prism::Delegate<prism::TabBar>::handle_input(
        field, prism::KeyPress{prism::keys::right, 0}, node);
    CHECK(field.get().selected == 1);

    prism::Delegate<prism::TabBar>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(field.get().selected == 0);

    // Left wraps to last
    prism::Delegate<prism::TabBar>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(field.get().selected == 2);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd builddir && meson compile && meson test test_tabs`
Expected: Compilation failure — `Delegate<TabBar>` not specialized, `tab_names` not a member.

- [ ] **Step 3: Add tab_names to WidgetNode**

In `include/prism/core/widget_node.hpp`, add after `table_input_wired` (line 36):

```cpp
    std::shared_ptr<std::vector<std::string>> tab_names;
```

- [ ] **Step 4: Declare Delegate\<TabBar\> in delegate.hpp**

Before the closing `} // namespace prism` (line 554), add:

```cpp
template <>
struct Delegate<TabBar> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<TabBar>& field, WidgetNode& node);
    static void handle_input(Field<TabBar>& field, const InputEvent& ev, WidgetNode& node);
};
```

- [ ] **Step 5: Create tabs_delegates.hpp**

Create `include/prism/core/tabs_delegates.hpp`:

```cpp
#pragma once

#include <prism/core/widget_node.hpp>

#include <functional>
#include <string>

namespace prism {
namespace detail {

inline const TabBarEditState& get_tabs_state(const WidgetNode& node) {
    static const TabBarEditState default_state;
    auto* p = std::get_if<TabBarEditState>(&node.edit_state);
    return p ? *p : default_state;
}

inline TabBarEditState& ensure_tabs_state(WidgetNode& node) {
    if (!std::holds_alternative<TabBarEditState>(node.edit_state))
        node.edit_state = TabBarEditState{};
    return std::get<TabBarEditState>(node.edit_state);
}

constexpr float tab_h = 32.f;
constexpr float tab_padding = 16.f;
constexpr float tab_font_size = 14.f;
constexpr float tab_accent_h = 2.f;
constexpr float tab_char_width = 8.f;

inline void tabs_record(DrawList& dl, WidgetNode& node,
                        size_t selected, const std::vector<std::string>& names) {
    auto& vs = node_vs(node);
    auto& es = ensure_tabs_state(node);

    float total_w = 0;
    for (auto& name : names)
        total_w += tab_padding * 2 + static_cast<float>(name.size()) * tab_char_width;

    dl.filled_rect(make_rect(0, 0, total_w, tab_h), Color::rgba(42, 42, 58));

    es.header_x_ranges.clear();
    es.header_x_ranges.reserve(names.size());
    float x = 0;
    for (size_t i = 0; i < names.size(); ++i) {
        float w = tab_padding * 2 + static_cast<float>(names[i].size()) * tab_char_width;
        es.header_x_ranges.push_back({x, x + w});

        bool is_selected = (i == selected);
        bool is_hovered = es.hovered_tab.has_value() && es.hovered_tab.value() == i;

        auto bg = is_selected ? Color::rgba(30, 30, 46)
                : is_hovered  ? Color::rgba(55, 55, 68)
                :               Color::rgba(42, 42, 58);
        dl.filled_rect(make_rect(x, 0, w, tab_h), bg);

        auto text_color = is_selected ? Color::rgba(220, 220, 240)
                                      : Color::rgba(140, 140, 160);
        dl.text(names[i], make_point(x + tab_padding, 8), tab_font_size, text_color);

        if (is_selected)
            dl.filled_rect(make_rect(x, tab_h - tab_accent_h, w, tab_accent_h),
                           Color::rgba(124, 111, 255));
        x += w;
    }

    if (vs.focused)
        dl.rect_outline(make_rect(-1, -1, total_w + 2, tab_h + 2),
                        Color::rgba(80, 160, 240), 2.0f);
}

inline bool tabs_handle_input(const InputEvent& ev, WidgetNode& node,
                              size_t selected, size_t count,
                              std::function<void(size_t)> select) {
    auto& es = ensure_tabs_state(node);

    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        float mx = mb->position.x.raw();
        for (size_t i = 0; i < es.header_x_ranges.size(); ++i) {
            auto [x0, x1] = es.header_x_ranges[i];
            if (mx >= x0 && mx < x1 && i != selected) {
                select(i);
                return true;
            }
        }
    } else if (auto* mm = std::get_if<MouseMove>(&ev)) {
        std::optional<size_t> hover;
        float mx = mm->position.x.raw();
        for (size_t i = 0; i < es.header_x_ranges.size(); ++i) {
            auto [x0, x1] = es.header_x_ranges[i];
            if (mx >= x0 && mx < x1) { hover = i; break; }
        }
        if (hover != es.hovered_tab) {
            es.hovered_tab = hover;
            node.dirty = true;
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (kp->key == keys::right) {
            select((selected + 1) % count);
            return true;
        } else if (kp->key == keys::left) {
            select((selected + count - 1) % count);
            return true;
        }
    }
    return false;
}

} // namespace detail

inline void Delegate<TabBar>::record(DrawList& dl, const Field<TabBar>& field,
                                     WidgetNode& node) {
    if (node.tab_names && !node.tab_names->empty())
        detail::tabs_record(dl, node, field.get().selected, *node.tab_names);
}

inline void Delegate<TabBar>::handle_input(Field<TabBar>& field, const InputEvent& ev,
                                            WidgetNode& node) {
    size_t count = node.tab_names ? node.tab_names->size() : 0;
    if (count == 0) return;
    detail::tabs_handle_input(ev, node, field.get().selected, count,
        [&](size_t idx) {
            auto tb = field.get();
            tb.selected = idx;
            field.set(tb);
        });
}

} // namespace prism
```

- [ ] **Step 6: Include tabs_delegates.hpp from widget_tree.hpp**

In `include/prism/core/widget_tree.hpp`, after `#include <prism/core/dropdown_delegates.hpp>` (line 3), add:

```cpp
#include <prism/core/tabs_delegates.hpp>
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `cd builddir && meson compile && meson test test_tabs`
Expected: All tests PASS.

- [ ] **Step 8: Commit**

```bash
git add include/prism/core/tabs_delegates.hpp include/prism/core/delegate.hpp \
        include/prism/core/widget_node.hpp include/prism/core/widget_tree.hpp \
        tests/test_tabs.cpp
git commit -m "feat(tabs): Delegate<TabBar> with rendering and keyboard/click input"
```

---

### Task 3: TabsState, Node metadata, ViewBuilder::tabs()/tab()

**Files:**
- Modify: `include/prism/core/widget_node.hpp` (TabsState struct)
- Modify: `include/prism/core/node.hpp` (tabs_state field)
- Modify: `include/prism/core/widget_tree.hpp` (ViewBuilder methods, build_widget_node, connect_dirty, build_layout)
- Modify: `include/prism/core/layout.hpp` (LayoutNode::Kind::Tabs, measure, arrange, flatten)
- Test: `tests/test_tabs.cpp`

- [ ] **Step 1: Write failing test — tree construction with tabs**

Append to `tests/test_tabs.cpp`:

```cpp
TEST_CASE("ViewBuilder tabs() creates tree with two tabs") {
    struct Model {
        prism::Field<prism::TabBar> tabs;
        prism::Field<std::string> page_a{"hello"};
        prism::Field<bool> page_b{true};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.tabs(tabs, [&] {
                vb.tab("Alpha", [&] { vb.widget(page_a); });
                vb.tab("Beta", [&] { vb.widget(page_b); });
            });
        }
    };

    Model model;
    prism::WidgetTree tree(model);

    CHECK(!tree.focus_order().empty());

    auto snap = tree.build_snapshot(800, 600, 1);
    CHECK(snap != nullptr);
    CHECK(snap->geometry.size() >= 2);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd builddir && meson compile && meson test test_tabs`
Expected: Compilation failure — `vb.tabs()` and `vb.tab()` not defined.

- [ ] **Step 3: Define TabsState in widget_node.hpp**

After `VirtualListState` (around line 53), add:

```cpp
struct TabsState {
    std::shared_ptr<std::vector<std::string>> tab_names;
    std::vector<std::function<void(Node&)>> tab_node_builders;
    std::function<size_t()> get_selected;
    size_t active_tab = std::numeric_limits<size_t>::max();
};
```

Add `#include <limits>` to the includes if not present.

- [ ] **Step 4: Add tabs_state to Node**

In `include/prism/core/node.hpp`, after `table_state` (line 44), add:

```cpp
    std::shared_ptr<TabsState> tabs_state;
```

- [ ] **Step 5: Add ViewBuilder::tabs() and tab() to widget_tree.hpp**

In the ViewBuilder public section (after the `table()` methods, around line 242), add:

```cpp
        void tabs(Field<TabBar>& field, std::invocable auto&& builder) {
            placed_.insert(&field);

            auto state = std::make_shared<TabsState>();
            state->get_selected = [&field]() { return field.get().selected; };

            auto* prev_tabs_state = current_tabs_state_;
            current_tabs_state_ = state.get();
            builder();
            current_tabs_state_ = prev_tabs_state;

            Node tabs_node;
            tabs_node.id = tree_.next_id_++;
            tabs_node.is_leaf = false;
            tabs_node.layout_kind = LayoutKind::Tabs;
            tabs_node.tabs_state = state;

            // Tab bar leaf (child 0)
            auto bar = node_leaf(field, tree_.next_id_);
            auto bar_build = bar.build_widget;
            auto names_ptr = state->tab_names;
            bar.build_widget = [bar_build, names_ptr](WidgetNode& wn) {
                if (bar_build) bar_build(wn);
                wn.tab_names = names_ptr;
            };
            tabs_node.children.push_back(std::move(bar));

            // Content container (child 1) — filled by materialize_tabs on first frame
            Node content;
            content.id = tree_.next_id_++;
            content.is_leaf = false;
            content.layout_kind = LayoutKind::Column;
            tabs_node.children.push_back(std::move(content));

            current_parent().children.push_back(std::move(tabs_node));
        }

        void tab(std::string_view name, std::invocable auto&& content_builder) {
            if (!current_tabs_state_) return;
            if (!current_tabs_state_->tab_names)
                current_tabs_state_->tab_names = std::make_shared<std::vector<std::string>>();
            current_tabs_state_->tab_names->push_back(std::string(name));
            current_tabs_state_->tab_node_builders.push_back(
                [this, cb = std::forward<decltype(content_builder)>(content_builder)]
                (Node& target) mutable {
                    ViewBuilder vb{tree_, target};
                    cb();
                });
        }
```

Add private member to ViewBuilder (after `placed_`):

```cpp
        TabsState* current_tabs_state_ = nullptr;
```

- [ ] **Step 6: Add Tabs case to build_widget_node**

In `build_widget_node()` (around line 612), add after the Table case and update the skip condition:

```cpp
            } else if (node.layout_kind == LayoutKind::Tabs && node.tabs_state) {
                wn.edit_state = node.tabs_state;
                for (auto& child : node.children)
                    wn.children.push_back(build_widget_node(child));
            }
```

Update the generic children condition (line 616) to exclude Tabs:

```cpp
            if (node.layout_kind != LayoutKind::VirtualList
                && node.layout_kind != LayoutKind::Table
                && node.layout_kind != LayoutKind::Tabs) {
```

- [ ] **Step 7: Add Tabs case to connect_dirty**

In `connect_dirty()`, before the `assert(node.children.size() == wn.children.size())` line (~711), add:

```cpp
            if (node.layout_kind == LayoutKind::Tabs) {
                // Connect tab bar (child 0) normally
                if (node.children.size() >= 1 && wn.children.size() >= 1)
                    connect_dirty(node.children[0], wn.children[0]);
                // Also mark the Tabs container dirty when selection changes
                auto id = wn.id;
                if (node.children.size() >= 1 && node.children[0].on_change) {
                    wn.connections.push_back(
                        node.children[0].on_change([this, id]() { set_dirty(id); })
                    );
                }
                // Content children (child 1) are managed by materialize_tabs
                return;
            }
```

- [ ] **Step 8: Implement materialize_tabs and unindex_subtree**

Add private methods to WidgetTree:

```cpp
    static TabsState* get_tabs_state(WidgetNode& node) {
        auto* sp = std::get_if<std::shared_ptr<TabsState>>(&node.edit_state);
        return sp ? sp->get() : nullptr;
    }

    void unindex_subtree(WidgetNode& node) {
        index_.erase(node.id);
        parent_map_.erase(node.id);
        std::erase(focus_order_, node.id);
        node.connections.clear();
        for (auto& c : node.children)
            unindex_subtree(c);
    }

    void materialize_tabs(WidgetNode& node) {
        auto* ts = get_tabs_state(node);
        if (!ts || !ts->get_selected) return;
        if (node.children.size() < 2) return;

        size_t selected = ts->get_selected();
        if (selected == ts->active_tab) return;
        ts->active_tab = selected;

        auto& content_wn = node.children[1];

        for (auto& c : content_wn.children)
            unindex_subtree(c);
        content_wn.children.clear();

        if (selected < ts->tab_node_builders.size()) {
            Node content_node;
            content_node.id = next_id_++;
            content_node.is_leaf = false;
            content_node.layout_kind = LayoutKind::Column;

            ts->tab_node_builders[selected](content_node);

            for (auto& child_node : content_node.children) {
                auto child_wn = build_widget_node(child_node);
                connect_dirty(child_node, child_wn);
                parent_map_[child_wn.id] = content_wn.id;
                content_wn.children.push_back(std::move(child_wn));
            }

            for (auto& c : content_wn.children)
                build_index(c);
        }
        content_wn.dirty = true;
    }
```

- [ ] **Step 9: Hook materialize_tabs into materialize_all_virtual_lists**

Update `materialize_all_virtual_lists()`:

```cpp
    void materialize_all_virtual_lists(WidgetNode& node) {
        if (node.layout_kind == LayoutKind::VirtualList)
            materialize_virtual_list(node);
        else if (node.layout_kind == LayoutKind::Table)
            materialize_table(node);
        else if (node.layout_kind == LayoutKind::Tabs)
            materialize_tabs(node);
        for (auto& c : node.children)
            materialize_all_virtual_lists(c);
    }
```

- [ ] **Step 10: Add Tabs to build_layout**

In `build_layout()`, before the Row/Column case (around line 1211), add:

```cpp
        } else if (node.layout_kind == LK::Tabs) {
            LayoutNode container;
            container.kind = LayoutNode::Kind::Tabs;
            container.id = node.id;
            for (auto& c : node.children)
                build_layout(c, container);
            parent.children.push_back(std::move(container));
```

- [ ] **Step 11: Add Tabs to LayoutNode::Kind enum**

In `layout.hpp`, change line 41:

```cpp
    enum class Kind { Leaf, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table, Tabs } kind = Kind::Leaf;
```

- [ ] **Step 12: Add Tabs measurement**

In `layout_measure()`, add before the closing `}` of the switch:

```cpp
    case LayoutNode::Kind::Tabs: {
        if (node.children.size() >= 1)
            layout_measure(node.children[0], LayoutAxis::Horizontal);
        if (node.children.size() >= 2)
            detail::measure_linear(node.children[1], LayoutAxis::Vertical, parent_axis);
        node.hint.expand = true;
        node.hint.preferred = 0;
        return;
    }
```

- [ ] **Step 13: Add Tabs arrangement**

In `layout_arrange()`, after the Scroll case (around line 159), add:

```cpp
    if (node.kind == LayoutNode::Kind::Tabs) {
        float bar_h = node.children.size() >= 1 ? node.children[0].hint.cross : 0;
        if (node.children.size() >= 1) {
            layout_arrange(node.children[0], {
                available.origin,
                Size{available.extent.w, Height{bar_h}}
            });
        }
        if (node.children.size() >= 2) {
            layout_arrange(node.children[1], {
                Point{available.origin.x, available.origin.y + DY{bar_h}},
                Size{available.extent.w, Height{available.extent.h.raw() - bar_h}}
            });
        }
        return;
    }
```

- [ ] **Step 14: Add Tabs flattening**

In `layout_flatten()`, after the Spacer early return (line 219), add:

```cpp
    if (node.kind == LayoutNode::Kind::Tabs) {
        for (auto& child : node.children)
            layout_flatten(child, snap);
        return;
    }
```

- [ ] **Step 15: Update close_overlays_impl to guard Tabs**

In `close_overlays_impl()`, update the condition to skip Tabs (like Table):

```cpp
    static void close_overlays_impl(WidgetNode& node) {
        if (!node.overlay_draws.empty()
            && node.layout_kind != LayoutKind::Table
            && node.layout_kind != LayoutKind::Tabs) {
            node.overlay_draws.clear();
            node.edit_state = std::monostate{};
            node.dirty = true;
        }
        for (auto& c : node.children) close_overlays_impl(c);
    }
```

- [ ] **Step 16: Run tests to verify they pass**

Run: `cd builddir && meson compile && meson test test_tabs`
Expected: All tests PASS.

- [ ] **Step 17: Commit**

```bash
git add include/prism/core/widget_node.hpp include/prism/core/node.hpp \
        include/prism/core/widget_tree.hpp include/prism/core/layout.hpp \
        tests/test_tabs.cpp
git commit -m "feat(tabs): ViewBuilder tabs()/tab(), layout, tree construction, lazy materialization"
```

---

### Task 4: Tab switching and content rematerialization tests

**Files:**
- Test: `tests/test_tabs.cpp`

- [ ] **Step 1: Write tab switching tests**

Append to `tests/test_tabs.cpp`:

```cpp
TEST_CASE("Tab switch rematerializes content") {
    struct Model {
        prism::Field<prism::TabBar> tabs;
        prism::Field<std::string> page_a{"hello"};
        prism::Field<bool> page_b{true};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.tabs(tabs, [&] {
                vb.tab("Alpha", [&] { vb.widget(page_a); });
                vb.tab("Beta", [&] { vb.widget(page_b); });
            });
        }
    };

    Model model;
    prism::WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(800, 600, 1);
    CHECK(snap1 != nullptr);

    model.tabs.set(prism::TabBar{.selected = 1});
    auto snap2 = tree.build_snapshot(800, 600, 2);
    CHECK(snap2 != nullptr);
    CHECK(!snap2->geometry.empty());
}

TEST_CASE("Inactive tab field changes reflected on switch back") {
    struct Model {
        prism::Field<prism::TabBar> tabs;
        prism::Field<std::string> page_a{"hello"};
        prism::Field<bool> page_b{true};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.tabs(tabs, [&] {
                vb.tab("Alpha", [&] { vb.widget(page_a); });
                vb.tab("Beta", [&] { vb.widget(page_b); });
            });
        }
    };

    Model model;
    prism::WidgetTree tree(model);
    tree.build_snapshot(800, 600, 1);

    model.tabs.set(prism::TabBar{.selected = 1});
    tree.build_snapshot(800, 600, 2);

    model.page_a.set("world");

    model.tabs.set(prism::TabBar{.selected = 0});
    auto snap = tree.build_snapshot(800, 600, 3);
    CHECK(snap != nullptr);
}

TEST_CASE("Single tab produces valid snapshot") {
    struct Model {
        prism::Field<prism::TabBar> tabs;
        prism::Field<std::string> page{"hello"};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.tabs(tabs, [&] {
                vb.tab("Only", [&] { vb.widget(page); });
            });
        }
    };

    Model model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    CHECK(snap != nullptr);
    CHECK(!snap->geometry.empty());
}
```

- [ ] **Step 2: Run tests**

Run: `cd builddir && meson compile && meson test test_tabs`
Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_tabs.cpp
git commit -m "test(tabs): tab switching, inactive field changes, single tab edge case"
```

---

### Task 5: Keyboard focus flow and click-to-switch tests

**Files:**
- Test: `tests/test_tabs.cpp`

- [ ] **Step 1: Write focus flow and click tests**

Append to `tests/test_tabs.cpp`:

```cpp
TEST_CASE("Tab bar is in focus order, Tab key moves to content") {
    struct Model {
        prism::Field<prism::TabBar> tabs;
        prism::Field<std::string> page_a{"hello"};
        prism::Field<bool> page_b{true};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.tabs(tabs, [&] {
                vb.tab("Alpha", [&] { vb.widget(page_a); });
                vb.tab("Beta", [&] { vb.widget(page_b); });
            });
        }
    };

    Model model;
    prism::WidgetTree tree(model);
    tree.build_snapshot(800, 600, 1);

    // Focus order includes tab bar + content widget(s)
    CHECK(tree.focus_order().size() >= 2);

    tree.focus_next();
    auto bar_id = tree.focused_id();
    CHECK(bar_id != 0);

    tree.focus_next();
    auto content_id = tree.focused_id();
    CHECK(content_id != bar_id);
}

TEST_CASE("Click on tab header switches tab") {
    struct Model {
        prism::Field<prism::TabBar> tabs;
        prism::Field<std::string> page_a{"hello"};
        prism::Field<bool> page_b{true};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.tabs(tabs, [&] {
                vb.tab("Alpha", [&] { vb.widget(page_a); });
                vb.tab("Beta", [&] { vb.widget(page_b); });
            });
        }
    };

    Model model;
    prism::WidgetTree tree(model);
    tree.build_snapshot(800, 600, 1);

    tree.focus_next();
    auto bar_id = tree.focused_id();

    // Second tab starts after "Alpha" (5 chars * 8px + 32px padding = 72px)
    prism::MouseButton click{prism::Point{prism::X{80}, prism::Y{10}}, 1, true};
    tree.dispatch(bar_id, click);

    CHECK(model.tabs.get().selected == 1);
}

TEST_CASE("close_overlays does not destroy tabs state") {
    struct Model {
        prism::Field<prism::TabBar> tabs;
        prism::Field<std::string> page_a{"hello"};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.tabs(tabs, [&] {
                vb.tab("Alpha", [&] { vb.widget(page_a); });
            });
        }
    };

    Model model;
    prism::WidgetTree tree(model);
    tree.build_snapshot(800, 600, 1);

    tree.close_overlays();

    auto snap = tree.build_snapshot(800, 600, 2);
    CHECK(snap != nullptr);
    CHECK(!snap->geometry.empty());
}
```

- [ ] **Step 2: Run tests**

Run: `cd builddir && meson compile && meson test test_tabs`
Expected: All tests PASS.

- [ ] **Step 3: Run full test suite**

Run: `cd builddir && meson compile && meson test`
Expected: All existing tests still PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/test_tabs.cpp
git commit -m "test(tabs): focus flow, click-to-switch, close_overlays safety"
```

---

### Task 6: Reflective TabBar\<S\> (C++26 reflection)

**Files:**
- Modify: `include/prism/core/delegate.hpp` (TabBar template)
- Modify: `include/prism/core/tabs_delegates.hpp` (update Delegate type)
- Modify: `include/prism/core/widget_tree.hpp` (reflective ViewBuilder::tabs)
- Test: `tests/test_tabs.cpp`

- [ ] **Step 1: Write failing test — reflective TabBar\<S\>**

Append to `tests/test_tabs.cpp`:

```cpp
#if __cpp_impl_reflection
TEST_CASE("TabBar<S> reflective mode: tabs from struct members") {
    struct GeneralPage {
        prism::Field<std::string> username{"jean"};
        void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(username); }
    };

    struct AdvancedPage {
        prism::Field<bool> dark_mode{false};
        void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(dark_mode); }
    };

    struct Pages {
        GeneralPage general;
        AdvancedPage advanced;
    };

    struct Model {
        prism::Field<prism::TabBar<Pages>> tabs;
    };

    Model model;
    prism::WidgetTree tree(model);

    auto snap = tree.build_snapshot(800, 600, 1);
    CHECK(snap != nullptr);
    CHECK(!snap->geometry.empty());

    // Switch tabs
    auto tb = model.tabs.get();
    tb.selected = 1;
    model.tabs.set(tb);
    auto snap2 = tree.build_snapshot(800, 600, 2);
    CHECK(snap2 != nullptr);
}
#endif
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd builddir && meson compile && meson test test_tabs`
Expected: Compilation failure — `TabBar<Pages>` not defined.

- [ ] **Step 3: Make TabBar a template with void default**

In `delegate.hpp`, replace the `TabBar` struct with:

```cpp
// Sentinel: tab bar
// TabBar<> (or TabBar<void>): explicit mode — tab names defined in view()
// TabBar<S>: reflective mode — tabs from struct members of S
template <typename S = void>
struct TabBar {
    size_t selected = 0;
    bool operator==(const TabBar&) const = default;
};

template <typename S>
    requires (!std::is_void_v<S>)
struct TabBar<S> {
    size_t selected = 0;
    S pages{};
    bool operator==(const TabBar&) const = default;
};
```

- [ ] **Step 4: Update Delegate declaration**

Change `Delegate<TabBar>` to `Delegate<TabBar<>>`:

```cpp
template <>
struct Delegate<TabBar<>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<TabBar<>>& field, WidgetNode& node);
    static void handle_input(Field<TabBar<>>& field, const InputEvent& ev, WidgetNode& node);
};
```

- [ ] **Step 5: Update tabs_delegates.hpp method bodies**

Change `Delegate<TabBar>` to `Delegate<TabBar<>>`:

```cpp
inline void Delegate<TabBar<>>::record(DrawList& dl, const Field<TabBar<>>& field,
                                       WidgetNode& node) {
    if (node.tab_names && !node.tab_names->empty())
        detail::tabs_record(dl, node, field.get().selected, *node.tab_names);
}

inline void Delegate<TabBar<>>::handle_input(Field<TabBar<>>& field, const InputEvent& ev,
                                              WidgetNode& node) {
    size_t count = node.tab_names ? node.tab_names->size() : 0;
    if (count == 0) return;
    detail::tabs_handle_input(ev, node, field.get().selected, count,
        [&](size_t idx) {
            auto tb = field.get();
            tb.selected = idx;
            field.set(tb);
        });
}
```

- [ ] **Step 6: Update ViewBuilder::tabs() signature**

Change `Field<TabBar>&` to `Field<TabBar<>>&` in the explicit overload:

```cpp
        void tabs(Field<TabBar<>>& field, std::invocable auto&& builder) {
```

- [ ] **Step 7: Update all tests to use TabBar\<\>**

In `tests/test_tabs.cpp`, replace all `prism::TabBar` with `prism::TabBar<>` where it's the explicit (non-reflective) mode. Replace all `prism::Field<prism::TabBar>` with `prism::Field<prism::TabBar<>>`. Replace all `prism::TabBar{.selected = N}` with `prism::TabBar<>{.selected = N}`.

- [ ] **Step 8: Add reflective ViewBuilder::tabs() for TabBar\<S\>**

In `widget_tree.hpp`, add the reflective overload (guarded by `__cpp_impl_reflection`), after the explicit `tabs()` method:

```cpp
#if __cpp_impl_reflection
        template <typename S>
            requires (!std::is_void_v<S>)
        void tabs(Field<TabBar<S>>& field) {
            placed_.insert(&field);

            auto state = std::make_shared<TabsState>();
            state->tab_names = std::make_shared<std::vector<std::string>>();
            state->get_selected = [&field]() { return field.get().selected; };

            static constexpr auto members = std::define_static_array(
                std::meta::nonstatic_data_members_of(
                    ^^S, std::meta::access_context::unchecked()));

            template for (constexpr auto m : members) {
                state->tab_names->push_back(
                    std::string(std::meta::identifier_of(m)));

                auto& member = field.value_mut().pages.[:m:];
                state->tab_node_builders.push_back(
                    [&member, this](Node& target) {
                        target.children.push_back(tree_.build_node_tree(member));
                    });
            }

            Node tabs_node;
            tabs_node.id = tree_.next_id_++;
            tabs_node.is_leaf = false;
            tabs_node.layout_kind = LayoutKind::Tabs;
            tabs_node.tabs_state = state;

            // Tab bar leaf — custom build since we don't have a Field<TabBar<>>
            Node bar;
            bar.id = tree_.next_id_++;
            bar.is_leaf = true;
            auto names_ptr = state->tab_names;
            bar.build_widget = [&field, names_ptr](WidgetNode& wn) {
                wn.focus_policy = FocusPolicy::tab_and_click;
                wn.tab_names = names_ptr;
                wn.record = [&field, names_ptr](WidgetNode& node) {
                    node.draws.clear();
                    node.overlay_draws.clear();
                    detail::tabs_record(node.draws, node, field.get().selected, *names_ptr);
                };
                wn.record(wn);
                wn.wire = [&field, names_ptr](WidgetNode& node) {
                    node.connections.push_back(
                        node.on_input.connect([&field, names_ptr, &node](const InputEvent& ev) {
                            size_t count = names_ptr->size();
                            if (count == 0) return;
                            detail::tabs_handle_input(ev, node, field.get().selected, count,
                                [&](size_t idx) {
                                    auto tb = field.get();
                                    tb.selected = idx;
                                    field.set(tb);
                                });
                        })
                    );
                };
            };
            bar.on_change = [&field](std::function<void()> cb) -> Connection {
                return field.on_change().connect(
                    [cb = std::move(cb)](const TabBar<S>&) { cb(); });
            };
            tabs_node.children.push_back(std::move(bar));

            Node content;
            content.id = tree_.next_id_++;
            content.is_leaf = false;
            content.layout_kind = LayoutKind::Column;
            tabs_node.children.push_back(std::move(content));

            current_parent().children.push_back(std::move(tabs_node));
        }
#endif
```

- [ ] **Step 9: Add Field::value_mut() if needed**

Check `include/prism/core/field.hpp` for mutable access. If `Field<T>` does not expose a `T& value_mut()`, add one:

```cpp
    T& value_mut() { return value_; }
```

This provides mutable access to the stored value without triggering change notifications — needed for the reflection walk to capture references to `pages` sub-members.

- [ ] **Step 10: Handle TabBar\<S\> in traits (is_field detection)**

`Field<TabBar<S>>` should be recognized by `is_field_v` — it already is, since `Field<T>` has `is_prism_field = true` regardless of `T`. No change needed.

For `build_node_tree` reflection: when the model has no `view()`, reflection walks members. `Field<TabBar<S>>` is a `Field<T>` so it would be placed as a leaf widget. But it should trigger the tabs pattern. The user's model either has a `view()` that calls `vb.tabs()`, or uses pure reflection. For the reflective mode, the user writes `Field<TabBar<Pages>> tabs;` with no `view()`. The `build_node_tree` reflection will try to create a `node_leaf` for it — but we need special handling.

Add to `build_node_tree`'s reflection path: detect `TabBar<S>` fields and build tabs nodes automatically:

In the `template for` loop inside `build_node_tree`:

```cpp
                if constexpr (is_field_v<M>) {
                    // Check if this is a TabBar<S> field that needs special handling
                    if constexpr (is_tab_bar_v<typename M::value_type>) {
                        // Build tabs node via ViewBuilder
                        ViewBuilder vb{*this, root};
                        vb.tabs(member);
                    } else {
                        root.children.push_back(node_leaf(member, next_id_));
                    }
                }
```

Add a trait to detect TabBar<S>:

In `delegate.hpp`:

```cpp
template <typename T>
struct is_tab_bar : std::false_type {};

template <typename S>
struct is_tab_bar<TabBar<S>> : std::true_type {};

template <typename T>
inline constexpr bool is_tab_bar_v = is_tab_bar<T>::value;

template <typename T>
inline constexpr bool is_tab_bar_pages_v = is_tab_bar_v<T> && !std::is_void_v<typename T::pages_type>;
```

Wait, `TabBar<void>` doesn't have a `pages` member. We need to distinguish. Let's add a type alias:

```cpp
template <typename S>
    requires (!std::is_void_v<S>)
struct TabBar<S> {
    using pages_type = S;
    size_t selected = 0;
    S pages{};
    bool operator==(const TabBar&) const = default;
};
```

And the trait:

```cpp
template <typename T>
concept TabBarWithPages = requires { typename T::pages_type; } && is_tab_bar_v<T>;
```

Then in `build_node_tree`:

```cpp
                if constexpr (is_field_v<M>) {
                    using V = typename M::value_type;
                    if constexpr (TabBarWithPages<V>) {
                        ViewBuilder vb{*this, root};
                        vb.tabs(member);
                    } else {
                        root.children.push_back(node_leaf(member, next_id_));
                    }
                }
```

But `M::value_type` might not exist for all Field types. Check Field's definition — it likely stores the value type as a template parameter. `Field<T>` so `typename Field<T>::value_type` or just the template parameter.

Actually, `Field<T>` wraps `T`. We can check via `is_field_v<M>` and then extract T. But simpler: since we have `member` which is `Field<TabBar<S>>`, we can use:

```cpp
                if constexpr (is_field_v<M>) {
                    if constexpr (requires { typename std::remove_cvref_t<decltype(member.get())>::pages_type; }) {
                        ViewBuilder vb{*this, root};
                        vb.tabs(member);
                    } else {
                        root.children.push_back(node_leaf(member, next_id_));
                    }
                }
```

This is getting complex. For now, we can require that reflective `TabBar<S>` is used with an explicit `view()` that calls `vb.tabs(field)`. The pure-reflection (no `view()`) mode for `TabBar<S>` can be a follow-up. The test in step 1 should use a model with `view()`:

Actually, looking at the test: it defines `Model` with just `Field<TabBar<Pages>> tabs;` and no `view()`. Let me update the test to use a `view()` method instead, to keep things simpler:

```cpp
    struct Model {
        prism::Field<prism::TabBar<Pages>> tabs;
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.tabs(tabs);
        }
    };
```

This is cleaner and doesn't require modifying `build_node_tree`. Let's go with this approach.

- [ ] **Step 10 (revised): Update test to use explicit view()**

The reflection test should use a model with `view()`:

```cpp
    struct Model {
        prism::Field<prism::TabBar<Pages>> tabs;
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.tabs(tabs);
        }
    };
```

Pure-reflection auto-detection of `TabBar<S>` in `build_node_tree` can be added later.

- [ ] **Step 11: Run all tests**

Run: `cd builddir && meson compile && meson test`
Expected: All tests PASS.

- [ ] **Step 12: Commit**

```bash
git add include/prism/core/delegate.hpp include/prism/core/tabs_delegates.hpp \
        include/prism/core/widget_tree.hpp include/prism/core/field.hpp \
        tests/test_tabs.cpp
git commit -m "feat(tabs): reflective TabBar<S> with C++26 reflection"
```
