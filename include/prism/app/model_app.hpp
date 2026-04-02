#pragma once

#include <prism/ui/animation.hpp>
#include <prism/app/backend.hpp>
#include <prism/core/exec.hpp>
#include <prism/input/hit_test.hpp>
#include <prism/input/input_event.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/ui/window_chrome.hpp>

#include <cstdint>
#include <thread>
#include <variant>

namespace prism {

class AppContext {
public:
    using scheduler_type = decltype(std::declval<stdexec::run_loop>().get_scheduler());

    explicit AppContext(scheduler_type s, AnimationClock& c, Window& w)
        : sched_(s), clock_(&c), window_(&w) {}
    scheduler_type scheduler() const { return sched_; }
    AnimationClock& clock() { return *clock_; }
    Window& window() { return *window_; }

private:
    scheduler_type sched_;
    AnimationClock* clock_;
    Window* window_;
};

namespace detail {

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
    if (id) {
        tree.set_pressed(*id, mb.pressed);
        if (mb.pressed) {
            if (*id != tree.focused_id())
                tree.close_overlays();
            tree.set_focused(*id);
        }
        auto rect = find_widget_rect(snap, *id);
        tree.dispatch(*id, rect ? localize_mouse(ev, *rect) : ev);
    } else if (!mb.pressed && tree.captured_id() != 0) {
        // Mouse released outside all widgets — release the captured widget
        auto cid = tree.captured_id();
        tree.set_pressed(cid, false);
        auto rect = find_widget_rect(snap, cid);
        tree.dispatch(cid, rect ? localize_mouse(ev, *rect) : ev);
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

} // namespace detail

template <typename Model>
void model_app(Backend& backend, Window& window, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    stdexec::run_loop loop;
    auto sched = loop.get_scheduler();

    WidgetTree tree(model);
    AnimationClock anim_clock;
    bool tick_scheduled = false;
    auto [w, h] = window.size();
    if (window.decoration_mode() == DecorationMode::Custom)
        h -= static_cast<int>(WindowChrome::title_bar_h);
    uint64_t version = 0;

    std::shared_ptr<const SceneSnapshot> current_snap;

    auto publish = [&] {
        current_snap = std::shared_ptr<const SceneSnapshot>(
            tree.build_snapshot(w, h, ++version));
        backend.submit(window.id(), current_snap);
        backend.wake();
        tree.clear_dirty();
    };

    std::function<void()> schedule_tick;
    schedule_tick = [&] {
        if (!anim_clock.active() || tick_scheduled) return;
        tick_scheduled = true;
        exec::start_detached(
            stdexec::schedule(sched)
            | stdexec::then([&] {
                tick_scheduled = false;
                anim_clock.tick(AnimationClock::clock::now());
                if (tree.any_dirty())
                    publish();
                if (anim_clock.active())
                    schedule_tick();
            })
        );
    };

    std::thread backend_thread([&] {
        backend.run([&](const WindowEvent& we) {
            const auto& ev = we.event;
            exec::start_detached(
                stdexec::schedule(sched)
                | stdexec::then([&, ev] {
                    if (std::holds_alternative<WindowClose>(ev)) {
                        loop.finish();
                        return;
                    }

                    bool needs_publish = false;
                    if (auto* resize = std::get_if<WindowResize>(&ev)) {
                        w = resize->width;
                        h = resize->height;
                        needs_publish = true;
                    }
                    if (current_snap) {
                        if (auto* mm = std::get_if<MouseMove>(&ev))
                            detail::route_mouse_move(tree, *current_snap, *mm);
                        if (auto* mb = std::get_if<MouseButton>(&ev))
                            detail::route_mouse_button(tree, *current_snap, ev, *mb);
                        if (auto* ms = std::get_if<MouseScroll>(&ev))
                            detail::route_mouse_scroll(tree, *current_snap, *ms);
                    }
                    if (auto* kp = std::get_if<KeyPress>(&ev))
                        detail::route_key_press(tree, ev, *kp);
                    if (std::get_if<TextInput>(&ev))
                        detail::route_text_input(tree, ev);

                    if (tree.any_dirty() || needs_publish)
                        publish();
                    schedule_tick();
                })
            );
        });
    });

    backend.wait_ready();
    publish();

    // AppContext must outlive setup — callbacks captured during setup use it.
    auto ctx = AppContext(sched, anim_clock, window);
    if (setup) {
        setup(ctx);
        schedule_tick();
    }

    loop.run();

    backend.quit();
    backend_thread.join();
}

template <typename Model>
void model_app(WindowConfig cfg, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    auto backend = Backend::software(RenderConfig{});
    auto& window = backend.create_window(cfg);
    model_app(backend, window, model, std::move(setup));
}

template <typename Model>
void model_app(std::string_view title, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    model_app(WindowConfig{.title = title.data()}, model, std::move(setup));
}

} // namespace prism
