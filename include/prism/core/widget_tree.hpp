#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/delegate.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/field.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/layout.hpp>
#include <prism/core/reflect.hpp>
#include <prism/core/scene_snapshot.hpp>
#include <prism/core/state.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace prism {

struct WidgetNode {
    WidgetId id = 0;
    bool dirty = false;
    bool is_container = false;
    WidgetVisualState visual_state;
    DrawList draws;
    std::vector<Connection> connections;
    std::vector<WidgetNode> children;
    SenderHub<const InputEvent&> on_input;
    std::function<void(WidgetNode&)> wire;
    std::function<void(WidgetNode&)> record;
};

// index_ stores raw pointers into the tree — valid only because the tree
// is fully built before build_index runs and never mutated after construction.
class WidgetTree {
public:
    template <typename Model>
    explicit WidgetTree(Model& model) {
        root_ = build_container(model);
        build_index(root_);
        clear_dirty();
    }

    WidgetTree(const WidgetTree&) = delete;
    WidgetTree& operator=(const WidgetTree&) = delete;

    [[nodiscard]] size_t leaf_count() const { return count_leaves(root_); }
    [[nodiscard]] bool any_dirty() const { return check_dirty(root_); }

    void clear_dirty() { clear_dirty_impl(root_); }

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
    }

    [[nodiscard]] Connection connect_input(WidgetId id, std::function<void(const InputEvent&)> cb) {
        if (auto it = index_.find(id); it != index_.end())
            return it->second->on_input.connect(std::move(cb));
        return {};
    }

    [[nodiscard]] std::unique_ptr<SceneSnapshot> build_snapshot(float w, float h, uint64_t version) {
        refresh_dirty(root_);

        LayoutNode layout;
        layout.kind = LayoutNode::Kind::Column;
        layout.id = root_.id;
        build_layout(root_, layout);

        layout_measure(layout, LayoutAxis::Vertical);
        layout_arrange(layout, {0, 0, w, h});

        auto snap = std::make_unique<SceneSnapshot>();
        snap->version = version;
        layout_flatten(layout, *snap);
        return snap;
    }

private:
    WidgetNode root_;
    WidgetId next_id_ = 1;
    WidgetId hovered_id_ = 0;
    std::unordered_map<WidgetId, WidgetNode*> index_;

    void build_index(WidgetNode& node) {
        index_[node.id] = &node;
        if (node.wire) {
            node.wire(node);
            node.wire = nullptr;
        }
        for (auto& c : node.children)
            build_index(c);
    }

    static size_t count_leaves(const WidgetNode& node) {
        if (!node.is_container) return 1;
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

    static void collect_leaf_ids(const WidgetNode& node, std::vector<WidgetId>& ids) {
        if (!node.is_container) {
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

    static bool mark_dirty(WidgetNode& node, WidgetId id) {
        if (node.id == id) { node.dirty = true; return true; }
        for (auto& c : node.children)
            if (mark_dirty(c, id)) return true;
        return false;
    }

    static void build_layout(WidgetNode& node, LayoutNode& layout) {
        if (!node.is_container) {
            LayoutNode leaf;
            leaf.kind = LayoutNode::Kind::Leaf;
            leaf.id = node.id;
            leaf.draws = node.draws;
            layout.children.push_back(std::move(leaf));
        } else {
            for (auto& c : node.children) {
                build_layout(c, layout);
            }
        }
    }

    template <typename T>
    WidgetNode build_leaf(Field<T>& field) {
        WidgetNode node;
        node.id = next_id_++;
        node.is_container = false;

        node.record = [&field](WidgetNode& n) {
            n.draws.clear();
            Delegate<T>::record(n.draws, field, n.visual_state);
        };
        node.record(node);

        node.wire = [&field](WidgetNode& n) {
            n.connections.push_back(
                n.on_input.connect([&field, &n](const InputEvent& ev) {
                    Delegate<T>::handle_input(field, ev, n.visual_state);
                })
            );
        };

        auto id = node.id;
        node.connections.push_back(
            field.on_change().connect([this, id](const T&) {
                mark_dirty(root_, id);
            })
        );

        return node;
    }

    template <typename Model>
    WidgetNode build_container(Model& model) {
        WidgetNode container;
        container.id = next_id_++;
        container.is_container = true;

        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(
                ^^Model, std::meta::access_context::unchecked()));

        template for (constexpr auto m : members) {
            auto& member = model.[:m:];
            using M = std::remove_cvref_t<decltype(member)>;

            if constexpr (is_state_v<M>) {
                // invisible observable — no widget
            } else if constexpr (is_field_v<M>) {
                container.children.push_back(build_leaf(member));
            } else if constexpr (is_component_v<M>) {
                container.children.push_back(build_container(member));
            }
        }

        return container;
    }
};

} // namespace prism
