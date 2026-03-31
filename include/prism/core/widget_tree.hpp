#pragma once

#include <prism/core/dropdown_delegates.hpp>
#include <prism/core/layout.hpp>
#include <prism/core/list.hpp>
#include <prism/core/table.hpp>
#include <prism/core/text_delegates.hpp>
#include <prism/core/traits.hpp>
#include <prism/core/widget_node.hpp>
#if __cpp_impl_reflection
#include <prism/core/reflect.hpp>
#endif
#include <prism/core/state.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <memory>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace prism {

// index_ stores raw pointers into the tree -- valid only because the tree
// is fully built before build_index runs and never mutated after construction.
class WidgetTree {
public:
    class ViewBuilder {
        WidgetTree& tree_;
        Node& target_;
        std::vector<Node*> stack_;
        std::set<const void*> placed_;

        Node& current_parent() {
            return stack_.empty() ? target_ : *stack_.back();
        }

    public:
        struct TableBuilder {
            Node& node_ref;
            std::set<const void*>& placed_ref;

            template <typename U>
            TableBuilder& depends_on(Field<U>& field) {
                placed_ref.insert(&field);
                node_ref.dependencies.push_back(
                    [&field](std::function<void()> cb) -> Connection {
                        return field.on_change().connect(
                            [cb = std::move(cb)](const U&) { cb(); });
                    }
                );
                return *this;
            }

            TableBuilder& headers(std::vector<std::string> hdrs) {
                if (node_ref.table_state)
                    node_ref.table_state->header_overrides = std::move(hdrs);
                return *this;
            }
        };

        struct CanvasHandle {
            Node& node_ref;
            std::set<const void*>& placed_ref;

            template <typename U>
            CanvasHandle& depends_on(Field<U>& field) {
                placed_ref.insert(&field);
                node_ref.dependencies.push_back(
                    [&field](std::function<void()> cb) -> Connection {
                        return field.on_change().connect(
                            [cb = std::move(cb)](const U&) { cb(); });
                    }
                );
                return *this;
            }
        };

    public:
        ViewBuilder(WidgetTree& tree, Node& target)
            : tree_(tree), target_(target) {}

        template <typename T>
        void widget(Field<T>& field) {
            placed_.insert(&field);
            current_parent().children.push_back(node_leaf(field, tree_.next_id_));
        }

        [[nodiscard]] const std::set<const void*>& placed() const { return placed_; }

        template <typename C>
        void component(C& comp) {
            current_parent().children.push_back(tree_.build_node_tree(comp));
        }

        void vstack(auto&... args) { (item(args), ...); }

        void hstack(std::invocable auto&& fn) { push_container(LayoutKind::Row, fn); }
        void hstack(auto&... args) { push_container(LayoutKind::Row, [&] { (item(args), ...); }); }
        void vstack(std::invocable auto&& fn) { push_container(LayoutKind::Column, fn); }

    private:
        void item(field_type auto& field) { widget(field); }
        void item(component_type auto& comp) { component(comp); }
        Node& push_container(LayoutKind kind, std::invocable auto&& fn) {
            Node container;
            container.id = tree_.next_id_++;
            container.is_leaf = false;
            container.layout_kind = kind;
            auto& parent = current_parent();
            parent.children.push_back(std::move(container));
            auto& ref = parent.children.back();
            stack_.push_back(&ref);
            fn();
            stack_.pop_back();
            return ref;
        }

    public:

        void spacer() {
            Node s;
            s.id = tree_.next_id_++;
            s.is_leaf = true;
            s.layout_kind = LayoutKind::Spacer;
            current_parent().children.push_back(std::move(s));
        }

        void scroll(std::invocable auto&& fn) {
            auto& node = push_container(LayoutKind::Scroll, fn);
            node.scroll_bar_policy = ScrollBarPolicy::Auto;
            node.scroll_event_policy = ScrollEventPolicy::BubbleAtBounds;
        }

        void scroll(ScrollBarPolicy policy, std::invocable auto&& fn) {
            auto& node = push_container(LayoutKind::Scroll, fn);
            node.scroll_bar_policy = policy;
            node.scroll_event_policy = ScrollEventPolicy::BubbleAtBounds;
        }

        void scroll(Field<ScrollArea>& field, std::invocable auto&& fn) {
            placed_.insert(&field);
            auto& scroll_node = push_container(LayoutKind::Scroll, fn);
            scroll_node.scroll_bar_policy = field.get().scrollbar;
            scroll_node.scroll_event_policy = field.get().event_policy;
            scroll_node.build_widget = [&field](WidgetNode& wn) {
                if (!std::holds_alternative<ScrollState>(wn.edit_state))
                    wn.edit_state = ScrollState{};
                auto& ss = std::get<ScrollState>(wn.edit_state);
                ss.scrollbar = field.get().scrollbar;
                ss.event_policy = field.get().event_policy;
                ss.offset_y = field.get().scroll_y;
            };
            scroll_node.on_change = [&field](std::function<void()> cb) -> Connection {
                return field.on_change().connect(
                    [cb = std::move(cb)](const ScrollArea&) { cb(); });
            };
        }

