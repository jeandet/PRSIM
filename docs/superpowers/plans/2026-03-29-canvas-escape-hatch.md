# Canvas Escape Hatch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `vb.canvas(model)` to ViewBuilder so model structs can do custom DrawList rendering in an expandable canvas area, plus a Waveform demo in the dashboard.

**Architecture:** Canvas nodes are leaf WidgetNodes with `LayoutKind::Canvas`. They expand like spacers but carry draw commands. Their `record()` runs post-layout (after bounds are known) via a new `record_canvas_nodes()` pass. Dirty tracking uses explicit `.depends_on(field)` connections.

**Tech Stack:** C++26 (P2996 reflection), doctest, Meson

---

### Task 1: Add LayoutKind::Canvas and LayoutNode::Kind::Canvas

**Files:**
- Modify: `include/prism/core/widget_tree.hpp:43` (WidgetNode::LayoutKind enum)
- Modify: `include/prism/core/layout.hpp:30` (LayoutNode::Kind enum)
- Modify: `include/prism/core/widget_tree.hpp:930-932` (count_leaves)
- Modify: `include/prism/core/widget_tree.hpp:959-962` (collect_leaf_ids)
- Test: `tests/test_view.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_view.cpp`:

```cpp
TEST_CASE("WidgetNode LayoutKind::Canvas exists") {
    prism::WidgetNode node;
    node.layout_kind = prism::WidgetNode::LayoutKind::Canvas;
    CHECK(node.layout_kind == prism::WidgetNode::LayoutKind::Canvas);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test view -C builddir --print-errorlogs`
Expected: FAIL — `Canvas` is not a member of `LayoutKind`

- [ ] **Step 3: Add Canvas to LayoutKind and LayoutNode::Kind**

In `include/prism/core/widget_tree.hpp`, change the LayoutKind enum on WidgetNode (line 43):

```cpp
    enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas } layout_kind = LayoutKind::Default;
```

In `include/prism/core/layout.hpp`, change LayoutNode::Kind (line 30):

```cpp
    enum class Kind { Leaf, Row, Column, Spacer, Canvas } kind = Kind::Leaf;
```

- [ ] **Step 4: Update count_leaves and collect_leaf_ids to handle Canvas**

Canvas nodes ARE leaves (they render content), so they should be counted. In `include/prism/core/widget_tree.hpp`, update `count_leaves` (line ~930):

```cpp
    static size_t count_leaves(const WidgetNode& node) {
        if (!node.is_container) {
            auto lk = node.layout_kind;
            return (lk == WidgetNode::LayoutKind::Spacer) ? 0 : 1;
        }
        size_t n = 0;
        for (auto& c : node.children) n += count_leaves(c);
        return n;
    }
```

Update `collect_leaf_ids` (line ~959):

```cpp
    static void collect_leaf_ids(const WidgetNode& node, std::vector<WidgetId>& ids) {
        if (!node.is_container) {
            if (node.layout_kind != WidgetNode::LayoutKind::Spacer)
                ids.push_back(node.id);
            return;
        }
        for (auto& c : node.children) collect_leaf_ids(c, ids);
    }
```

(These already exclude Spacer only — Canvas passes through. No change actually needed here since the condition is `!= Spacer`. Just verify.)

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test view -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/widget_tree.hpp include/prism/core/layout.hpp tests/test_view.cpp
git commit -m "feat: add LayoutKind::Canvas and LayoutNode::Kind::Canvas enum values"
```

---

### Task 2: Canvas node in build_layout and layout_measure

**Files:**
- Modify: `include/prism/core/widget_tree.hpp:982-1011` (build_layout)
- Modify: `include/prism/core/layout.hpp:33-84` (layout_measure)
- Modify: `include/prism/core/widget_tree.hpp:29-44` (WidgetNode — add canvas_bounds)
- Test: `tests/test_view.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_view.cpp`:

```cpp
struct CanvasModel {
    prism::Field<int> a{0};

    void canvas(prism::DrawList&, prism::Rect, const prism::WidgetNode&) {}

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(a);
        vb.canvas(*this);
    }
};

