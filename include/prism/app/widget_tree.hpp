#pragma once

#include <prism/delegates/dropdown_delegates.hpp>
#include <prism/delegates/tabs_delegates.hpp>
#include <prism/ui/layout.hpp>
#include <prism/core/list.hpp>
#include <prism/ui/table.hpp>
#include <prism/ui/tree.hpp>
#include <prism/delegates/text_delegates.hpp>
#include <prism/core/traits.hpp>
#include <prism/ui/widget_node.hpp>
#include <prism/app/widget_tree_layout.hpp>
#include <prism/app/widget_tree_traversal.hpp>
#if __cpp_impl_reflection
#include <prism/core/reflect.hpp>
#endif
#include <prism/core/state.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace prism::app {
using namespace prism::core;
using namespace prism::ui;
using namespace prism::app::widget_detail;


class ViewBuilder;

// index_ stores raw pointers into the tree -- valid only because the tree
// is fully built before build_index runs and never mutated after construction.
class WidgetTree {
public:
    using ViewBuilder = app::ViewBuilder;
    friend class app::ViewBuilder;

    template <typename Model>
    explicit WidgetTree(Model& model) {
        auto node_tree = build_node_tree(model);
        collect_drains(node_tree);
        root_ = build_widget_node(node_tree);
        propagate_theme(root_);
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
    [[nodiscard]] std::size_t dirty_count() const { return count_dirty(root_); }

    void clear_dirty() { clear_dirty_impl(root_); }

    void drain_shared() {
        for (auto& fn : drain_callbacks_)
            fn();
    }

    const Theme& theme() const { return theme_; }

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
                        if (sv->event_policy == ScrollEventPolicy::BubbleAtBounds) {
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

    void scroll_row_into_view(WidgetId container_id, size_t row_index, Height row_h) {
        auto it = index_.find(container_id);
        if (it == index_.end()) return;
        auto sv = get_scroll_view(*it->second);
        if (!sv) return;

        DY row_top{static_cast<float>(row_index) * row_h.raw()};
        DY row_bottom = row_top + DY{row_h.raw()};
        DY vp_top = sv->offset;
        DY vp_bottom = vp_top + DY{sv->viewport_h.raw()};
        DY max_off{std::max(0.f, sv->content_h.raw() - sv->viewport_h.raw())};

        if (row_bottom > vp_bottom)
            scroll_to(container_id, DY{std::clamp(row_bottom.raw() - sv->viewport_h.raw(), 0.f, max_off.raw())});
        else if (row_top < vp_top)
            scroll_to(container_id, DY{std::clamp(row_top.raw(), 0.f, max_off.raw())});
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
        if (hovered_id_ == 0) return; // 0 means "no widget under cursor"
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

    [[nodiscard]] CursorShape desired_cursor() const {
        WidgetId active = captured_id_ != 0 ? captured_id_ : hovered_id_;
        if (active == 0) return CursorShape::Default;
        auto it = index_.find(active);
        return it != index_.end() ? it->second->visual_state.cursor : CursorShape::Default;
    }

    struct ScrollbarDrag {
        WidgetId scroll_id = 0;
        Y anchor_y{0};       // mouse Y at drag start (absolute)
        DY anchor_offset{0}; // scroll offset at drag start
        Height viewport_h{0};
        Height content_h{0};
        Height thumb_h{0};
    };

    struct SplitDrag {
        WidgetId container_id = 0;
        size_t handle_index = 0;
        float anchor = 0.f;
        float orig_before = 0.f;
        float orig_after = 0.f;
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
        Height track_range = d.viewport_h - d.thumb_h;
        if (track_range.raw() <= 0) return;
        DY max_scroll{d.content_h.raw() - d.viewport_h.raw()};
        DY dy_pixels = mouse_y - d.anchor_y;
        float new_offset = std::clamp(
            d.anchor_offset.raw() + dy_pixels.raw() * max_scroll.raw() / track_range.raw(),
            0.f, max_scroll.raw());
        scroll_to(d.scroll_id, DY{new_offset});
    }

    void end_scrollbar_drag() {
        scrollbar_drag_ = {};
    }

    [[nodiscard]] bool in_scrollbar_drag() const { return scrollbar_drag_.scroll_id != 0; }

    void begin_split_drag(WidgetId container_id, size_t handle_index, float pos) {
        auto it = index_.find(container_id);
        if (it == index_.end()) return;
        auto& container_wn = *it->second;
        auto& ss = container_wn.get_or_create<SplitState>();
        bool vertical = (container_wn.layout_kind == LayoutKind::Column);
        if (!ss.engaged) {
            ss.pane_sizes.clear();
            for (auto& child : container_wn.children) {
                if (child.layout_kind == LayoutKind::Handle) continue;
                ss.pane_sizes.push_back(vertical ? child.arranged_extent.h.raw()
                                                  : child.arranged_extent.w.raw());
            }
            ss.engaged = true;
        }
        if (handle_index + 1 >= ss.pane_sizes.size()) return;
        split_drag_ = SplitDrag{
            .container_id = container_id,
            .handle_index = handle_index,
            .anchor = pos,
            .orig_before = ss.pane_sizes[handle_index],
            .orig_after = ss.pane_sizes[handle_index + 1],
        };
    }

    void update_split_drag(float pos) {
        if (split_drag_.container_id == 0) return;
        auto it = index_.find(split_drag_.container_id);
        if (it == index_.end()) return;
        auto* ss = std::any_cast<SplitState>(&it->second->edit_state);
        if (!ss) return;
        float delta = pos - split_drag_.anchor;
        float before = split_drag_.orig_before + delta;
        float after = split_drag_.orig_after - delta;
        if (before < splitter::min_pane_size_px) {
            after -= (splitter::min_pane_size_px - before);
            before = splitter::min_pane_size_px;
        }
        if (after < splitter::min_pane_size_px) {
            before -= (splitter::min_pane_size_px - after);
            after = splitter::min_pane_size_px;
        }
        ss->pane_sizes[split_drag_.handle_index] = before;
        ss->pane_sizes[split_drag_.handle_index + 1] = after;
        set_dirty(split_drag_.container_id);
    }

    void end_split_drag() {
        split_drag_ = {};
    }

    [[nodiscard]] bool in_split_drag() const { return split_drag_.container_id != 0; }

    [[nodiscard]] Connection connect_input(WidgetId id, std::function<void(const InputEvent&)> cb) {
        if (auto it = index_.find(id); it != index_.end())
            return it->second->on_input.connect(std::move(cb));
        return {};
    }

    [[nodiscard]] WidgetId hovered_id() const { return hovered_id_; }

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

    void set_debug_highlight(std::optional<WidgetId> id) {
        highlight_id_ = id;
        mark_dirty_by_id(root_.id);
    }

    [[nodiscard]] std::unique_ptr<SceneSnapshot> build_snapshot(float w, float h, uint64_t version) {
        auto build_start = std::chrono::steady_clock::now();
        // Counted before refresh_dirty() runs record() -- record() never touches the
        // dirty flag itself (only clear_dirty(), called by the caller after publish,
        // does), so this reflects exactly what changed since the last publish.
        std::size_t dirty_widgets = count_dirty(root_);
        refresh_dirty(root_);
        materialize_all_virtual_lists(root_);

        auto do_layout = [&] {
            LayoutNode layout;
            assert(root_.layout_kind != LayoutKind::Spacer);
            layout.kind = (root_.layout_kind == LayoutKind::Row)
                ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
            layout.id = root_.id;
            layout.theme = &theme_;
            // ViewBuilder::finalize() hoists a lone top-level Row/Column into
            // root_ itself (see its id-adoption comment), so this root layout
            // node is built by hand here rather than via build_layout()'s
            // Row/Column branch -- it needs the same split_sizes read as that
            // branch, or an engaged split on the sole top-level container
            // would silently never apply.
            if (auto* ss = std::any_cast<SplitState>(&root_.edit_state); ss && ss->engaged)
                layout.split_sizes = ss->pane_sizes;
            for (auto& c : root_.children)
                build_layout(c, layout);

            layout_measure(layout, LayoutAxis::Vertical);
            layout_arrange(layout, {Point{X{0}, Y{0}}, Size{Width{w}, Height{h}}});
            update_scroll_state(layout);
            update_split_state(layout);
            return layout;
        };

        std::vector<std::pair<WidgetId, Height>> viewports_before;
        collect_viewport_heights(root_, viewports_before);

        LayoutNode layout = do_layout();
        std::size_t layout_passes = 1;

        // Tables and virtual lists depend on viewport sizes set by update_scroll_state,
        // itself only known after this arrange pass -- on the first frame (or any real
        // resize) they were materialized against a stale/zero viewport, producing wrong
        // cell positions. Re-materialize and re-layout only when a viewport actually
        // changed, not on any unrelated dirty flag elsewhere in the tree: every publish
        // that touches a VirtualList/Table freshly rebinds its visible rows (marking them
        // dirty regardless of whether their viewport moved), so gating on check_dirty()
        // reran this whole second pass on every single publish for any app using one.
        std::vector<std::pair<WidgetId, Height>> viewports_after;
        collect_viewport_heights(root_, viewports_after);
        if (viewports_before != viewports_after) {
            materialize_all_virtual_lists(root_);
            layout = do_layout();
            ++layout_passes;
        }

        update_canvas_bounds(layout, Height{h});

        auto snap = std::make_unique<SceneSnapshot>();
        snap->version = version;
        layout_flatten(layout, *snap);
        resolve_clips(*snap);
        if (highlight_id_) {
            for (auto& [id, rect] : snap->geometry) {
                if (id == *highlight_id_) {
                    snap->overlay.rect_outline(rect, Color::rgba(255, 140, 0), 2.0f);
                    break;
                }
            }
        }

        snap->dirty_widget_count = dirty_widgets;
        snap->layout_pass_count = layout_passes;
        snap->draw_command_count = snap->overlay.size();
        snap->approx_bytes = snap->overlay.approx_bytes();
        for (auto& dl : snap->draw_lists) {
            snap->draw_command_count += dl->size();
            snap->approx_bytes += dl->approx_bytes();
        }
        snap->build_time_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - build_start).count();
        return snap;
    }

private:
    WidgetNode root_;
    Theme theme_;
#ifdef PRISM_DEBUG_TOOLS_ENABLED
#if __cpp_impl_reflection
    // Address -> reflected member identifier, accumulated across every build_node_tree() call
    // (the top-level model and every nested component()) -- lives as long as the tree itself, so
    // tab()'s lazily-materialized content builders (which run well after the initial tree build)
    // can still resolve names. See ViewBuilder::apply_field_name.
    std::unordered_map<const void*, std::string_view> field_names_;
#endif
#endif
    WidgetId next_id_ = 1;
    WidgetId hovered_id_ = 0;
    WidgetId focused_id_ = 0;
    WidgetId captured_id_ = 0;
    std::optional<WidgetId> highlight_id_;
    ScrollbarDrag scrollbar_drag_;
    SplitDrag split_drag_;
    std::vector<WidgetId> focus_order_;
    std::unordered_map<WidgetId, WidgetNode*> index_;
    std::unordered_map<WidgetId, WidgetId> parent_map_;
    std::vector<std::function<void()>> drain_callbacks_;

    void collect_drains(const Node& node) {
        if (node.drain_fn)
            drain_callbacks_.push_back(node.drain_fn);
        for (const auto& child : node.children)
            collect_drains(child);
    }

    // --- Node → WidgetNode conversion ---

    WidgetNode build_widget_node(Node& node) {
        WidgetNode wn;
        wn.id = node.id;
        wn.layout_kind = node.layout_kind;
        wn.theme = &theme_;
#ifdef PRISM_DEBUG_TOOLS_ENABLED
        wn.debug_name = node.debug_name;
#endif
        if (node.is_leaf) {
            wn.is_container = false;
            if (node.build_widget)
                node.build_widget(wn);
            if (node.layout_kind == LayoutKind::Canvas) {
                wn.canvas_min_width = node.canvas_min_width;
                wn.canvas_min_height = node.canvas_min_height;
            }
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
                vls->event_policy = node.scroll_event_policy;
                if (node.vlist_bind_row) vls->bind_row = node.vlist_bind_row;
                if (node.vlist_unbind_row) vls->unbind_row = node.vlist_unbind_row;
                wn.edit_state = vls;
                // Opt-in only: no existing .list() caller sets build_widget, so this is a no-op
                // for every VirtualList container except the one ViewBuilder::tree() builds below.
                if (node.build_widget)
                    node.build_widget(wn);
            } else if (node.layout_kind == LayoutKind::Table && node.table_state) {
                node.table_state->event_policy = node.scroll_event_policy;
                wn.edit_state = node.table_state;
                wn.focus_policy = FocusPolicy::tab_and_click;
            } else if (node.layout_kind == LayoutKind::Tabs && node.tabs_state) {
                wn.edit_state = node.tabs_state;
                for (auto& child : node.children)
                    wn.children.push_back(build_widget_node(child));
            }
            if (node.layout_kind != LayoutKind::VirtualList
                && node.layout_kind != LayoutKind::Table
                && node.layout_kind != LayoutKind::Tabs) {
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

            if (node.layout_kind == LayoutKind::Tabs) {
                if (node.children.size() >= 1 && wn.children.size() >= 1)
                    connect_dirty(node.children[0], wn.children[0]);
                auto id = wn.id;
                if (node.children.size() >= 1 && node.children[0].on_change) {
                    wn.connections.push_back(
                        node.children[0].on_change([this, id]() { set_dirty(id); })
                    );
                }
                return;
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
#ifdef PRISM_DEBUG_TOOLS_ENABLED
#if __cpp_impl_reflection
        // Every component() nesting call (PRISM's primary composition mechanism) wraps its
        // sub-tree in a root Node via this function. Without this, that wrapper's debug_name
        // stays empty and its layout_kind default (LayoutKind::Default) leaks through as the
        // row's displayed name -- see the analogous fallback comment on check_unplaced_fields'
        // model_name below.
        static constexpr std::string_view root_debug_name = std::meta::has_identifier(^^Model)
            ? std::meta::identifier_of(^^Model) : std::string_view{"<anonymous>"};
        root.debug_name = std::string(root_debug_name);
#endif
#endif

        if constexpr (requires(Model& m, ViewBuilder& vb) { m.view(vb); }) {
#ifdef PRISM_DEBUG_TOOLS_ENABLED
#if __cpp_impl_reflection
            // view()-driven placement (vb.widget(field)) only ever sees a Field<T>&, never the
            // member identifier that placed it -- unlike the whole-model reflection branch below,
            // which walks Model's own members and knows both. Recover the identifier here, keyed
            // by address, so ViewBuilder::apply_field_name can resolve it for whichever field
            // view() happens to place.
            static constexpr auto own_members = std::define_static_array(
                std::meta::nonstatic_data_members_of(
                    ^^Model, std::meta::access_context::unchecked()));
            template for (constexpr auto m : own_members) {
                auto& member = model.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;
                if constexpr (is_field_v<M> || is_derived_v<M> || is_shared_v<M>) {
                    field_names_.emplace(static_cast<const void*>(&member),
                                         std::meta::identifier_of(m));
                }
            }
#endif
#endif
            ViewBuilder vb{*this, root};
            model.view(vb);
            // vb.placed()/vb.finalize() are non-dependent calls on a non-dependent local
            // object; called directly here they'd require ViewBuilder complete at this point
            // in the file, which the forward-declared split (Steps 2-4) can't provide. Routing
            // them through a generic lambda makes the call dependent on the lambda's own
            // deduced template parameter, deferring the completeness check to instantiation
            // time -- the same mechanism model.view(vb) above already relies on.
            [&](auto& built) {
#if __cpp_impl_reflection
                check_unplaced_fields(model, built.placed());
#endif
                built.finalize();
            }(vb);
            if constexpr (requires(Model& m) { { m.drain() } -> std::same_as<void>; })
                root.drain_fn = [&model] { model.drain(); };
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
                    auto leaf = node_leaf(member, next_id_);
#ifdef PRISM_DEBUG_TOOLS_ENABLED
                    leaf.debug_name = std::string(std::meta::identifier_of(m));
#endif
                    root.children.push_back(std::move(leaf));
                } else if constexpr (is_derived_v<M>) {
                    using Inner = std::remove_cvref_t<decltype(member.get())>;
                    root.children.push_back(node_readonly_leaf<Inner>(member, next_id_));
                } else if constexpr (is_shared_v<M>) {
                    using Inner = std::remove_cvref_t<decltype(member.get())>;
                    auto node = node_readonly_leaf<Inner>(member, next_id_);
                    node.drain_fn = [&member] { member.drain_notifications(); };
                    root.children.push_back(std::move(node));
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
        // Model can be a class-template specialization reached through a deduced function-
        // template parameter (e.g. LeafSlot<M, ReadOnly>) -- on this reflection implementation
        // such a ^^Model reports has_identifier() == false, and identifier_of() would throw as
        // a consteval call. Fall back to a placeholder rather than crash the build; this is a
        // debug-only diagnostic string, not something callers parse. See the analogous
        // Shared<T>& -> Shared<T>* workaround in inspector.hpp for the same underlying bug.
        static constexpr std::string_view model_name = std::meta::has_identifier(^^Model)
            ? std::meta::identifier_of(^^Model) : std::string_view{"<anonymous>"};
        template for (constexpr auto m : members) {
            auto& member = model.[:m:];
            using M = std::remove_cvref_t<decltype(member)>;
            if constexpr (is_field_v<M> || is_derived_v<M> || is_shared_v<M>) {
                if (!placed.contains(&member)) {
                    std::fprintf(stderr, "[prism] warning: Field '%.*s' in %.*s not placed by view()\n",
                        static_cast<int>(std::meta::identifier_of(m).size()),
                        std::meta::identifier_of(m).data(),
                        static_cast<int>(model_name.size()),
                        model_name.data());
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
        if (node.focus_policy != FocusPolicy::none &&
            (!node.is_container || node.layout_kind == LayoutKind::Table ||
             node.layout_kind == LayoutKind::VirtualList))
            focus_order_.push_back(node.id);
        for (auto& c : node.children) {
            parent_map_[c.id] = node.id;
            build_index(c);
        }
    }

    void propagate_theme(WidgetNode& node) {
        node.theme = &theme_;
        for (auto& child : node.children)
            propagate_theme(child);
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
                    bool was_zero = ts->viewport_w.raw() == 0.f;
                    ts->viewport_h = Height{layout_node.allocated.extent.h.raw() - ts->row_height.raw()};
                    ts->viewport_w = layout_node.allocated.extent.w;
                    if (was_zero && ts->viewport_w.raw() > 0.f)
                        it->second->dirty = true;
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

    void update_split_state(LayoutNode& layout_node) {
        bool is_split_container = (layout_node.kind == LayoutNode::Kind::Row
                                 || layout_node.kind == LayoutNode::Kind::Column)
                                 && !layout_node.split_sizes.empty();
        if (is_split_container) {
            auto it = index_.find(layout_node.id);
            if (it != index_.end()) {
                if (auto* ss = std::any_cast<SplitState>(&it->second->edit_state); ss && ss->engaged) {
                    bool vertical = (layout_node.kind == LayoutNode::Kind::Column);
                    float available = vertical ? layout_node.allocated.extent.h.raw()
                                                : layout_node.allocated.extent.w.raw();
                    float handle_total = 0.f;
                    for (auto& child : layout_node.children)
                        if (child.kind == LayoutNode::Kind::Handle)
                            handle_total += splitter::thickness_px;
                    float pane_total = 0.f;
                    for (float sz : ss->pane_sizes) pane_total += sz;
                    float target = available - handle_total;
                    if (pane_total > 0.f && std::abs(target - pane_total) > 0.5f) {
                        float scale = target / pane_total;
                        for (auto& sz : ss->pane_sizes) sz *= scale;
                    }
                }
            }
        }
        for (auto& child : layout_node.children)
            update_split_state(child);
    }

    void update_canvas_bounds(LayoutNode& layout_node, Height viewport_h = Height{0}) {
        // Propagate absolute Y and viewport height to all indexed widget nodes
        if (layout_node.id != 0) {
            auto it = index_.find(layout_node.id);
            if (it != index_.end()) {
                it->second->absolute_x = layout_node.allocated.origin.x;
                it->second->absolute_y = layout_node.allocated.origin.y;
                it->second->viewport_height = viewport_h;
                it->second->arranged_extent = layout_node.allocated.extent;
            }
        }

        // Re-record leaf/canvas/handle widgets after layout so delegates can use their
        // allocated size instead of a pre-layout guess -- but only when that size actually
        // differs from wn->canvas_bounds (the size wn->draws was last produced at, whether
        // by refresh_dirty()'s pre-layout call this same build_snapshot(), or by this same
        // re-record last frame). No widget here sizes its own record() output from its field
        // value independent of allocated size (bounding_box() never measures text/points by
        // content, see draw_list.hpp), so if the size didn't move, the currently-held draws
        // are still correct -- skipping avoids a second full record() for every leaf/canvas/
        // handle in the window on every publish, including ones untouched by whatever else
        // made the window dirty (see benchmarks/stall_latency.cpp's
        // untouched_sibling_record_calls, and tests/test_record_reuse.cpp).
        if (layout_node.kind == LayoutNode::Kind::Leaf ||
            layout_node.kind == LayoutNode::Kind::Canvas ||
            layout_node.kind == LayoutNode::Kind::Handle) {
            auto it = index_.find(layout_node.id);
            auto* wn = (it != index_.end()) ? it->second : nullptr;
            // build_layout() already copied wn->draws/overlay_draws (as of refresh_dirty()'s
            // output) into layout_node -- nothing between there and here touches wn->draws,
            // so when we skip re-recording, layout_node's copy is already correct and doesn't
            // need refreshing.
            if (wn && wn->record && wn->canvas_bounds.extent != layout_node.allocated.extent) {
                wn->canvas_bounds = Rect{
                    Point{X{0}, Y{0}},
                    layout_node.allocated.extent
                };
                wn->record(*wn);
                layout_node.draws = wn->draws;
                layout_node.overlay_draws = wn->overlay_draws;
            }
            return;
        }
        for (auto& child : layout_node.children)
            update_canvas_bounds(child, viewport_h);
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
        auto* ts = get_tabs_state_ptr(node);
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
                propagate_theme(child_wn);
                connect_dirty(child_node, child_wn);
                parent_map_[child_wn.id] = content_wn.id;
                content_wn.children.push_back(std::move(child_wn));
            }

            for (auto& c : content_wn.children)
                build_index(c);
        }
        content_wn.dirty = true;
    }

    void materialize_virtual_list(WidgetNode& node) {
        auto* vls = get_vlist_state(node);
        if (!vls || !vls->bind_row) return;

        // Measure item height from first item if not yet known
        if (vls->item_height.raw() <= 0.f && vls->item_count.raw() > 0) {
            WidgetNode probe;
            probe.id = next_id_++;
            probe.theme = &theme_;
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
            wn.theme = &theme_;
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

        Width col_w = ts->column_count > 0
            ? Width{std::max(120.f, ts->viewport_w.raw() / static_cast<float>(ts->column_count))}
            : Width{120.f};

        size_t range_size = new_end.raw() - new_start.raw();
        node.children.reserve(range_size);
        for (size_t i = new_start.raw(); i < new_end.raw(); ++i) {
            WidgetNode wn;
            if (!ts->pool.empty()) {
                wn = std::move(ts->pool.back());
                ts->pool.pop_back();
            }
            wn.id = 0;  // cells are hit-test transparent; the table container handles input
            wn.dirty = true;
            wn.draws.clear();
            wn.theme = &theme_;

            size_t row_idx = i;
            bool selected = ts->selected_row.get().has_value() &&
                            ts->selected_row.get().value() == row_idx;

            auto bg = (row_idx % 2 == 0)
                ? wn.theme->table_row_even
                : wn.theme->table_row_odd;
            if (selected)
                bg = wn.theme->table_selected;

            Width total_w = col_w * static_cast<float>(ts->column_count);
            wn.draws.filled_rect(
                Rect{Point{X{0}, Y{0}},
                     Size{total_w, ts->row_height}},
                bg);

            for (size_t c = 0; c < ts->column_count; ++c) {
                std::string txt = ts->source.cell_text(row_idx, c);
                X cx{static_cast<float>(c) * col_w.raw()};
                wn.draws.clip_push(
                    Point{cx, Y{0}},
                    Size{col_w, ts->row_height});
                wn.draws.text(std::move(txt),
                    Point{X{4.f}, Y{4.f}},
                    14.f, wn.theme->text);
                wn.draws.clip_pop();

                if (c > 0) {
                    wn.draws.filled_rect(
                        Rect{Point{cx, Y{0}},
                             Size{Width{1.f}, ts->row_height}},
                        wn.theme->table_divider);
                }
            }

            // id=0 is shared by every row (hit-test transparent, see above);
            // registering it in index_/parent_map_ would alias to whichever
            // row was inserted last, so it's deliberately skipped here.
            node.children.push_back(std::move(wn));
        }

        for (auto& c : node.children)
            if (c.id != 0) index_[c.id] = &c;

        ts->visible_start = new_start;
        ts->visible_end = new_end;

        // Render header text into overlay_draws (picked up by table flatten)
        node.overlay_draws.clear();
        for (size_t c = 0; c < ts->column_count; ++c) {
            X cx{static_cast<float>(c) * col_w.raw()};
            auto hdr = ts->column_header(c);
            node.overlay_draws.text(std::string(hdr),
                Point{cx + DX{4.f}, Y{4.f}},
                14.f, node.theme->table_header_text);
        }

        // Wire input handler for selection (only once)
        if (!node.table_input_wired) {
        node.table_input_wired = true;
        auto node_id = node.id;
        node.connections.push_back(
            node.on_input.connect([ts, node_id, this](const InputEvent& ev) {
                auto it = index_.find(node_id);
                if (it == index_.end()) return;
                auto& n = *it->second;
                auto& t = *ts;
                auto table_max_scroll = [&t] {
                    return DY{std::max(0.f,
                        t.row_height.raw() * static_cast<float>(t.row_count()) - t.viewport_h.raw())};
                };
                if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == 1) {
                    Height header_h = t.row_height;
                    Y local_y = mb->position.y;
                    if (local_y.raw() < header_h.raw()) return;
                    DY body_y{local_y.raw() - header_h.raw() + t.scroll_y.raw()};
                    size_t row = static_cast<size_t>(body_y.raw() / t.row_height.raw());
                    if (row < t.row_count()) {
                        auto current = t.selected_row.get();
                        if (current.has_value() && current.value() == row)
                            t.selected_row.set(std::nullopt);
                        else
                            t.selected_row.set(row);
                        n.dirty = true;
                    }
                }
                if (auto* ms = std::get_if<MouseScroll>(&ev)) {
                    DY max_scroll = table_max_scroll();
                    if (ms->dy.raw() != 0.f) {
                        DY scroll_amount{ms->dy.raw() * t.row_height.raw() * 3.f};
                        t.scroll_y = DY{std::clamp(t.scroll_y.raw() + scroll_amount.raw(), 0.f, max_scroll.raw())};
                        n.dirty = true;
                    }
                }
                if (auto* kp = std::get_if<KeyPress>(&ev)) {
                    auto current = t.selected_row.get();
                    if (kp->key == keys::down) {
                        size_t next = current.has_value() ? current.value() + 1 : 0;
                        if (next < t.row_count()) {
                            t.selected_row.set(next);
                            n.dirty = true;
                        }
                    } else if (kp->key == keys::up) {
                        if (current.has_value() && current.value() > 0) {
                            t.selected_row.set(current.value() - 1);
                            n.dirty = true;
                        }
                    } else if (kp->key == keys::page_down) {
                        DY max_scroll = table_max_scroll();
                        t.scroll_y = DY{std::clamp(t.scroll_y.raw() + t.viewport_h.raw(), 0.f, max_scroll.raw())};
                        n.dirty = true;
                    } else if (kp->key == keys::page_up) {
                        DY max_scroll = table_max_scroll();
                        t.scroll_y = DY{std::clamp(t.scroll_y.raw() - t.viewport_h.raw(), 0.f, max_scroll.raw())};
                        n.dirty = true;
                    }
                    // Scroll selected row into view
                    if (auto sel = t.selected_row.get(); sel.has_value()) {
                        DY row_top{static_cast<float>(sel.value()) * t.row_height.raw()};
                        DY row_bottom = row_top + DY{t.row_height.raw()};
                        DY vp_top = t.scroll_y;
                        DY vp_bottom = vp_top + DY{t.viewport_h.raw()};
                        DY max_scroll = table_max_scroll();
                        if (row_bottom > vp_bottom)
                            t.scroll_y = DY{std::clamp(row_bottom.raw() - t.viewport_h.raw(), 0.f, max_scroll.raw())};
                        else if (row_top < vp_top)
                            t.scroll_y = DY{std::clamp(row_top.raw(), 0.f, max_scroll.raw())};
                    }
                }
            })
        );
        } // if (!node.table_input_wired)
    }

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

};

} // namespace prism::app

#include <prism/app/view_builder.hpp>
