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

#include <any>
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
    FocusPolicy focus_policy = FocusPolicy::none;
    WidgetVisualState visual_state;
    std::any edit_state;
    DrawList draws;
    DrawList overlay_draws;
    std::vector<Connection> connections;
    std::vector<WidgetNode> children;
    SenderHub<const InputEvent&> on_input;
    std::function<void(WidgetNode&)> wire;
    std::function<void(WidgetNode&)> record;
};

// Defined here (after WidgetNode is complete) for use in delegate.hpp bodies.
inline const WidgetVisualState& node_vs(const WidgetNode& n) { return n.visual_state; }

// --- Delegate<TextField<T>> method bodies ---
// These are defined here because they access WidgetNode members (edit_state, dirty)
// which require the complete type.

template <StringLike T>
const TextEditState& Delegate<TextField<T>>::get_edit_state(const WidgetNode& node) {
    static const TextEditState default_state;
    if (!node.edit_state.has_value()) return default_state;
    return std::any_cast<const TextEditState&>(node.edit_state);
}

template <StringLike T>
TextEditState& Delegate<TextField<T>>::ensure_edit_state(WidgetNode& node) {
    if (!node.edit_state.has_value())
        node.edit_state = TextEditState{};
    return std::any_cast<TextEditState&>(node.edit_state);
}

template <StringLike T>
void Delegate<TextField<T>>::record(DrawList& dl, const Field<TextField<T>>& field,
                                    const WidgetNode& node) {
    auto& vs = node_vs(node);
    auto& tf = field.get();
    auto& es = get_edit_state(node);
    float cw = char_width(font_size);

    auto bg = vs.focused ? Color::rgba(65, 65, 78)
            : vs.hovered ? Color::rgba(55, 55, 68)
            : Color::rgba(45, 45, 55);
    dl.filled_rect({0, 0, widget_w, widget_h}, bg);

    if (vs.focused)
        dl.rect_outline({-1, -1, widget_w + 2, widget_h + 2},
                        Color::rgba(80, 160, 240), 2.0f);

    float text_area_w = widget_w - 2 * padding;
    dl.clip_push({padding, 0, text_area_w, widget_h});

    if (tf.value.empty() && !vs.focused) {
        dl.text(tf.placeholder, {padding, padding + 2}, font_size,
                Color::rgba(120, 120, 130));
    } else {
        float text_x = padding - es.scroll_offset;
        std::string text_str(tf.value.data(), tf.value.size());
        dl.text(text_str, {text_x, padding + 2}, font_size,
                Color::rgba(220, 220, 220));
    }

    if (vs.focused) {
        float cursor_x = padding + es.cursor * cw - es.scroll_offset;
        dl.filled_rect({cursor_x, padding, cursor_w, widget_h - 2 * padding},
                       Color::rgba(220, 220, 240));
    }

    dl.clip_pop();
}