TEST_CASE("canvas node expands to fill remaining space") {
    CanvasModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);  // widget a + canvas

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 2);

    // Canvas should get remaining vertical space after widget a
    auto& [id_a, r_a] = snap->geometry[0];
    auto& [id_canvas, r_canvas] = snap->geometry[1];
    CHECK(r_canvas.origin.y.raw() >= r_a.origin.y.raw() + r_a.extent.h.raw());
    CHECK(r_canvas.extent.h.raw() > 0);
    // Canvas should fill the width
    CHECK(r_canvas.extent.w.raw() == doctest::Approx(800));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test view -C builddir --print-errorlogs`
Expected: FAIL — `canvas` is not a member of `ViewBuilder`

- [ ] **Step 3: Add canvas_bounds field to WidgetNode**

In `include/prism/core/widget_tree.hpp`, add to `WidgetNode` struct (after `overlay_draws`):

```cpp
    Rect canvas_bounds{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
```

- [ ] **Step 4: Add canvas handling to build_layout**

In `include/prism/core/widget_tree.hpp`, update `build_layout` to handle Canvas nodes. Replace the leaf branch (lines ~985-998):

```cpp
    static void build_layout(WidgetNode& node, LayoutNode& parent) {
        using LK = WidgetNode::LayoutKind;

        if (!node.is_container) {
            if (node.layout_kind == LK::Spacer) {
                LayoutNode spacer;
                spacer.kind = LayoutNode::Kind::Spacer;
                spacer.id = node.id;
                parent.children.push_back(std::move(spacer));
            } else if (node.layout_kind == LK::Canvas) {
                LayoutNode canvas;
                canvas.kind = LayoutNode::Kind::Canvas;
                canvas.id = node.id;
                canvas.draws = node.draws;
                canvas.overlay_draws = node.overlay_draws;
                parent.children.push_back(std::move(canvas));
            } else {
                LayoutNode leaf;
                leaf.kind = LayoutNode::Kind::Leaf;
                leaf.id = node.id;
                leaf.draws = node.draws;
                leaf.overlay_draws = node.overlay_draws;
                parent.children.push_back(std::move(leaf));
            }
        } else if (node.layout_kind == LK::Row || node.layout_kind == LK::Column) {
            LayoutNode container;
            container.kind = (node.layout_kind == LK::Row)
                ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
            container.id = node.id;
            for (auto& c : node.children)
                build_layout(c, container);
            parent.children.push_back(std::move(container));
        } else {
            for (auto& c : node.children)
                build_layout(c, parent);
        }
    }
```

- [ ] **Step 5: Add Canvas handling to layout_measure**

In `include/prism/core/layout.hpp`, add a `Canvas` case in `layout_measure` after the `Spacer` case:

```cpp
    case LayoutNode::Kind::Canvas:
        node.hint = {.preferred = 0, .expand = true};
        return;
```

- [ ] **Step 6: Add Canvas handling to layout_flatten**

In `include/prism/core/layout.hpp`, update `layout_flatten` — Canvas nodes are treated like leaves (they already have draws). Add at the top of `layout_flatten`, right after the Spacer early return:

```cpp
inline void layout_flatten(LayoutNode& node, SceneSnapshot& snap) {
    if (node.kind == LayoutNode::Kind::Spacer) return;

    if (!node.draws.empty() || node.kind == LayoutNode::Kind::Canvas) {
        DX dx{node.allocated.origin.x.raw()};
        DY dy{node.allocated.origin.y.raw()};
        detail::translate_draw_list(node.draws, dx, dy);
        auto idx = static_cast<uint16_t>(snap.geometry.size());
        snap.geometry.push_back({node.id, node.allocated});
        snap.draw_lists.push_back(std::move(node.draws));
        snap.z_order.push_back(idx);
    }

    if (!node.overlay_draws.empty()) {
        DX dx{node.allocated.origin.x.raw()};
        DY dy{node.allocated.origin.y.raw()};
        detail::translate_draw_list(node.overlay_draws, dx, dy);
        snap.overlay_geometry.push_back({node.id, node.overlay_draws.bounding_box()});
        for (auto& cmd : node.overlay_draws.commands)
            snap.overlay.commands.push_back(std::move(cmd));
    }

    for (auto& child : node.children) {
        layout_flatten(child, snap);
    }
}
```

The key change: canvas nodes always emit geometry (even with empty draws), so hit testing works on them.

- [ ] **Step 7: Add minimal ViewBuilder::canvas() stub**

In `include/prism/core/widget_tree.hpp`, add to `ViewBuilder` (after the `spacer()` method):

```cpp
        template <typename T>
            requires requires(T& t, DrawList& dl, Rect r, const WidgetNode& n) {
                t.canvas(dl, r, n);
            }
        auto canvas(T& model) {
            WidgetNode node;
            node.id = tree_.next_id_++;
            node.is_container = false;
            node.layout_kind = WidgetNode::LayoutKind::Canvas;

            node.record = [&model](WidgetNode& n) {
                n.draws.clear();
                model.canvas(n.draws, n.canvas_bounds, n);
            };
            // Initial record with zero bounds (placeholder — real record happens post-layout)
            node.record(node);

            current_parent().children.push_back(std::move(node));

            struct CanvasHandle {
                WidgetNode& node_ref;
                WidgetTree& tree_ref;

                template <typename U>
                CanvasHandle& depends_on(Field<U>& field) {
                    auto id = node_ref.id;
                    node_ref.connections.push_back(
                        field.on_change().connect([&tree_ref = tree_ref, id](const U&) {
                            tree_ref.mark_dirty_by_id(id);
                        })
                    );
                    return *this;
                }
            };
            return CanvasHandle{current_parent().children.back(), tree_};
        }
```

- [ ] **Step 8: Expose mark_dirty_by_id as package-private helper**

In `include/prism/core/widget_tree.hpp`, add a public method to WidgetTree (after `focus_prev()`):

```cpp
    void mark_dirty_by_id(WidgetId id) {
        mark_dirty(root_, id);
    }
```

- [ ] **Step 9: Run test to verify it passes**

Run: `meson test view -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 10: Commit**

```bash
git add include/prism/core/widget_tree.hpp include/prism/core/layout.hpp tests/test_view.cpp
git commit -m "feat: canvas node in ViewBuilder with expand layout and geometry"
```

---

### Task 3: Post-layout canvas re-record in build_snapshot

**Files:**
- Modify: `include/prism/core/widget_tree.hpp:866-885` (build_snapshot)
- Test: `tests/test_view.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_view.cpp`:

```cpp
struct DrawingCanvasModel {
    prism::Field<int> value{42};

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        // Draw a filled rect using the actual bounds
        dl.filled_rect(bounds, prism::Color::rgba(255, 0, 0));
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(value);
        vb.canvas(*this).depends_on(value);
    }
};

TEST_CASE("canvas record() receives allocated bounds from layout") {
    DrawingCanvasModel model;
    prism::WidgetTree tree(model);

    auto snap = tree.build_snapshot(400, 300, 1);
    REQUIRE(snap->geometry.size() == 2);

    auto& [id_canvas, r_canvas] = snap->geometry[1];
    // The canvas should have draws (filled_rect) with the actual bounds
    auto& canvas_draws = snap->draw_lists[1];
    REQUIRE_FALSE(canvas_draws.empty());

    // The draw command should use the allocated rect (translated to screen coords)
    auto& cmd = canvas_draws.commands[0];
    auto* fr = std::get_if<prism::FilledRect>(&cmd);
    REQUIRE(fr != nullptr);
    // The rect should cover the canvas area (starts at canvas origin, has canvas size)
    CHECK(fr->rect.extent.w.raw() > 0);
    CHECK(fr->rect.extent.h.raw() > 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test view -C builddir --print-errorlogs`
Expected: FAIL — canvas draws with zero-bounds rect (no post-layout re-record yet)

- [ ] **Step 3: Add post-layout canvas re-record to build_snapshot**

The approach: after `layout_arrange()`, walk the layout tree to find Canvas nodes, update their `canvas_bounds` on the corresponding WidgetNode, and call `record()`. Then rebuild the layout draws from the re-recorded WidgetNodes.

In `include/prism/core/widget_tree.hpp`, replace `build_snapshot`:

```cpp
    [[nodiscard]] std::unique_ptr<SceneSnapshot> build_snapshot(float w, float h, uint64_t version) {
        refresh_dirty(root_);

        LayoutNode layout;
        assert(root_.layout_kind != WidgetNode::LayoutKind::Spacer);
        layout.kind = (root_.layout_kind == WidgetNode::LayoutKind::Row)
            ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
        layout.id = root_.id;
        for (auto& c : root_.children)
            build_layout(c, layout);

        layout_measure(layout, LayoutAxis::Vertical);
        layout_arrange(layout, {Point{X{0}, Y{0}}, Size{Width{w}, Height{h}}});

        // Post-layout: update canvas nodes with their resolved bounds and re-record
        update_canvas_bounds(layout, root_);

        auto snap = std::make_unique<SceneSnapshot>();
        snap->version = version;
        layout_flatten(layout, *snap);
        return snap;
    }
```

Add private helper `update_canvas_bounds`:

```cpp
    static void update_canvas_bounds(LayoutNode& layout_node, WidgetNode& widget_root) {
        if (layout_node.kind == LayoutNode::Kind::Canvas) {
            auto* wn = find_widget_node(widget_root, layout_node.id);
            if (wn && wn->record) {
                wn->canvas_bounds = Rect{
                    Point{X{0}, Y{0}},
                    layout_node.allocated.extent
                };
                wn->record(*wn);
                layout_node.draws = wn->draws;
            }
            return;
        }
        for (auto& child : layout_node.children)
            update_canvas_bounds(child, widget_root);
    }

    static WidgetNode* find_widget_node(WidgetNode& node, WidgetId id) {
        if (node.id == id) return &node;
        for (auto& c : node.children) {
            auto* found = find_widget_node(c, id);
            if (found) return found;
        }
        return nullptr;
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test view -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_view.cpp
git commit -m "feat: post-layout canvas re-record with resolved bounds"
```

---

### Task 4: Canvas dirty tracking via depends_on

**Files:**
- Test: `tests/test_view.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_view.cpp`:

```cpp
TEST_CASE("canvas depends_on triggers dirty on field change") {
    DrawingCanvasModel model;
    prism::WidgetTree tree(model);

    tree.clear_dirty();
    CHECK_FALSE(tree.any_dirty());

    model.value.set(99);
    CHECK(tree.any_dirty());
}

TEST_CASE("canvas depends_on supports multiple fields") {
    struct MultiDepCanvas {
        prism::Field<int> x{0};
        prism::Field<int> y{0};

        void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
            dl.filled_rect(bounds, prism::Color::rgba(0, 0, 0));
        }

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.canvas(*this).depends_on(x).depends_on(y);
        }
    };

    MultiDepCanvas model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 1);

    tree.clear_dirty();
    model.x.set(1);
    CHECK(tree.any_dirty());

    tree.clear_dirty();
    model.y.set(2);
    CHECK(tree.any_dirty());
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `meson test view -C builddir --print-errorlogs`
Expected: PASS (depends_on was implemented in Task 2)

If it passes, this confirms the feature works end-to-end.

- [ ] **Step 3: Commit**

```bash
git add tests/test_view.cpp
git commit -m "test: canvas dirty tracking via depends_on"
```

---

### Task 5: Canvas input handling (handle_canvas_input)

**Files:**
- Modify: `include/prism/core/widget_tree.hpp` (ViewBuilder::canvas — wire and focus_policy)
- Test: `tests/test_view.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_view.cpp`:

```cpp
struct InteractiveCanvas {
    prism::Field<int> click_count{0};
    bool input_received = false;

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        dl.filled_rect(bounds, prism::Color::rgba(30, 30, 40));
    }

    void handle_canvas_input(const prism::InputEvent& ev, prism::WidgetNode&, prism::Rect) {
        if (std::get_if<prism::MouseButton>(&ev))
            input_received = true;
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.canvas(*this).depends_on(click_count);
    }
};