        template <typename T>
            requires requires(T& t, DrawList& dl, Rect r, const WidgetNode& n) {
                t.canvas(dl, r, n);
            }
        auto canvas(T& model) {
            current_parent().children.push_back(node_canvas(model, tree_.next_id_));
            return CanvasHandle{current_parent().children.back(), placed_};
        }

        template <typename T>
        void list(List<T>& items) {
            Node container;
            container.id = tree_.next_id_++;
            container.is_leaf = false;
            container.layout_kind = LayoutKind::VirtualList;
            container.vlist_item_count = items.size();

            container.vlist_bind_row = [&items](WidgetNode& wn, size_t index) {
                auto field_ptr = std::make_shared<Field<T>>(items[index]);
                wn.edit_state = std::shared_ptr<void>(field_ptr);
                wn.focus_policy = Delegate<T>::focus_policy;
                wn.dirty = true;
                wn.is_container = false;
                wn.draws.clear();
                wn.overlay_draws.clear();
                wn.record = [field_ptr](WidgetNode& node) {
                    node.draws.clear();
                    node.overlay_draws.clear();
                    Delegate<T>::record(node.draws, *field_ptr, node);
                };
                wn.record(wn);
                wn.wire = [field_ptr](WidgetNode& node) {
                    node.connections.push_back(
                        node.on_input.connect([field_ptr, &node](const InputEvent& ev) {
                            Delegate<T>::handle_input(*field_ptr, ev, node);
                        })
                    );
                };
            };

            container.vlist_unbind_row = [](WidgetNode& wn) {
                wn.connections.clear();
                wn.draws.clear();
                wn.overlay_draws.clear();
                wn.edit_state = std::monostate{};
                wn.wire = nullptr;
                wn.record = nullptr;
                wn.dirty = false;
            };

            container.vlist_on_insert = [&items](size_t, std::function<void()> cb) -> Connection {
                return items.on_insert().connect(
                    [cb = std::move(cb)](size_t, const auto&) { cb(); });
            };
            container.vlist_on_remove = [&items](size_t, std::function<void()> cb) -> Connection {
                return items.on_remove().connect(
                    [cb = std::move(cb)](size_t) { cb(); });
            };
            container.vlist_on_update = [&items](size_t, std::function<void()> cb) -> Connection {
                return items.on_update().connect(
                    [cb = std::move(cb)](size_t, const auto&) { cb(); });
            };

            current_parent().children.push_back(std::move(container));
        }

        template <ColumnStorage T>
        TableBuilder table(T& data) {
            Node container;
            container.id = tree_.next_id_++;
            container.is_leaf = false;
            container.layout_kind = LayoutKind::Table;

            auto state = std::make_shared<TableState>();
            state->source = wrap_column_storage(data);
            state->column_count = data.column_count();
            container.table_state = state;

            current_parent().children.push_back(std::move(container));
            return TableBuilder{current_parent().children.back(), placed_};
        }

#if __cpp_impl_reflection
        template <typename T>
            requires RowStorage<List<T>>
        TableBuilder table(List<T>& list) {
            Node container;
            container.id = tree_.next_id_++;
            container.is_leaf = false;
            container.layout_kind = LayoutKind::Table;

            auto state = std::make_shared<TableState>();
            state->source = wrap_row_storage(list);
            state->column_count = state->source.column_count();
            container.table_state = state;

            container.vlist_on_insert = [&list](size_t, std::function<void()> cb) -> Connection {
                return list.on_insert().connect(
                    [cb = std::move(cb)](size_t, const auto&) { cb(); });
            };
            container.vlist_on_remove = [&list](size_t, std::function<void()> cb) -> Connection {
                return list.on_remove().connect(
                    [cb = std::move(cb)](size_t) { cb(); });
            };
            container.vlist_on_update = [&list](size_t, std::function<void()> cb) -> Connection {
                return list.on_update().connect(
                    [cb = std::move(cb)](size_t, const auto&) { cb(); });
            };

            current_parent().children.push_back(std::move(container));
            return TableBuilder{current_parent().children.back(), placed_};
        }
#endif // __cpp_impl_reflection

        void finalize() {
            if (target_.children.size() > 1) {
                Node wrapper;
                wrapper.id = tree_.next_id_++;
                wrapper.is_leaf = false;
                wrapper.layout_kind = LayoutKind::Column;
                wrapper.children = std::move(target_.children);
                target_.children.clear();
                target_.children.push_back(std::move(wrapper));
            }
            // Hoist single Row/Column child: avoids an unnecessary nesting level
            // when view() produces exactly one container (the common case).
            if (target_.children.size() == 1) {
                auto lk = target_.children[0].layout_kind;
                if (lk == LayoutKind::Row || lk == LayoutKind::Column) {
                    target_.layout_kind = lk;
                    target_.children = std::move(target_.children[0].children);
                }
            }
        }
    };

    template <typename Model>
    explicit WidgetTree(Model& model) {
        auto node_tree = build_node_tree(model);
        root_ = build_widget_node(node_tree);
        connect_dirty(node_tree, root_);
        build_index(root_);
        clear_dirty();
    }

    WidgetTree(const WidgetTree&) = delete;
    WidgetTree& operator=(const WidgetTree&) = delete;

    [[nodiscard]] WidgetNode& root() { return root_; }
    [[nodiscard]] const WidgetNode& root() const { return root_; }

