#pragma once

#include <prism/ui/animation.hpp>
#include <prism/app/backend.hpp>
#include <prism/core/exec.hpp>
#include <prism/input/input_event.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/ui/window_chrome.hpp>
#include <prism/app/event_routing.hpp>
#include <prism/app/window_registry.hpp>
#ifdef PRISM_DEBUG_TOOLS_ENABLED
#include <prism/widgets/debug/tree_inspector.hpp>
#endif

#include <cstdint>
#include <optional>
#include <thread>
#include <variant>

namespace prism::app {
using namespace prism::core;
using namespace prism::input;
using namespace prism::ui;


class AppContext {
public:
    using scheduler_type = decltype(std::declval<stdexec::run_loop>().get_scheduler());

    explicit AppContext(scheduler_type s, AnimationClock& c, Window& w, Backend& b,
                         WindowRegistry& r, std::function<void(const KeyPress&)>& key_handler,
                         std::function<void()>& post_dispatch_hook)
        : sched_(s), clock_(&c), window_(&w), backend_(&b), registry_(&r),
          key_handler_(&key_handler), post_dispatch_hook_(&post_dispatch_hook) {}

    scheduler_type scheduler() const { return sched_; }
    AnimationClock& clock() { return *clock_; }
    Window& window() { return *window_; }
    Backend& backend() { return *backend_; }
    WindowRegistry& registry() { return *registry_; }
    void set_global_key_handler(std::function<void(const KeyPress&)> fn) {
        *key_handler_ = std::move(fn);
    }
    void set_post_dispatch_hook(std::function<void()> fn) {
        *post_dispatch_hook_ = std::move(fn);
    }

private:
    scheduler_type sched_;
    AnimationClock* clock_;
    Window* window_;
    Backend* backend_;
    WindowRegistry* registry_;
    std::function<void(const KeyPress&)>* key_handler_;
    std::function<void()>* post_dispatch_hook_;
};

template <typename Model>
void model_app(Backend& backend, Window& window, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    stdexec::run_loop loop;
    auto sched = loop.get_scheduler();

#ifdef PRISM_DEBUG_TOOLS_ENABLED
    // Declared before `registry` so it outlives the registry-owned debug WidgetTree:
    // that tree holds Connections into debug_model's SenderHubs, and locals destruct in
    // reverse declaration order. If debug_model destructed before registry (i.e. were
    // declared after it), quitting while the inspector is still attached would leave
    // registry's teardown disconnecting Connections into an already-freed debug_model —
    // a use-after-free. See the "quit while still attached" regression test below.
    debug::TreeInspectorModel debug_model;
    std::optional<WindowId> debug_window_id;
    std::optional<debug::TreeInspectorController> debug_controller;
#endif

    WindowRegistry registry;
    WindowId primary_id = registry.add(window, model);

    AnimationClock anim_clock;
    std::function<void(const KeyPress&)> global_key_handler;
    std::function<void()> post_dispatch_hook;
    bool tick_scheduled = false;

#ifdef PRISM_DEBUG_TOOLS_ENABLED
    // Shared by the hotkey's own detach branch and the generic secondary-WindowClose
    // path below (debug window closed via its own chrome) — both must leave the
    // inspector fully dormant so the next Ctrl+Shift+I press reopens it.
    auto reset_debug_inspector = [&] {
        debug_window_id.reset();
        debug_controller.reset();
        post_dispatch_hook = nullptr;
        // Closing the inspector (either path) must not leave a stale highlight rect on the
        // main window with no inspector left to explain it.
        if (auto* primary_entry = registry.find(primary_id))
            primary_entry->tree->set_debug_highlight(std::nullopt);
    };
#endif

    auto publish_entry = [&](WindowId id, WindowRegistry::Entry& entry) {
        entry.current_snap = std::shared_ptr<const SceneSnapshot>(
            entry.tree->build_snapshot(static_cast<float>(entry.width),
                                        static_cast<float>(entry.height),
                                        ++entry.version));
        backend.submit(id, entry.current_snap);
        backend.wake();
        entry.tree->clear_dirty();
    };

    auto publish_dirty = [&] {
        registry.for_each_dirty([&](WindowId id, WindowRegistry::Entry& entry) {
            publish_entry(id, entry);
        });
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
                publish_dirty();
                if (anim_clock.active())
                    schedule_tick();
            })
        );
    };

    std::thread backend_thread([&] {
        backend.run([&](const WindowEvent& we) {
            const auto& ev = we.event;
            WindowId wid = we.window;
            exec::start_detached(
                stdexec::schedule(sched)
                | stdexec::then([&, ev, wid] {
                    if (std::holds_alternative<WindowClose>(ev)) {
                        if (wid == primary_id) {
                            loop.finish();
                        } else {
                            registry.remove(wid);
                            backend.close_window(wid);
#ifdef PRISM_DEBUG_TOOLS_ENABLED
                            // Debug window closed via its own chrome (not the hotkey) —
                            // reset the same state the hotkey's detach branch would, so
                            // the next Ctrl+Shift+I press reopens rather than tries to
                            // detach an already-removed window.
                            if (debug_window_id && wid == *debug_window_id)
                                reset_debug_inspector();
#endif
                        }
                        return;
                    }

                    auto* entry = registry.find(wid);
                    if (!entry) return;

                    bool needs_publish = false;
                    if (auto* resize = std::get_if<WindowResize>(&ev)) {
                        entry->width = resize->width;
                        entry->height = resize->height;
                        needs_publish = true;
                    }
                    if (entry->current_snap) {
                        if (auto* mm = std::get_if<MouseMove>(&ev))
                            detail::route_mouse_move(*entry->tree, *entry->current_snap, *mm);
                        if (auto* mb = std::get_if<MouseButton>(&ev))
                            detail::route_mouse_button(*entry->tree, *entry->current_snap, ev, *mb);
                        if (auto* ms = std::get_if<MouseScroll>(&ev))
                            detail::route_mouse_scroll(*entry->tree, *entry->current_snap, *ms);
                    }
                    if (auto* kp = std::get_if<KeyPress>(&ev)) {
                        if (global_key_handler) global_key_handler(*kp);
                        detail::route_key_press(*entry->tree, ev, *kp);
                    }
                    if (std::get_if<TextInput>(&ev))
                        detail::route_text_input(*entry->tree, ev);

                    entry->tree->drain_shared();
                    if (post_dispatch_hook) post_dispatch_hook();
                    if (needs_publish) publish_entry(wid, *entry);
                    registry.for_each_dirty([&](WindowId id, WindowRegistry::Entry& e) {
                        publish_entry(id, e);
                    });
                    schedule_tick();
                })
            );
        });
    });

    backend.wait_ready();
    registry.for_each([&](WindowId id, WindowRegistry::Entry& entry) {
        publish_entry(id, entry);
    });

    // AppContext must outlive setup — callbacks captured during setup use it.
    auto ctx = AppContext(sched, anim_clock, window, backend, registry, global_key_handler, post_dispatch_hook);