TEST_CASE("canvas with handle_canvas_input is focusable") {
    InteractiveCanvas model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    CHECK(focus.size() == 1);
}

TEST_CASE("canvas dispatches input to handle_canvas_input") {
    InteractiveCanvas model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    REQUIRE(ids.size() == 1);

    prism::MouseButton click{prism::Point{prism::X{50}, prism::Y{50}}, 1, true};
    tree.dispatch(ids[0], click);

    CHECK(model.input_received);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test view -C builddir --print-errorlogs`
Expected: FAIL — canvas node has no focus_policy or wire

- [ ] **Step 3: Add input handling to ViewBuilder::canvas**

Update the `canvas()` method in ViewBuilder to detect and wire `handle_canvas_input`. Replace the canvas method from Task 2 with this expanded version:

```cpp
        template <typename T>
            requires requires(T& t, DrawList& dl, Rect r, const WidgetNode& n) {
                t.canvas(dl, r, n);
            }
        auto canvas(T& model) {
            WidgetNode node;
            node.id = tree_.next_id_++;
            node.is_container = false;
            node.layout_kind = WidgetNode::LayoutKind::Canvas;

            node.record = [&model](WidgetNode& n) {
                n.draws.clear();
                model.canvas(n.draws, n.canvas_bounds, n);
            };
            node.record(node);

            if constexpr (requires(T& t, const InputEvent& ev, WidgetNode& n, Rect r) {
                               t.handle_canvas_input(ev, n, r);
                           }) {
                node.focus_policy = FocusPolicy::tab_and_click;
                node.wire = [&model](WidgetNode& n) {
                    n.connections.push_back(
                        n.on_input.connect([&model, &n](const InputEvent& ev) {
                            model.handle_canvas_input(ev, n, n.canvas_bounds);
                        })
                    );
                };
            }

            current_parent().children.push_back(std::move(node));

            struct CanvasHandle {
                WidgetNode& node_ref;
                WidgetTree& tree_ref;

                template <typename U>
                CanvasHandle& depends_on(Field<U>& field) {
                    auto id = node_ref.id;
                    node_ref.connections.push_back(
                        field.on_change().connect([&tree_ref = tree_ref, id](const U&) {
                            tree_ref.mark_dirty_by_id(id);
                        })
                    );
                    return *this;
                }
            };
            return CanvasHandle{current_parent().children.back(), tree_};
        }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test view -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_view.cpp
git commit -m "feat: canvas input handling via handle_canvas_input with focus support"
```

---

### Task 6: Canvas alongside other widgets in row/column layouts

**Files:**
- Test: `tests/test_view.cpp`

- [ ] **Step 1: Write the test**

Add to `tests/test_view.cpp`:

```cpp
struct CanvasInRow {
    prism::Field<int> a{0};

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        dl.filled_rect(bounds, prism::Color::rgba(0, 100, 0));
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.widget(a);
            vb.canvas(*this);
        });
    }
};