template <StringLike T>
void Delegate<TextField<T>>::handle_input(Field<TextField<T>>& field, const InputEvent& ev,
                                          WidgetNode& node) {
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
        size_t pos = static_cast<size_t>(
            std::clamp(rel_x / cw + 0.5f, 0.f, static_cast<float>(len)));
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

// --- Shared dropdown rendering/input helpers ---

namespace detail {

struct DropdownLabelResolver {
    std::function<std::string(size_t)> label_at;
    size_t count;
};

inline const DropdownEditState& get_dropdown_state(const WidgetNode& node) {
    static const DropdownEditState default_state;
    if (!node.edit_state.has_value()) return default_state;
    return std::any_cast<const DropdownEditState&>(node.edit_state);
}

inline DropdownEditState& ensure_dropdown_state(WidgetNode& node) {
    if (!node.edit_state.has_value())
        node.edit_state = DropdownEditState{};
    return std::any_cast<DropdownEditState&>(node.edit_state);
}

constexpr float dd_widget_w = 200.f;
constexpr float dd_widget_h = 30.f;
constexpr float dd_padding = 8.f;
constexpr float dd_font_size = 14.f;
constexpr float dd_option_h = 28.f;

inline void dropdown_record(DrawList& dl, WidgetNode& node,
                            const std::string& current_label,
                            const DropdownLabelResolver& resolver) {
    auto& vs = node_vs(node);
    auto& es = get_dropdown_state(node);

    auto bg = es.open    ? Color::rgba(65, 65, 78)
            : vs.hovered ? Color::rgba(55, 55, 68)
            : Color::rgba(45, 45, 55);
    dl.filled_rect({0, 0, dd_widget_w, dd_widget_h}, bg);

    dl.text(current_label, {dd_padding, 7}, dd_font_size, Color::rgba(220, 220, 220));

    dl.text("\xe2\x96\xbe", {dd_widget_w - 20, 7}, dd_font_size, Color::rgba(160, 160, 170));

    if (vs.focused)
        dl.rect_outline({-1, -1, dd_widget_w + 2, dd_widget_h + 2},
                        Color::rgba(80, 160, 240), 2.0f);

    node.overlay_draws.clear();
    if (es.open) {
        float popup_h = resolver.count * dd_option_h;
        node.overlay_draws.filled_rect(
            {0, dd_widget_h, dd_widget_w, popup_h},
            Color::rgba(50, 50, 62));
        node.overlay_draws.rect_outline(
            {0, dd_widget_h, dd_widget_w, popup_h},
            Color::rgba(80, 80, 95), 1.0f);

        for (size_t i = 0; i < resolver.count; ++i) {
            float y = dd_widget_h + i * dd_option_h;
            if (i == es.highlighted) {
                node.overlay_draws.filled_rect(
                    {0, y, dd_widget_w, dd_option_h},
                    Color::rgba(60, 100, 180));
            }
            node.overlay_draws.text(
                resolver.label_at(i),
                {dd_padding, y + 6}, dd_font_size,
                i == es.highlighted ? Color::rgba(255, 255, 255)
                                    : Color::rgba(200, 200, 210));
        }
    }
}

inline bool dropdown_handle_input(const InputEvent& ev, WidgetNode& node,
                                  size_t current_index, size_t count,
                                  std::function<void(size_t)> select) {
    auto& es = ensure_dropdown_state(node);
    bool changed = false;

    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        if (es.open) {
            float rel_y = mb->position.y - dd_widget_h;
            if (rel_y >= 0 && rel_y < count * dd_option_h
                && mb->position.x >= 0 && mb->position.x < dd_widget_w) {
                size_t idx = static_cast<size_t>(rel_y / dd_option_h);
                select(idx);
                changed = true;
            }
            es.open = false;
            node.dirty = true;
        } else {
            es.open = true;
            es.highlighted = current_index;
            node.dirty = true;
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (es.open) {
            if (kp->key == keys::down) {
                es.highlighted = (es.highlighted + 1) % count;
                node.dirty = true;
            } else if (kp->key == keys::up) {
                es.highlighted = (es.highlighted + count - 1) % count;
                node.dirty = true;
            } else if (kp->key == keys::enter) {
                select(es.highlighted);
                es.open = false;
                node.dirty = true;
                changed = true;
            } else if (kp->key == keys::escape) {
                es.open = false;
                node.dirty = true;
            }
        } else {
            if (kp->key == keys::space || kp->key == keys::enter) {
                es.open = true;
                es.highlighted = current_index;
                node.dirty = true;
            } else if (kp->key == keys::down) {
                size_t next = (current_index + 1) % count;
                select(next);
                changed = true;
            } else if (kp->key == keys::up) {
                size_t prev = (current_index + count - 1) % count;
                select(prev);
                changed = true;
            }
        }
    }
    return changed;
}

} // namespace detail

// --- Delegate<ScopedEnum T> method bodies ---

template <ScopedEnum T>
    requires (!TextEditable<T>)
void Delegate<T>::record(DrawList& dl, const Field<T>& field, const WidgetNode& node) {
    size_t idx = enum_index(field.get());
    std::string label = enum_label<T>(idx);
    detail::DropdownLabelResolver resolver{
        .label_at = [](size_t i) { return enum_label<T>(i); },
        .count = enum_count<T>()
    };
    detail::dropdown_record(dl, const_cast<WidgetNode&>(node), label, resolver);
}

template <ScopedEnum T>
    requires (!TextEditable<T>)
void Delegate<T>::handle_input(Field<T>& field, const InputEvent& ev, WidgetNode& node) {
    size_t current = enum_index(field.get());
    constexpr size_t count = enum_count<T>();
    detail::dropdown_handle_input(ev, node, current, count,
        [&](size_t idx) { field.set(enum_from_index<T>(idx)); });
}

// --- Delegate<Dropdown<T>> method bodies ---

template <ScopedEnum T>
void Delegate<Dropdown<T>>::record(DrawList& dl, const Field<Dropdown<T>>& field,
                                    const WidgetNode& node) {
    auto& dd = field.get();
    size_t idx = enum_index(dd.value);
    bool has_custom = !dd.labels.empty();
    std::string label = has_custom ? dd.labels[idx] : enum_label<T>(idx);
    detail::DropdownLabelResolver resolver{
        .label_at = [&dd, has_custom](size_t i) -> std::string {
            return has_custom ? dd.labels[i] : enum_label<T>(i);
        },
        .count = enum_count<T>()
    };
    detail::dropdown_record(dl, const_cast<WidgetNode&>(node), label, resolver);
}

template <ScopedEnum T>
void Delegate<Dropdown<T>>::handle_input(Field<Dropdown<T>>& field, const InputEvent& ev,
                                          WidgetNode& node) {
    auto dd = field.get();
    size_t current = enum_index(dd.value);
    constexpr size_t count = enum_count<T>();
    detail::dropdown_handle_input(ev, node, current, count,
        [&](size_t idx) {
            dd.value = enum_from_index<T>(idx);
            field.set(dd);
        });
}

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

    void close_overlays() { close_overlays_impl(root_); }

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
    WidgetId focused_id_ = 0;
    std::vector<WidgetId> focus_order_;
    std::unordered_map<WidgetId, WidgetNode*> index_;

    void build_index(WidgetNode& node) {
        index_[node.id] = &node;
        if (node.wire) {
            node.wire(node);
            node.wire = nullptr;
        }
        if (!node.is_container && node.focus_policy != FocusPolicy::none)
            focus_order_.push_back(node.id);
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

    static void close_overlays_impl(WidgetNode& node) {
        if (!node.overlay_draws.empty()) {
            node.overlay_draws.clear();
            node.edit_state.reset();
            node.dirty = true;
        }
        for (auto& c : node.children) close_overlays_impl(c);
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
            leaf.overlay_draws = node.overlay_draws;
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
        node.focus_policy = Delegate<T>::focus_policy;

        node.record = [&field](WidgetNode& n) {
            n.draws.clear();
            n.overlay_draws.clear();
            Delegate<T>::record(n.draws, field, n);
        };
        node.record(node);

        node.wire = [&field](WidgetNode& n) {
            n.connections.push_back(
                n.on_input.connect([&field, &n](const InputEvent& ev) {
                    Delegate<T>::handle_input(field, ev, n);
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
