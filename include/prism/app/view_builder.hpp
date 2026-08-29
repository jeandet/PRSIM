#pragma once

// Include <prism/app/widget_tree.hpp> instead of this header directly. ViewBuilder's inline
// method bodies call WidgetTree's methods and need it complete, which only happens because
// widget_tree.hpp includes this file at its own bottom, after WidgetTree's definition closes --
// including this header on its own leaves WidgetTree an incomplete forward declaration and
// fails with "invalid use of incomplete type 'class prism::app::WidgetTree'".

#include <prism/delegates/dropdown_delegates.hpp>
#include <prism/delegates/tabs_delegates.hpp>
#include <prism/delegates/text_delegates.hpp>
#include <prism/core/list.hpp>
#include <prism/core/traits.hpp>
#include <prism/core/state.hpp>
#include <prism/ui/layout.hpp>
#include <prism/ui/table.hpp>
#include <prism/ui/tree.hpp>
#include <prism/ui/widget_node.hpp>
#if __cpp_impl_reflection
#include <prism/core/reflect.hpp>
#endif

#include <algorithm>
#include <set>
#include <type_traits>
#include <vector>

namespace prism::app {
class WidgetTree;

using namespace prism::core;
using namespace prism::ui;

class ViewBuilder {
    WidgetTree& tree_;
    Node& target_;
    std::vector<Node*> stack_;
    std::set<const void*> placed_;
    TabsState* current_tabs_state_ = nullptr;

    Node& current_parent() {
        return stack_.empty() ? target_ : *stack_.back();
    }

    // Names a leaf after its enclosing field member (tree_.field_names_, populated by
    // build_node_tree()'s reflection walk) instead of node_leaf<T>'s generic value-type
    // fallback -- e.g. "count" instead of "int". No-op when the address isn't a reflected
    // member of any model this tree was built from (component() sub-trees populate their own
    // entries recursively) or when this build lacks reflection support.
    template <typename T>
    void apply_field_name([[maybe_unused]] Node& leaf, [[maybe_unused]] T& observable) {
#ifdef PRISM_DEBUG_TOOLS_ENABLED
#if __cpp_impl_reflection
        auto it = tree_.field_names_.find(static_cast<const void*>(&observable));
        if (it != tree_.field_names_.end()) leaf.debug_name = std::string(it->second);
#endif
#endif
    }

public:
    template <typename Self>
    struct DependsOnMixin {
        Node& node_ref;
        std::set<const void*>& placed_ref;

        template <typename Observable>
        Self& depends_on(Observable& obs) {
            if constexpr (is_field_v<Observable>)
                placed_ref.insert(&obs);
            node_ref.dependencies.push_back(
                [&obs](std::function<void()> cb) -> Connection {
                    return obs.on_change().connect(
                        [cb = std::move(cb)](const auto&) { cb(); });
                }
            );
            return static_cast<Self&>(*this);
        }

        Self& depends_on(auto&... obs) {
            (depends_on(obs), ...);
            return static_cast<Self&>(*this);
        }
    };

    struct TableBuilder : DependsOnMixin<TableBuilder> {
        TableBuilder& headers(std::vector<std::string> hdrs) {
            if (node_ref.table_state)
                node_ref.table_state->header_overrides = std::move(hdrs);
            return *this;
        }
    };

    struct CanvasHandle : DependsOnMixin<CanvasHandle> {
        CanvasHandle& min_size(Height h) { node_ref.canvas_min_height = h.raw(); return *this; }
        CanvasHandle& min_size(Width w) { node_ref.canvas_min_width = w.raw(); return *this; }
    };

public:
    ViewBuilder(WidgetTree& tree, Node& target)
        : tree_(tree), target_(target) {}

    template <typename T>
    void widget(Field<T>& field) {
        placed_.insert(&field);
        auto leaf = node_leaf(field, tree_.next_id_);
        apply_field_name(leaf, field);
        current_parent().children.push_back(std::move(leaf));
    }

    // Renders like widget(Field<T>&) but without focus/input wiring --
    // for prism::inspector's [[=prism::inspector::readonly]] annotation.
    template <typename T>
    void widget_readonly(Field<T>& field) {
        placed_.insert(&field);
        auto leaf = node_readonly_leaf<T>(field, tree_.next_id_);
        apply_field_name(leaf, field);
        current_parent().children.push_back(std::move(leaf));
    }