    [[nodiscard]] size_t leaf_count() const { return count_leaves(root_); }
    [[nodiscard]] bool any_dirty() const { return check_dirty(root_); }

    void clear_dirty() { clear_dirty_impl(root_); }

    void close_overlays() { close_overlays_impl(root_); }

    void scroll_at(WidgetId target, DY delta) {
        WidgetId current = target;
        while (current != 0) {
            auto it = index_.find(current);
            if (it != index_.end()) {
                auto sv = get_scroll_view(*it->second);
                if (sv) {
                    DY max_off{std::max(0.f, sv->content_h.raw() - sv->viewport_h.raw())};
                    DY new_off{std::clamp(sv->offset.raw() + delta.raw(), 0.f, max_off.raw())};

                    if (std::abs(new_off.raw() - sv->offset.raw()) < 0.001f) {
                        auto* ss = std::get_if<ScrollState>(&it->second->edit_state);
                        if (ss && ss->event_policy == ScrollEventPolicy::BubbleAtBounds) {
                            auto pit = parent_map_.find(current);
                            current = (pit != parent_map_.end()) ? pit->second : 0;
                            continue;
                        }
                        return;
                    }

                    sv->offset = new_off;
                    sv->show_ticks = 30;
                    set_dirty(current);
                    return;
                }
            }
            auto pit = parent_map_.find(current);
            current = (pit != parent_map_.end()) ? pit->second : 0;
        }
    }

    void scroll_to(WidgetId id, DY offset) {
        auto it = index_.find(id);
        if (it == index_.end()) return;
        auto sv = get_scroll_view(*it->second);
        if (!sv) return;
        DY max_off{std::max(0.f, sv->content_h.raw() - sv->viewport_h.raw())};
        sv->offset = DY{std::clamp(offset.raw(), 0.f, max_off.raw())};
        sv->show_ticks = 30;
        set_dirty(id);
    }

    [[nodiscard]] std::vector<WidgetId> leaf_ids() const {
        std::vector<WidgetId> ids;
        collect_leaf_ids(root_, ids);
        return ids;
    }

    void dispatch(WidgetId id, const InputEvent& ev) {
        if (auto it = index_.find(id); it != index_.end())
            it->second->on_input.emit(ev);
    }

    void update_hover(std::optional<WidgetId> id) {
        WidgetId new_id = id.value_or(0);
        if (new_id == hovered_id_) return;
        if (auto it = index_.find(hovered_id_); it != index_.end()) {
            it->second->visual_state.hovered = false;
            it->second->dirty = true;
        }
        hovered_id_ = new_id;
        if (auto it = index_.find(hovered_id_); it != index_.end()) {
            it->second->visual_state.hovered = true;
            it->second->dirty = true;
        }
    }

    void set_pressed(WidgetId id, bool pressed) {
        if (auto it = index_.find(id); it != index_.end()) {
            it->second->visual_state.pressed = pressed;
            it->second->dirty = true;
        }
        captured_id_ = pressed ? id : 0;
    }

    [[nodiscard]] WidgetId captured_id() const { return captured_id_; }

    struct ScrollbarDrag {
        WidgetId scroll_id = 0;
        Y anchor_y{0};       // mouse Y at drag start (absolute)
        DY anchor_offset{0}; // scroll offset at drag start
        Height viewport_h{0};
        Height content_h{0};
        Height thumb_h{0};
    };

    void begin_scrollbar_drag(WidgetId id, Y mouse_y) {
        auto it = index_.find(id);
        if (it == index_.end()) return;
        auto sv = get_scroll_view(*it->second);
        if (!sv || sv->content_h.raw() <= sv->viewport_h.raw()) return;
        scrollbar_drag_ = ScrollbarDrag{
            .scroll_id = id,
            .anchor_y = mouse_y,
            .anchor_offset = sv->offset,
            .viewport_h = sv->viewport_h,
            .content_h = sv->content_h,
            .thumb_h = scrollbar::thumb_height(sv->viewport_h, sv->content_h),
        };
        captured_id_ = id;
    }

    void update_scrollbar_drag(Y mouse_y) {
        if (scrollbar_drag_.scroll_id == 0) return;
        auto& d = scrollbar_drag_;
        float track_range = d.viewport_h.raw() - d.thumb_h.raw();
        if (track_range <= 0) return;
        float max_scroll = d.content_h.raw() - d.viewport_h.raw();
        float dy_pixels = mouse_y.raw() - d.anchor_y.raw();
        float new_offset = std::clamp(
            d.anchor_offset.raw() + dy_pixels * max_scroll / track_range, 0.f, max_scroll);
        scroll_to(d.scroll_id, DY{new_offset});
    }

    void end_scrollbar_drag() {
        scrollbar_drag_ = {};
    }

    [[nodiscard]] bool in_scrollbar_drag() const { return scrollbar_drag_.scroll_id != 0; }

    [[nodiscard]] Connection connect_input(WidgetId id, std::function<void(const InputEvent&)> cb) {
        if (auto it = index_.find(id); it != index_.end())
            return it->second->on_input.connect(std::move(cb));
        return {};
    }

    [[nodiscard]] WidgetId focused_id() const { return focused_id_; }