#ifdef PRISM_DEBUG_TOOLS_ENABLED
    // Live tree inspector, toggled by Ctrl+Shift+I. This installs the sole
    // global-key-handler/post-dispatch-hook slots AppContext exposes — an
    // app's own setup() calling set_global_key_handler/set_post_dispatch_hook
    // below would silently override this wiring. Known limitation, not solved
    // here (see commit message).
    ctx.set_global_key_handler([&](const KeyPress& kp) {
        if (kp.key != keys::i || !(kp.mods & mods::ctrl) || !(kp.mods & mods::shift))
            return;
        if (!debug_window_id) {
            auto* win = backend.request_window(WindowConfig{.title = "PRISM Tree Inspector",
                                                              .decoration = DecorationMode::Custom});
            if (!win) return; // request failed — stay dormant, try again on next hotkey press
            auto* primary_entry = registry.find(primary_id);
            if (!primary_entry) return;
            debug_window_id = registry.add(*win, debug_model);
            debug_controller.emplace(*primary_entry->tree, debug_model);
            ctx.set_post_dispatch_hook([&] {
                if (debug_controller) debug_controller->refresh();
            });
        } else {
            registry.remove(*debug_window_id);
            backend.close_window(*debug_window_id);
            reset_debug_inspector();
        }
    });
#endif

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

} // namespace prism::app