TEST_CASE("canvas in row expands horizontally") {
    CanvasInRow model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap->geometry.size() == 2);

    auto& [id_a, r_a] = snap->geometry[0];
    auto& [id_canvas, r_canvas] = snap->geometry[1];

    // Same y (row), canvas starts after widget a
    CHECK(r_canvas.origin.y.raw() == r_a.origin.y.raw());
    CHECK(r_canvas.origin.x.raw() >= r_a.origin.x.raw() + r_a.extent.w.raw());
    // Canvas fills remaining width
    float expected_w = 800 - r_a.extent.w.raw();
    CHECK(r_canvas.extent.w.raw() == doctest::Approx(expected_w));
}

struct CanvasOnlyModel {
    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        dl.filled_rect(bounds, prism::Color::rgba(0, 0, 100));
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.canvas(*this);
    }
};

TEST_CASE("canvas-only model fills entire viewport") {
    CanvasOnlyModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 1);

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap->geometry.size() == 1);

    auto& [id, r] = snap->geometry[0];
    CHECK(r.extent.w.raw() == doctest::Approx(800));
    CHECK(r.extent.h.raw() == doctest::Approx(600));
}
```

- [ ] **Step 2: Run tests to verify they pass**

Run: `meson test view -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add tests/test_view.cpp
git commit -m "test: canvas in row layout and canvas-only model"
```

---

### Task 7: Run full test suite

**Files:** None (verification only)

- [ ] **Step 1: Run all tests**

Run: `meson test -C builddir --print-errorlogs`
Expected: All tests PASS. No regressions in existing layout, widget_tree, delegate, dropdown, text_field, password, text_area, or view tests.

- [ ] **Step 2: Fix any regressions**

If any test fails, diagnose and fix. The most likely issue: `layout_flatten` change affecting non-canvas nodes. Verify the condition `!node.draws.empty() || node.kind == LayoutNode::Kind::Canvas` doesn't emit extra geometry for empty non-canvas nodes.

---

### Task 8: Waveform demo in dashboard

**Files:**
- Modify: `examples/model_dashboard.cpp`

- [ ] **Step 1: Add Waveform struct to dashboard**

In `examples/model_dashboard.cpp`, add the `Waveform` struct after the `Settings` struct and before `Dashboard`:

```cpp
struct Waveform {
    prism::Field<prism::Slider<>> frequency{{.value = 2.0, .min = 0.5, .max = 10.0}};
    prism::Field<prism::Slider<>> amplitude{{.value = 0.8, .min = 0.0, .max = 1.0}};

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        auto w = bounds.extent.w.raw();
        auto h = bounds.extent.h.raw();