    [[nodiscard]] const std::vector<WidgetId>& focus_order() const { return focus_order_; }

    void set_focused(WidgetId id) {
        if (id == focused_id_) return;
        if (std::find(focus_order_.begin(), focus_order_.end(), id) == focus_order_.end()) return;
        if (auto it = index_.find(focused_id_); it != index_.end()) {
            it->second->visual_state.focused = false;
            it->second->dirty = true;
        }
        focused_id_ = id;
        if (auto it = index_.find(focused_id_); it != index_.end()) {
            it->second->visual_state.focused = true;
            it->second->dirty = true;
        }
    }

    void clear_focus() {
        if (focused_id_ == 0) return;
        if (auto it = index_.find(focused_id_); it != index_.end()) {
            it->second->visual_state.focused = false;
            it->second->dirty = true;
        }
        focused_id_ = 0;
    }

    void focus_next() {
        if (focus_order_.empty()) return;
        if (focused_id_ == 0) {
            set_focused(focus_order_.front());
            return;
        }
        auto it = std::find(focus_order_.begin(), focus_order_.end(), focused_id_);
        if (it == focus_order_.end() || ++it == focus_order_.end())
            set_focused(focus_order_.front());
        else
            set_focused(*it);
    }

    void focus_prev() {
        if (focus_order_.empty()) return;
        if (focused_id_ == 0) {
            set_focused(focus_order_.back());
            return;
        }
        auto it = std::find(focus_order_.begin(), focus_order_.end(), focused_id_);
        if (it == focus_order_.begin())
            set_focused(focus_order_.back());
        else
            set_focused(*std::prev(it));
    }

    void mark_dirty_by_id(WidgetId id) { set_dirty(id); }

    [[nodiscard]] std::unique_ptr<SceneSnapshot> build_snapshot(float w, float h, uint64_t version) {
        refresh_dirty(root_);
        materialize_all_virtual_lists(root_);

        LayoutNode layout;
        // root_ is always a container; Spacer is only valid on non-container nodes
        assert(root_.layout_kind != LayoutKind::Spacer);
        layout.kind = (root_.layout_kind == LayoutKind::Row)
            ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
        layout.id = root_.id;
        for (auto& c : root_.children)
            build_layout(c, layout);

        layout_measure(layout, LayoutAxis::Vertical);
        layout_arrange(layout, {Point{X{0}, Y{0}}, Size{Width{w}, Height{h}}});

        // Post-layout: sync scroll state (viewport/content sizes, offset clamping)
        update_scroll_state(layout);

        // Post-layout: update canvas nodes with their resolved bounds and re-record
        update_canvas_bounds(layout);

        auto snap = std::make_unique<SceneSnapshot>();
        snap->version = version;
        layout_flatten(layout, *snap);
        return snap;
    }

private:
    WidgetNode root_;
    WidgetId next_id_ = 1;
    WidgetId hovered_id_ = 0;
    WidgetId focused_id_ = 0;
    WidgetId captured_id_ = 0;
    ScrollbarDrag scrollbar_drag_;
    std::vector<WidgetId> focus_order_;
    std::unordered_map<WidgetId, WidgetNode*> index_;
    std::unordered_map<WidgetId, WidgetId> parent_map_;

    static ScrollState& ensure_scroll_state(WidgetNode& node) {
        if (!std::holds_alternative<ScrollState>(node.edit_state))
            node.edit_state = ScrollState{};
        return std::get<ScrollState>(node.edit_state);
    }

    static TableState* get_table_state(WidgetNode& node) {
        auto* sp = std::get_if<std::shared_ptr<TableState>>(&node.edit_state);
        return sp ? sp->get() : nullptr;
    }

    static VirtualListState* get_vlist_state(WidgetNode& node) {
        auto* sp = std::get_if<std::shared_ptr<VirtualListState>>(&node.edit_state);
        return sp ? sp->get() : nullptr;
    }

    struct ScrollView {
        DY& offset;
        Height viewport_h;
        Height content_h;
        uint8_t& show_ticks;
    };

    static std::optional<ScrollView> get_scroll_view(WidgetNode& node) {
        if (auto* ss = std::get_if<ScrollState>(&node.edit_state))
            return ScrollView{ss->offset_y, ss->viewport_h, ss->content_h, ss->show_ticks};
        if (auto* vls = get_vlist_state(node)) {
            Height ch{static_cast<float>(vls->item_count.raw()) * vls->item_height.raw()};
            return ScrollView{vls->scroll_offset, vls->viewport_h, ch, vls->show_ticks};
        }
        return std::nullopt;
    }

    // --- Node → WidgetNode conversion ---