    template <typename T>
    void widget(Derived<T>& derived) {
        placed_.insert(&derived);
        auto leaf = node_readonly_leaf<T>(derived, tree_.next_id_);
        apply_field_name(leaf, derived);
        current_parent().children.push_back(std::move(leaf));
    }

    template <typename T>
    void widget(Shared<T>& shared) {
        placed_.insert(&shared);
        auto node = node_readonly_leaf<T>(shared, tree_.next_id_);
        apply_field_name(node, shared);
        node.drain_fn = [&shared] { shared.drain_notifications(); };
        current_parent().children.push_back(std::move(node));
    }

    [[nodiscard]] const std::set<const void*>& placed() const { return placed_; }

    template <typename C>
    void component(C& comp) {
        current_parent().children.push_back(tree_.build_node_tree(comp));
    }

    void vstack(auto&... args) { (item(args), ...); }

    WidgetId hstack(std::invocable auto&& fn) { return push_container(LayoutKind::Row, fn).id; }
    void hstack(auto&... args) { push_container(LayoutKind::Row, [&] { (item(args), ...); }); }
    WidgetId vstack(std::invocable auto&& fn) { return push_container(LayoutKind::Column, fn).id; }

private:
    void item(field_type auto& field) { widget(field); }
    template <typename T>
    void item(Derived<T>& d) { widget(d); }
    template <typename T>
    void item(Shared<T>& s) { widget(s); }
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
        if (kind == LayoutKind::Row || kind == LayoutKind::Column)
            wire_split_handles(ref);
        return ref;
    }

    // Shared by handle() (a bare divider with no drag wiring, e.g. if placed
    // outside a Row/Column) and wire_split_handles() (the real, draggable one) --
    // the hit-rect Handle gets in layout_flatten is the bounding box of whatever
    // this paints, so it must always cover the node's full allocated size.
    static void draw_divider(WidgetNode& node) {
        node.draws.clear();
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        auto sz = node_allocated(node);
        auto color = (vs.pressed || vs.hovered) ? t.divider_hover : t.divider;
        node.draws.filled_rect(ui::detail::make_rect(X{0}, Y{0}, sz.w, sz.h), color);
    }

    void wire_split_handles(Node& container) {
        bool vertical = (container.layout_kind == LayoutKind::Column);
        WidgetId container_id = container.id;
        // pane_count tracks non-Handle children seen so far, matching how
        // SplitState::pane_sizes is built (one entry per non-Handle child, in
        // order) -- a handle's real "before" pane is pane_count - 1, which is
        // NOT the same as how many handles precede it whenever more than one
        // pane sits between two handles (or before the first one).
        size_t pane_count = 0;
        for (size_t i = 0; i < container.children.size(); ++i) {
            auto& child = container.children[i];
            if (child.layout_kind != LayoutKind::Handle) {
                ++pane_count;
                continue;
            }
            size_t index = pane_count - 1;

            bool has_left = (i > 0);
            bool has_right = (i + 1 < container.children.size());
            bool is_split_divider = has_left && has_right;

            child.build_widget = [&tree = tree_, container_id, index, vertical, is_split_divider](WidgetNode& wn) {
                wn.focus_policy = FocusPolicy::none;
                if (is_split_divider) {
                    wn.visual_state.cursor = vertical ? CursorShape::ResizeNS : CursorShape::ResizeEW;
                }
                wn.record = &draw_divider;
                wn.wire = [&tree, container_id, index, vertical](WidgetNode& self) {
                    self.connections.push_back(self.on_input.connect(
                        [&tree, container_id, index, vertical, &self](const InputEvent& ev) {
                            float origin = vertical ? self.absolute_y.raw() : self.absolute_x.raw();
                            if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->button == 1) {
                                float local = vertical ? mb->position.y.raw() : mb->position.x.raw();
                                if (mb->pressed) tree.begin_split_drag(container_id, index, local + origin);
                                else tree.end_split_drag();
                            } else if (auto* mm = std::get_if<MouseMove>(&ev); mm && node_vs(self).pressed) {
                                float local = vertical ? mm->position.y.raw() : mm->position.x.raw();
                                tree.update_split_drag(local + origin);
                            }
                        }));
                };
                wn.record(wn);
            };
        }
    }

