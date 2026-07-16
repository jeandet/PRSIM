#pragma once

#include <prism/input/hit_test.hpp>
#include <prism/input/input_event.hpp>
#include <prism/app/widget_tree.hpp>

#include <variant>

namespace prism::app::detail {
using namespace prism::core;
using namespace prism::input;
using namespace prism::ui;

inline void route_mouse_move(WidgetTree& tree, const SceneSnapshot& snap,
                             const MouseMove& mm) {
    auto hovered = hit_test(snap, mm.position);
    tree.update_hover(hovered);
    if (tree.in_scrollbar_drag()) {
        tree.update_scrollbar_drag(mm.position.y);
        return;
    }
    if (auto cid = tree.captured_id(); cid != 0) {
        auto rect = find_widget_rect(snap, cid);
        InputEvent ev = mm;
        tree.dispatch(cid, rect ? localize_mouse(ev, *rect) : ev);
    } else if (hovered) {
        auto rect = find_widget_rect(snap, *hovered);
        InputEvent ev = mm;
        tree.dispatch(*hovered, rect ? localize_mouse(ev, *rect) : ev);
    }
}

inline void route_mouse_button(WidgetTree& tree, const SceneSnapshot& snap,
                               const InputEvent& ev, const MouseButton& mb) {
    if (!mb.pressed && tree.in_scrollbar_drag()) {
        tree.end_scrollbar_drag();
        tree.set_pressed(tree.captured_id(), false);
        return;
    }
    if (mb.pressed) {
        if (auto oid = hit_test_overlay(snap, mb.position)) {
            tree.begin_scrollbar_drag(*oid, mb.position.y);
            if (tree.in_scrollbar_drag()) return;
        }
    }
    auto id = hit_test(snap, mb.position);
    if (!mb.pressed && tree.captured_id() != 0) {
        auto cid = tree.captured_id();
        tree.set_pressed(cid, false);
        auto rect = find_widget_rect(snap, cid);
        tree.dispatch(cid, rect ? localize_mouse(ev, *rect) : ev);
    } else if (id) {
        tree.set_pressed(*id, mb.pressed);
        if (mb.pressed) {
            if (*id != tree.focused_id())
                tree.close_overlays();
            tree.set_focused(*id);
        }
        auto rect = find_widget_rect(snap, *id);
        tree.dispatch(*id, rect ? localize_mouse(ev, *rect) : ev);
    } else if (mb.pressed) {
        tree.close_overlays();
        tree.clear_focus();
    }
}

inline void route_mouse_scroll(WidgetTree& tree, const SceneSnapshot& snap,
                               const MouseScroll& ms) {
    constexpr float wheel_multiplier = 60.f;
    auto id = hit_test(snap, ms.position);
    if (id) {
        auto rect = find_widget_rect(snap, *id);
        InputEvent ev = ms;
        tree.dispatch(*id, rect ? localize_mouse(ev, *rect) : ev);
        tree.scroll_at(*id, DY{ms.dy.raw() * wheel_multiplier});
    }
}

inline void route_key_press(WidgetTree& tree, const InputEvent& ev,
                            const KeyPress& kp) {
    constexpr float page_delta = 200.f;
    if (kp.key == keys::tab) {
        if (kp.mods & mods::shift) tree.focus_prev();
        else tree.focus_next();
    } else if (kp.key == keys::page_up && tree.focused_id() != 0) {
        tree.scroll_at(tree.focused_id(), DY{-page_delta});
    } else if (kp.key == keys::page_down && tree.focused_id() != 0) {
        tree.scroll_at(tree.focused_id(), DY{page_delta});
    } else if (tree.focused_id() != 0) {
        tree.dispatch(tree.focused_id(), ev);
    }
}

inline void route_text_input(WidgetTree& tree, const InputEvent& ev) {
    if (tree.focused_id() != 0)
        tree.dispatch(tree.focused_id(), ev);
}

} // namespace prism::app::detail