    static WidgetNode build_widget_node(Node& node) {
        WidgetNode wn;
        wn.id = node.id;
        wn.layout_kind = node.layout_kind;
        if (node.is_leaf) {
            wn.is_container = false;
            if (node.build_widget)
                node.build_widget(wn);
        } else {
            wn.is_container = true;
            if (node.layout_kind == LayoutKind::Scroll) {
                ScrollState ss;
                ss.scrollbar = node.scroll_bar_policy;
                ss.event_policy = node.scroll_event_policy;
                wn.edit_state = ss;
                if (node.build_widget)
                    node.build_widget(wn);  // Field<ScrollArea> overrides
            } else if (node.layout_kind == LayoutKind::VirtualList) {
                auto vls = std::make_shared<VirtualListState>();
                vls->item_count = ItemCount{node.vlist_item_count};
                if (node.vlist_bind_row) vls->bind_row = node.vlist_bind_row;
                if (node.vlist_unbind_row) vls->unbind_row = node.vlist_unbind_row;
                wn.edit_state = vls;
            } else if (node.layout_kind == LayoutKind::Table && node.table_state) {
                wn.edit_state = node.table_state;
                wn.focus_policy = FocusPolicy::tab_and_click;
            }
            if (node.layout_kind != LayoutKind::VirtualList && node.layout_kind != LayoutKind::Table) {
                for (auto& child : node.children)
                    wn.children.push_back(build_widget_node(child));
            }
        }
        return wn;
    }

    void connect_dirty(Node& node, WidgetNode& wn) {
        if (node.is_leaf) {
            auto id = wn.id;
            if (node.on_change) {
                wn.connections.push_back(
                    node.on_change([this, id]() { set_dirty(id); })
                );
            }
            for (auto& dep : node.dependencies) {
                wn.connections.push_back(
                    dep([this, id]() { set_dirty(id); })
                );
            }
        } else {
            // Scroll containers with Field<ScrollArea> have their own on_change
            if (node.on_change) {
                auto id = wn.id;
                wn.connections.push_back(
                    node.on_change([this, id]() { set_dirty(id); })
                );
            }

            // Table: connect List<T> signals (RowStorage) and depends_on (ColumnStorage)
            if (node.layout_kind == LayoutKind::Table) {
                auto id = wn.id;
                if (node.vlist_on_insert) {
                    wn.connections.push_back(
                        node.vlist_on_insert(0, [this, id]() { set_dirty(id); })
                    );
                }
                if (node.vlist_on_remove) {
                    wn.connections.push_back(
                        node.vlist_on_remove(0, [this, id]() { set_dirty(id); })
                    );
                }
                if (node.vlist_on_update) {
                    wn.connections.push_back(
                        node.vlist_on_update(0, [this, id]() { set_dirty(id); })
                    );
                }
                for (auto& dep : node.dependencies) {
                    wn.connections.push_back(
                        dep([this, id]() { set_dirty(id); })
                    );
                }
                return;
            }

            // Virtual list: connect List<T> signals
            if (node.layout_kind == LayoutKind::VirtualList) {
                auto id = wn.id;
                if (node.vlist_on_insert) {
                    wn.connections.push_back(
                        node.vlist_on_insert(0, [this, id]() {
                            auto it = index_.find(id);
                            if (it != index_.end()) {
                                if (auto* vls = get_vlist_state(*it->second))
                                    vls->item_count = ItemCount{vls->item_count.raw() + 1};
                            }
                            set_dirty(id);
                        })
                    );
                }
                if (node.vlist_on_remove) {
                    wn.connections.push_back(
                        node.vlist_on_remove(0, [this, id]() {
                            auto it = index_.find(id);
                            if (it != index_.end()) {
                                if (auto* vls = get_vlist_state(*it->second)) {
                                    if (vls->item_count.raw() > 0)
                                        vls->item_count = ItemCount{vls->item_count.raw() - 1};
                                }
                            }
                            set_dirty(id);
                        })
                    );
                }
                if (node.vlist_on_update) {
                    wn.connections.push_back(
                        node.vlist_on_update(0, [this, id]() {
                            set_dirty(id);
                        })
                    );
                }
                return; // no child nodes to recurse into
            }

            assert(node.children.size() == wn.children.size());
            for (size_t i = 0; i < node.children.size(); ++i)
                connect_dirty(node.children[i], wn.children[i]);
        }
    }

    // --- Node tree construction ---

    template <typename Model>
    Node build_node_tree(Model& model) {
        Node root;
        root.id = next_id_++;
        root.is_leaf = false;

        if constexpr (requires(Model& m, ViewBuilder& vb) { m.view(vb); }) {
            ViewBuilder vb{*this, root};
            model.view(vb);
#if __cpp_impl_reflection
            check_unplaced_fields(model, vb.placed());
#endif
            vb.finalize();
        }
#if __cpp_impl_reflection
        else {
            static constexpr auto members = std::define_static_array(
                std::meta::nonstatic_data_members_of(
                    ^^Model, std::meta::access_context::unchecked()));

            template for (constexpr auto m : members) {
                auto& member = model.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;

                if constexpr (is_state_v<M>) {
                    // invisible observable — no widget
                } else if constexpr (is_field_v<M>) {
                    root.children.push_back(node_leaf(member, next_id_));
                } else if constexpr (is_component_v<M>) {
                    root.children.push_back(build_node_tree(member));
                }
            }
        }
#endif // __cpp_impl_reflection

        return root;
    }

#if __cpp_impl_reflection
    template <typename Model>
    void check_unplaced_fields([[maybe_unused]] Model& model,
                               [[maybe_unused]] const std::set<const void*>& placed) {
#ifndef NDEBUG
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(
                ^^Model, std::meta::access_context::unchecked()));
        template for (constexpr auto m : members) {
            auto& member = model.[:m:];
            using M = std::remove_cvref_t<decltype(member)>;
            if constexpr (is_field_v<M>) {
                if (!placed.contains(&member)) {
                    std::fprintf(stderr, "[prism] warning: Field '%.*s' in %.*s not placed by view()\n",
                        static_cast<int>(std::meta::identifier_of(m).size()),
                        std::meta::identifier_of(m).data(),
                        static_cast<int>(std::meta::identifier_of(^^Model).size()),
                        std::meta::identifier_of(^^Model).data());
                }
            }
        }
#endif
    }