public:

    void spacer() {
        Node s;
        s.id = tree_.next_id_++;
        s.is_leaf = true;
        s.layout_kind = LayoutKind::Spacer;
        current_parent().children.push_back(std::move(s));
    }

    void handle() {
        Node h;
        h.id = tree_.next_id_++;
        h.is_leaf = true;
        h.layout_kind = LayoutKind::Handle;
        h.build_widget = [](WidgetNode& wn) {
            wn.focus_policy = FocusPolicy::none;
            wn.record = &draw_divider;
            wn.record(wn);
        };
        current_parent().children.push_back(std::move(h));
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
            auto& ss = wn.get_or_create<ScrollState>();
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
    void list(List<T>& items, std::function<void(size_t, const T&)> on_row_click = nullptr) {
        Node container;
        container.id = tree_.next_id_++;
        container.is_leaf = false;
        container.layout_kind = LayoutKind::VirtualList;
        container.vlist_item_count = items.size();

        container.vlist_bind_row = [&items, on_row_click](WidgetNode& wn, size_t index) {
            auto field_ptr = std::make_shared<Field<T>>(items[index]);
            wn.edit_state = std::shared_ptr<void>(field_ptr);
            wn.focus_policy = Widget<T>::focus_policy;
            wn.dirty = true;
            wn.is_container = false;
            wn.draws.clear();
            wn.overlay_draws.clear();
            wn.record = [field_ptr](WidgetNode& node) {
                node.draws.clear();
                node.overlay_draws.clear();
                Widget<T>::record(node.draws, *field_ptr, node);
            };
            wn.record(wn);
            wn.wire = [field_ptr, &items, index, on_row_click](WidgetNode& node) {
                node.connections.push_back(
                    node.on_input.connect([field_ptr, &node](const InputEvent& ev) {
                        Widget<T>::handle_input(*field_ptr, ev, node);
                    })
                );
                node.connections.push_back(
                    field_ptr->on_change().connect([field_ptr, &items, index](const T&) {
                        if (index < items.size()) items.set(index, field_ptr->get());
                    })
                );
                if (on_row_click) {
                    // Fires synchronously with a live items[index] ref during dispatch --
                    // a handler that mutates the list (e.g. erasing this row) risks it
                    // dangling mid-call; capture index/copy rather than mutate through it.
                    node.connections.push_back(
                        node.on_input.connect([on_row_click, &items, index](const InputEvent& ev) {
                            auto* mb = std::get_if<MouseButton>(&ev);
                            if (mb && mb->pressed && mb->button == 1 && index < items.size())
                                on_row_click(index, items[index]);
                        })
                    );
                }
            };
        };

        container.vlist_unbind_row = [](WidgetNode& wn) {
            wn.connections.clear();
            wn.draws.clear();
            wn.overlay_draws.clear();
            wn.edit_state.reset();
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

    void tree(TreeController& ctrl) {
        hstack([&] {
            Node container;
            container.id = tree_.next_id_++;
            container.is_leaf = false;
            container.layout_kind = LayoutKind::VirtualList;
            container.vlist_item_count = ctrl.rows.size();

            container.build_widget = [&ctrl, container_id = container.id, &tree = tree_](WidgetNode& wn) {
                wn.focus_policy = FocusPolicy::tab_and_click;
                wn.wire = [&ctrl, container_id, &tree](WidgetNode& self) {
                    self.connections.push_back(
                        self.on_input.connect([&ctrl, container_id, &tree](const InputEvent& ev) {
                            auto idx = ctrl.on_key(ev);
                            if (idx)
                                tree.scroll_row_into_view(container_id, *idx, Widget<TreeRow>::row_h);
                        })
                    );
                };
            };

            container.vlist_bind_row = [&ctrl, container_id = container.id, &tree = tree_](WidgetNode& wn, size_t index) {
                auto field_ptr = std::make_shared<Field<TreeRow>>(ctrl.rows[index]);
                wn.edit_state = std::shared_ptr<void>(field_ptr);
                wn.focus_policy = FocusPolicy::none; // rows never take focus; the container does
                wn.dirty = true;
                wn.is_container = false;
                wn.draws.clear();
                wn.overlay_draws.clear();
                wn.record = [field_ptr](WidgetNode& node) {
                    node.draws.clear();
                    node.overlay_draws.clear();
                    Widget<TreeRow>::record(node.draws, *field_ptr, node);
                };
                wn.record(wn);
                wn.wire = [field_ptr, &ctrl, index, container_id, &tree](WidgetNode& node) {
                    node.connections.push_back(
                        node.on_input.connect([&ctrl, index, container_id, &tree](const InputEvent& ev) {
                            auto* mb = std::get_if<MouseButton>(&ev);
                            if (mb && mb->pressed && mb->button == 1 && index < ctrl.rows.size()) {
                                tree.set_focused(container_id);
                                ctrl.on_row_clicked(index, ctrl.rows[index]);
                            }
                        })
                    );
                };
            };

            container.vlist_unbind_row = [](WidgetNode& wn) {
                wn.connections.clear();
                wn.draws.clear();
                wn.overlay_draws.clear();
                wn.edit_state.reset();
                wn.wire = nullptr;
                wn.record = nullptr;
                wn.dirty = false;
            };

            container.vlist_on_insert = [&ctrl](size_t, std::function<void()> cb) -> Connection {
                return ctrl.rows.on_insert().connect([cb = std::move(cb)](size_t, const auto&) { cb(); });
            };
            container.vlist_on_remove = [&ctrl](size_t, std::function<void()> cb) -> Connection {
                return ctrl.rows.on_remove().connect([cb = std::move(cb)](size_t) { cb(); });
            };
            container.vlist_on_update = [&ctrl](size_t, std::function<void()> cb) -> Connection {
                return ctrl.rows.on_update().connect([cb = std::move(cb)](size_t, const auto&) { cb(); });
            };

            current_parent().children.push_back(std::move(container));
            handle();
            widget(ctrl.detail);
        });
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

    template <typename T>
        requires SoaStorage<T>
    TableBuilder table(T& data) {
        Node container;
        container.id = tree_.next_id_++;
        container.is_leaf = false;
        container.layout_kind = LayoutKind::Table;

        auto state = std::make_shared<TableState>();
        state->source = wrap_soa_columns(data);
        state->column_count = state->source.column_count();
        container.table_state = state;

        current_parent().children.push_back(std::move(container));
        return TableBuilder{current_parent().children.back(), placed_};
    }
#endif // __cpp_impl_reflection

    void tabs(Field<TabBar<>>& field, std::invocable auto&& builder) {
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
            wn.tab_names = names_ptr;
            if (bar_build) bar_build(wn);
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

    template <typename F>
        requires std::invocable<F, ViewBuilder&>
    void tab(std::string_view name, F&& content_builder) {
        if (!current_tabs_state_) return;
        if (!current_tabs_state_->tab_names)
            current_tabs_state_->tab_names = std::make_shared<std::vector<std::string>>();
        current_tabs_state_->tab_names->push_back(std::string(name));
        current_tabs_state_->tab_node_builders.push_back(
            [&tree = tree_, cb = std::forward<F>(content_builder)]
            (Node& target) mutable {
                ViewBuilder vb{tree, target};
                cb(vb);
            });
    }

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

            auto& member = field.value.pages.[:m:];
            state->tab_node_builders.push_back(
                [&member, &tree = tree_](Node& target) {
                    target.children.push_back(tree.build_node_tree(member));
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
                prism::ui::detail::tabs_record(node.draws, node, field.get().selected, *names_ptr);
            };
            wn.record(wn);
            wn.wire = [&field, names_ptr](WidgetNode& node) {
                node.connections.push_back(
                    node.on_input.connect([&field, names_ptr, &node](const InputEvent& ev) {
                        size_t count = names_ptr->size();
                        if (count == 0) return;
                        prism::ui::detail::tabs_handle_input(ev, node, field.get().selected, count,
                            [&field](size_t idx) {
                                field.value.selected = idx;
                                field.on_change().emit(field.value);
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

        // Content container
        Node content;
        content.id = tree_.next_id_++;
        content.is_leaf = false;
        content.layout_kind = LayoutKind::Column;
        tabs_node.children.push_back(std::move(content));

        current_parent().children.push_back(std::move(tabs_node));
    }
#endif

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
                // Adopt the hoisted container's own id: callers that captured it
                // (e.g. hstack()'s single-lambda overload return value) must be
                // able to look it up in the built tree afterward -- without this,
                // the id returned to a view() like
                // `container_id = vb.hstack(...)` would refer to a Node discarded
                // right here, and begin_split_drag(container_id, ...) would
                // silently fail to find it.
                target_.id = target_.children[0].id;
                target_.children = std::move(target_.children[0].children);
            }
        }
    }
};

} // namespace prism::app