        // Background
        dl.filled_rect(bounds, prism::Color::rgba(20, 22, 30));

        // Center line
        float cy = h * 0.5f;
        dl.filled_rect(
            prism::Rect{prism::Point{prism::X{0}, prism::Y{cy}},
                        prism::Size{prism::Width{w}, prism::Height{1}}},
            prism::Color::rgba(60, 60, 80));

        // Sine wave as vertical bars
        float freq = frequency.get().value;
        float amp = amplitude.get().value;
        int steps = std::max(1, static_cast<int>(w / 3));
        float bar_w = w / static_cast<float>(steps);

        for (int i = 0; i < steps; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(steps);
            float y_val = amp * std::sin(2.0f * 3.14159265f * freq * t);
            float bar_h = std::abs(y_val) * h * 0.45f;
            float bar_y = y_val > 0 ? cy - bar_h : cy;

            auto green = static_cast<uint8_t>(80 + 175 * std::abs(y_val));
            dl.filled_rect(
                prism::Rect{
                    prism::Point{prism::X{i * bar_w}, prism::Y{bar_y}},
                    prism::Size{prism::Width{std::max(bar_w - 1, 1.0f)},
                                prism::Height{bar_h}}},
                prism::Color::rgba(0, green, 80));
        }
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.widget(frequency);
            vb.widget(amplitude);
        });
        vb.canvas(*this).depends_on(frequency).depends_on(amplitude);
    }
};
```

- [ ] **Step 2: Add Waveform to Dashboard struct**

Add `Waveform waveform;` to the `Dashboard` struct:

```cpp
struct Dashboard {
    Settings settings;
    Waveform waveform;
    prism::Field<prism::Label<>> status{{"All systems go"}};
    prism::Field<prism::TextArea<>> notes{{.placeholder = "Notes...", .rows = 4}};
    prism::Field<prism::Button> increment{{"Increment"}};
    prism::Field<int> counter{0};
    prism::State<int> request_count{0};
};
```

- [ ] **Step 3: Add cmath include**

Add at the top of the file:

```cpp
#include <cmath>
```

- [ ] **Step 4: Build and run the dashboard**

Run: `meson compile -C builddir && ./builddir/examples/model_dashboard`
Expected: Dashboard shows the existing widgets plus a waveform visualizer with frequency and amplitude sliders at the top. Adjusting the sliders redraws the sine wave.

- [ ] **Step 5: Commit**

```bash
git add examples/model_dashboard.cpp
git commit -m "feat: add Waveform canvas demo to model dashboard"
```

---

### Task 9: Update design docs and roadmap

**Files:**
- Modify: `doc/design/README.md`
- Modify: `doc/design/components.md` (note that canvas() is now implemented as ViewBuilder method)

- [ ] **Step 1: Update README.md design doc table**

Add canvas row to the implemented section in `doc/design/README.md`:

```markdown
| [canvas escape hatch](../../docs/superpowers/specs/2026-03-29-canvas-escape-hatch-design.md) | `vb.canvas(model)` — custom DrawList rendering in expandable area | **Implemented** |
```

- [ ] **Step 2: Update components.md**

In `doc/design/components.md`, update the "Custom rendering surface (future, requires canvas)" section header to note that canvas is now available as a ViewBuilder method rather than a Component virtual method:

```markdown
### Custom rendering surface (implemented via ViewBuilder::canvas)

The `vb.canvas(model)` method in ViewBuilder provides the canvas escape hatch. Any struct with a `canvas(DrawList&, Rect, const WidgetNode&)` method can draw custom content. Input handling is optional via `handle_canvas_input()`. See the [canvas spec](../../docs/superpowers/specs/2026-03-29-canvas-escape-hatch-design.md) for details.
```

- [ ] **Step 3: Commit**

```bash
git add doc/design/README.md doc/design/components.md
git commit -m "docs: update design docs for canvas escape hatch"
```