#endif // __cpp_impl_reflection

    // --- WidgetNode tree utilities ---

    void build_index(WidgetNode& node) {
        index_[node.id] = &node;
        if (node.wire) {
            node.wire(node);
            node.wire = nullptr;
        }
        if (!node.is_container && node.focus_policy != FocusPolicy::none)
            focus_order_.push_back(node.id);
        for (auto& c : node.children) {
            parent_map_[c.id] = node.id;
            build_index(c);
        }
    }

    static size_t count_leaves(const WidgetNode& node) {
        if (!node.is_container)
            return node.layout_kind == LayoutKind::Spacer ? 0 : 1;
        size_t n = 0;
        for (auto& c : node.children) n += count_leaves(c);
        return n;
    }

    static bool check_dirty(const WidgetNode& node) {
        if (node.dirty) return true;
        for (auto& c : node.children)
            if (check_dirty(c)) return true;
        return false;
    }

    static void clear_dirty_impl(WidgetNode& node) {
        node.dirty = false;
        for (auto& c : node.children) clear_dirty_impl(c);
    }

    static void close_overlays_impl(WidgetNode& node) {
        if (!node.overlay_draws.empty()) {
            node.overlay_draws.clear();
            node.edit_state = std::monostate{};
            node.dirty = true;
        }
        for (auto& c : node.children) close_overlays_impl(c);
    }

    static void collect_leaf_ids(const WidgetNode& node, std::vector<WidgetId>& ids) {
        if (!node.is_container) {
            if (node.layout_kind != LayoutKind::Spacer)
                ids.push_back(node.id);
            return;
        }
        for (auto& c : node.children) collect_leaf_ids(c, ids);
    }

    static void refresh_dirty(WidgetNode& node) {
        if (node.dirty && node.record)
            node.record(node);
        for (auto& c : node.children)
            refresh_dirty(c);
    }

    void set_dirty(WidgetId id) {
        if (auto it = index_.find(id); it != index_.end())
            it->second->dirty = true;
    }

    void update_scroll_state(LayoutNode& layout_node) {
        if (layout_node.kind == LayoutNode::Kind::Scroll) {
            auto it = index_.find(layout_node.id);
            if (it != index_.end()) {
                auto& ss = ensure_scroll_state(*it->second);
                ss.viewport_h = layout_node.allocated.extent.h;
                ss.viewport_w = layout_node.allocated.extent.w;
                ss.content_h = layout_node.scroll_content_h;
            }
        }
        if (layout_node.kind == LayoutNode::Kind::VirtualList) {
            auto it = index_.find(layout_node.id);
            if (it != index_.end()) {
                if (auto* vls = get_vlist_state(*it->second))
                    vls->viewport_h = layout_node.allocated.extent.h;
            }
        }
        if (layout_node.kind == LayoutNode::Kind::Table) {
            auto it = index_.find(layout_node.id);
            if (it != index_.end()) {
                if (auto* ts = get_table_state(*it->second)) {
                    ts->viewport_h = Height{layout_node.allocated.extent.h.raw() - ts->row_height.raw()};
                    ts->viewport_w = layout_node.allocated.extent.w;
                }
            }
        }
        auto it = index_.find(layout_node.id);
        if (it != index_.end()) {
            if (auto sv = get_scroll_view(*it->second)) {
                DY max_off{std::max(0.f, sv->content_h.raw() - sv->viewport_h.raw())};
                sv->offset = DY{std::clamp(sv->offset.raw(), 0.f, max_off.raw())};
                layout_node.scroll_offset = sv->offset;
                if (sv->show_ticks > 0) sv->show_ticks--;
            }
        }
        for (auto& child : layout_node.children)
            update_scroll_state(child);
    }

    void update_canvas_bounds(LayoutNode& layout_node) {
        if (layout_node.kind == LayoutNode::Kind::Canvas) {
            auto it = index_.find(layout_node.id);
            auto* wn = (it != index_.end()) ? it->second : nullptr;
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
            update_canvas_bounds(child);
    }

    void materialize_virtual_list(WidgetNode& node) {
        auto* vls = get_vlist_state(node);
        if (!vls || !vls->bind_row) return;

        // Measure item height from first item if not yet known
        if (vls->item_height.raw() <= 0.f && vls->item_count.raw() > 0) {
            WidgetNode probe;
            probe.id = next_id_++;
            vls->bind_row(probe, 0);
            auto bb = probe.draws.bounding_box();
            vls->item_height = bb.extent.h;
            if (vls->unbind_row) vls->unbind_row(probe);
        }

        auto [new_start, new_end] = compute_visible_range(
            vls->item_count, vls->item_height, vls->scroll_offset,
            vls->viewport_h, vls->overscan);

        // Unbind all current children -> pool
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
            index_.erase(it->id);
            parent_map_.erase(it->id);
            std::erase(focus_order_, it->id);
            if (vls->unbind_row) vls->unbind_row(*it);
            vls->pool.push_back(std::move(*it));
        }
        node.children.clear();

        // Bind children for visible range (wire after push to avoid dangling SenderHub pointers)
        size_t range_size = new_end.raw() - new_start.raw();
        node.children.reserve(range_size);
        for (size_t i = new_start.raw(); i < new_end.raw(); ++i) {
            WidgetNode wn;
            if (!vls->pool.empty()) {
                wn = std::move(vls->pool.back());
                vls->pool.pop_back();
            } else {
                wn.id = next_id_++;
            }
            vls->bind_row(wn, i);
            parent_map_[wn.id] = node.id;
            if (wn.focus_policy != FocusPolicy::none)
                focus_order_.push_back(wn.id);
            node.children.push_back(std::move(wn));
        }

        // Wire and fix index pointers after all children are in final positions
        for (auto& c : node.children) {
            if (c.wire) {
                c.wire(c);
                c.wire = nullptr;
            }
            index_[c.id] = &c;
        }

        vls->visible_start = new_start;
        vls->visible_end = new_end;
    }

    void materialize_table(WidgetNode& node) {
        auto* ts = get_table_state(node);
        if (!ts || !ts->source.row_count) return;

        if (ts->row_height.raw() <= 0.f)
            ts->row_height = Height{24.f};

        size_t total_rows = ts->row_count();
        auto [new_start, new_end] = compute_visible_range(
            ItemCount{total_rows}, ts->row_height, ts->scroll_y,
            ts->viewport_h, ts->overscan);

        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
            index_.erase(it->id);
            parent_map_.erase(it->id);
            std::erase(focus_order_, it->id);
            ts->pool.push_back(std::move(*it));
        }
        node.children.clear();

        float col_w = ts->column_count > 0
            ? std::max(120.f, ts->viewport_w.raw() / static_cast<float>(ts->column_count))
            : 120.f;

        size_t range_size = new_end.raw() - new_start.raw();
        node.children.reserve(range_size);
        for (size_t i = new_start.raw(); i < new_end.raw(); ++i) {
            WidgetNode wn;
            if (!ts->pool.empty()) {
                wn = std::move(ts->pool.back());
                ts->pool.pop_back();
            } else {
                wn.id = next_id_++;
            }
            wn.dirty = true;
            wn.draws.clear();

            size_t row_idx = i;
            bool selected = ts->selected_row.get().has_value() &&
                            ts->selected_row.get().value() == row_idx;

            auto bg = (row_idx % 2 == 0)
                ? Color::rgba(30, 30, 50)
                : Color::rgba(26, 26, 46);
            if (selected)
                bg = Color::rgba(50, 50, 120);

            float total_w = col_w * static_cast<float>(ts->column_count);
            wn.draws.filled_rect(
                Rect{Point{X{0}, Y{0}},
                     Size{Width{total_w}, ts->row_height}},
                bg);

            for (size_t c = 0; c < ts->column_count; ++c) {
                std::string txt = ts->source.cell_text(row_idx, c);
                float cx = static_cast<float>(c) * col_w;
                wn.draws.clip_push(
                    Point{X{cx}, Y{0}},
                    Size{Width{col_w}, ts->row_height});
                wn.draws.text(std::move(txt),
                    Point{X{cx + 4.f}, Y{4.f}},
                    14.f, Color::rgba(200, 200, 220));
                wn.draws.clip_pop();

                if (c > 0) {
                    wn.draws.filled_rect(
                        Rect{Point{X{cx}, Y{0}},
                             Size{Width{1.f}, ts->row_height}},
                        Color::rgba(50, 50, 70));
                }
            }

            parent_map_[wn.id] = node.id;
            node.children.push_back(std::move(wn));
        }

        for (auto& c : node.children)
            index_[c.id] = &c;

        ts->visible_start = new_start;
        ts->visible_end = new_end;

        // Render header text into overlay_draws (picked up by table flatten)
        node.overlay_draws.clear();
        float col_w_for_header = ts->column_count > 0
            ? std::max(120.f, ts->viewport_w.raw() / static_cast<float>(ts->column_count))
            : 120.f;
        for (size_t c = 0; c < ts->column_count; ++c) {
            float cx = static_cast<float>(c) * col_w_for_header;
            auto hdr = ts->column_header(c);
            node.overlay_draws.text(std::string(hdr),
                Point{X{cx + 4.f}, Y{4.f}},
                14.f, Color::rgba(136, 136, 204));
        }

        // Wire input handler for selection (only once)
        if (!node.wire) {
        node.wire = [](WidgetNode&) {}; // mark as wired
        node.connections.push_back(
            node.on_input.connect([ts_ptr = &(*ts), &node](const InputEvent& ev) {
                auto& ts = *ts_ptr;
                if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == 1) {
                    float header_h = ts.row_height.raw();
                    float local_y = mb->position.y.raw();
                    if (local_y < header_h) return;
                    float body_y = local_y - header_h + ts.scroll_y.raw();
                    size_t row = static_cast<size_t>(body_y / ts.row_height.raw());
                    if (row < ts.row_count()) {
                        auto current = ts.selected_row.get();
                        if (current.has_value() && current.value() == row)
                            ts.selected_row.set(std::nullopt);
                        else
                            ts.selected_row.set(row);
                        node.dirty = true;
                    }
                }
                if (auto* ms = std::get_if<MouseScroll>(&ev)) {
                    float max_scroll = std::max(0.f,
                        ts.row_height.raw() * static_cast<float>(ts.row_count()) - ts.viewport_h.raw());
                    if (ms->dy.raw() != 0.f) {
                        float scroll_amount = ms->dy.raw() * ts.row_height.raw() * 3.f;
                        ts.scroll_y = DY{std::clamp(ts.scroll_y.raw() + scroll_amount, 0.f, max_scroll)};
                        node.dirty = true;
                    }
                }
                if (auto* kp = std::get_if<KeyPress>(&ev)) {
                    auto current = ts.selected_row.get();
                    if (kp->key == keys::down) {
                        size_t next = current.has_value() ? current.value() + 1 : 0;
                        if (next < ts.row_count()) {
                            ts.selected_row.set(next);
                            node.dirty = true;
                        }
                    } else if (kp->key == keys::up) {
                        if (current.has_value() && current.value() > 0) {
                            ts.selected_row.set(current.value() - 1);
                            node.dirty = true;
                        }
                    } else if (kp->key == keys::page_down) {
                        float max_scroll = std::max(0.f,
                            ts.row_height.raw() * static_cast<float>(ts.row_count()) - ts.viewport_h.raw());
                        ts.scroll_y = DY{std::clamp(ts.scroll_y.raw() + ts.viewport_h.raw(), 0.f, max_scroll)};
                        node.dirty = true;
                    } else if (kp->key == keys::page_up) {
                        float max_scroll = std::max(0.f,
                            ts.row_height.raw() * static_cast<float>(ts.row_count()) - ts.viewport_h.raw());
                        ts.scroll_y = DY{std::clamp(ts.scroll_y.raw() - ts.viewport_h.raw(), 0.f, max_scroll)};
                        node.dirty = true;
                    }
                    // Scroll selected row into view
                    if (auto sel = ts.selected_row.get(); sel.has_value()) {
                        float row_top = static_cast<float>(sel.value()) * ts.row_height.raw();
                        float row_bottom = row_top + ts.row_height.raw();
                        float vp_top = ts.scroll_y.raw();
                        float vp_bottom = vp_top + ts.viewport_h.raw();
                        float max_scroll = std::max(0.f,
                            ts.row_height.raw() * static_cast<float>(ts.row_count()) - ts.viewport_h.raw());
                        if (row_bottom > vp_bottom)
                            ts.scroll_y = DY{std::clamp(row_bottom - ts.viewport_h.raw(), 0.f, max_scroll)};
                        else if (row_top < vp_top)
                            ts.scroll_y = DY{std::clamp(row_top, 0.f, max_scroll)};
                    }
                }
            })
        );
        } // if (!node.wire)
    }

    void materialize_all_virtual_lists(WidgetNode& node) {
        if (node.layout_kind == LayoutKind::VirtualList)
            materialize_virtual_list(node);
        else if (node.layout_kind == LayoutKind::Table)
            materialize_table(node);
        for (auto& c : node.children)
            materialize_all_virtual_lists(c);
    }

    static void build_layout(WidgetNode& node, LayoutNode& parent) {
        using LK = LayoutKind;

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
        } else if (node.layout_kind == LK::Scroll) {
            LayoutNode container;
            container.kind = LayoutNode::Kind::Scroll;
            container.id = node.id;
            if (auto* ss = std::get_if<ScrollState>(&node.edit_state))
                container.scroll_offset = ss->offset_y;
            for (auto& c : node.children)
                build_layout(c, container);
            parent.children.push_back(std::move(container));
        } else if (node.layout_kind == LK::VirtualList) {
            LayoutNode container;
            container.kind = LayoutNode::Kind::VirtualList;
            container.id = node.id;
            if (auto* vls = get_vlist_state(node)) {
                container.scroll_offset = vls->scroll_offset;
                container.scroll_content_h = Height{
                    static_cast<float>(vls->item_count.raw()) * vls->item_height.raw()};
                container.vlist_visible_start = vls->visible_start.raw();
                container.vlist_item_height = vls->item_height.raw();
            }
            for (auto& c : node.children)
                build_layout(c, container);
            parent.children.push_back(std::move(container));
        } else if (node.layout_kind == LK::Table) {
            LayoutNode container;
            container.kind = LayoutNode::Kind::Table;
            container.id = node.id;
            if (auto* ts = get_table_state(node)) {
                container.scroll_offset = ts->scroll_y;
                container.scroll_content_h = Height{
                    static_cast<float>(ts->row_count()) * ts->row_height.raw()};
                container.vlist_visible_start = ts->visible_start.raw();
                container.vlist_item_height = ts->row_height.raw();
                container.table_column_count = ts->column_count;
                container.table_header_h = ts->row_height.raw();
                container.table_scroll_x = ts->scroll_x;
            }
            container.overlay_draws = node.overlay_draws;
            for (auto& c : node.children)
                build_layout(c, container);
            parent.children.push_back(std::move(container));
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
};

} // namespace prism
