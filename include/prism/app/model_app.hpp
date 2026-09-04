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
#include <atomic>
#include <functional>
#include <memory>
#include <prism/core/error_hub.hpp>
#include <prism/core/mpsc_queue.hpp>

namespace prism::app {
using namespace prism::core;
using namespace prism::input;
using namespace prism::ui;

inline thread_local bool detail_is_logic_thread = false;
inline thread_local bool detail_in_mutation_batch = false;

class AppContext {
public:
    using scheduler_type = decltype(std::declval<stdexec::run_loop>().get_scheduler());

    explicit AppContext(scheduler_type s, AnimationClock& c, Window& w, Backend& b,
                         WindowRegistry& r, std::function<void(const KeyPress&)>& key_handler,
                         std::function<void()>& post_dispatch_hook,
                         std::shared_ptr<mpsc_queue<std::function<void()>>> q,
                         std::shared_ptr<std::atomic<bool>> scheduled,
                         std::shared_ptr<std::atomic<bool>> closed,
                         std::shared_ptr<std::function<void()>> drain_publish,
                         std::shared_ptr<std::function<void(std::function<void()>)>> logic_wrapper = nullptr)
        : sched_(s), clock_(&c), window_(&w), backend_(&b), registry_(&r),
          key_handler_(&key_handler), post_dispatch_hook_(&post_dispatch_hook),
          queue_(std::move(q)), scheduled_(std::move(scheduled)),
          closed_(std::move(closed)), drain_publish_(std::move(drain_publish)),
          logic_wrapper_(std::move(logic_wrapper)) {}

    // Keep old 3-arg ctor for tests that construct BackendBase directly without queue —
    // delegates to the full ctor with empty queue (post becomes no-op).
    explicit AppContext(scheduler_type s, AnimationClock& c, Window& w, Backend& b,
                         WindowRegistry& r, std::function<void(const KeyPress&)>& key_handler,
                         std::function<void()>& post_dispatch_hook)
        : AppContext(s, c, w, b, r, key_handler, post_dispatch_hook,
                     std::make_shared<mpsc_queue<std::function<void()>>>(),
                     std::make_shared<std::atomic<bool>>(false),
                     std::make_shared<std::atomic<bool>>(false),
                     std::make_shared<std::function<void()>>()) {}

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
    // Injectable wrapper for logic-thread work (e.g. GIL acquire for Python callbacks).
    // Keep SDL-free: caller provides the wrapping logic.
    void set_logic_wrapper(std::function<void(std::function<void()>)> w) {
        if (logic_wrapper_) *logic_wrapper_ = std::move(w);
        else logic_wrapper_ = std::make_shared<std::function<void(std::function<void()>)>>(std::move(w));
    }
    static void apply_wrapper(const std::shared_ptr<std::function<void(std::function<void()>)>>& w,
                              std::function<void()> fn) {
        if (w && *w) (*w)(std::move(fn));
        else fn();
    }
    void run_wrapped(std::function<void()> fn) const {
        apply_wrapper(logic_wrapper_, std::move(fn));
    }

    struct PostHandle {
        std::weak_ptr<mpsc_queue<std::function<void()>>> queue;
        std::weak_ptr<std::atomic<bool>> scheduled;
        std::weak_ptr<std::atomic<bool>> closed;
        std::weak_ptr<std::function<void()>> drain_publish;
        scheduler_type sched;
    };
    PostHandle post_handle() const { return {queue_, scheduled_, closed_, drain_publish_, sched_}; }
    std::shared_ptr<std::function<void(std::function<void()>)>> logic_wrapper_ptr() const { return logic_wrapper_; }

    void post(std::function<void()> fn) {
        if (!fn) return;
        if (closed_ && closed_->load(std::memory_order_acquire)) return;
        if (detail_is_logic_thread) {
            if (detail_in_mutation_batch) {
                // Re-entrant from within a batch drain — defer publish to the
                // outer batch's tail (publish-after-whole-batch ordering).
                run_wrapped(std::move(fn));
                return;
            }
            run_wrapped([&]{
                fn();
                if (drain_publish_ && *drain_publish_) (*drain_publish_)();
            });
            return;
        }
        queue_->push(std::move(fn));
        bool expected = false;
        if (scheduled_->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            exec::start_detached(stdexec::schedule(sched_)
                                 | stdexec::then([this] { drain_queue(); }));
        }
    }

private:
    // CAS-reschedule drain: pop every queued mutation, run the publish tail,
    // then either exit or re-arm if a producer pushed between the last pop
    // and the flag reset.
    void drain_queue() {
        auto do_drain = [&]{
            do {
                detail_in_mutation_batch = true;
                while (auto f = queue_->pop()) {
                    try {
                        (*f)();
                    } catch (...) {
                        report_unhandled_error(std::current_exception());
                    }
                }
                detail_in_mutation_batch = false;
                if (drain_publish_ && *drain_publish_) {
                    try {
                        (*drain_publish_)();
                    } catch (...) {
                        report_unhandled_error(std::current_exception());
                    }
                }
                scheduled_->store(false, std::memory_order_release);
                if (queue_->empty()) break;
                bool expected = false;
                if (!scheduled_->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) break;
            } while (true);
        };
        apply_wrapper(logic_wrapper_, do_drain);
    }
    scheduler_type sched_;
    AnimationClock* clock_;
    Window* window_;
    Backend* backend_;
    WindowRegistry* registry_;
    std::function<void(const KeyPress&)>* key_handler_;
    std::function<void()>* post_dispatch_hook_;
    std::shared_ptr<mpsc_queue<std::function<void()>>> queue_;
    std::shared_ptr<std::atomic<bool>> scheduled_;
    std::shared_ptr<std::atomic<bool>> closed_;
    std::shared_ptr<std::function<void()>> drain_publish_;
    std::shared_ptr<std::function<void(std::function<void()>)>> logic_wrapper_;
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

    // Destroyed by run() with the GIL released — queued closures must never capture nb::object.
    auto mutation_queue = std::make_shared<mpsc_queue<std::function<void()>>>();
    auto mutation_scheduled = std::make_shared<std::atomic<bool>>(false);
    auto mutation_closed = std::make_shared<std::atomic<bool>>(false);
    auto mutation_drain_publish = std::make_shared<std::function<void()>>();

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
#ifdef PRISM_DEBUG_TOOLS_ENABLED
        if (id == primary_id && debug_controller)
            debug_controller->update_stats(*entry.current_snap);
#endif
    };

    auto publish_dirty = [&] {
        registry.for_each_dirty([&](WindowId id, WindowRegistry::Entry& entry) {
            publish_entry(id, entry);
        });
    };

    // Shared wrapper holder for logic-thread work (GIL acquire for Python). SDL-free.
    auto logic_wrapper_holder = std::make_shared<std::function<void(std::function<void()>)>>();

    std::function<void()> schedule_tick;
    schedule_tick = [&] {
        if (!anim_clock.active() || tick_scheduled) return;
        tick_scheduled = true;
        exec::start_detached(
            stdexec::schedule(sched)
            | stdexec::then([&, logic_wrapper_holder] {
                auto do_tick = [&]{
                    tick_scheduled = false;
                    anim_clock.tick(AnimationClock::clock::now());
                    // schedule_tick() inside the shared tail no-ops when the
                    // clock went inactive, matching the old guarded reschedule.
                    (*mutation_drain_publish)();
                };
                AppContext::apply_wrapper(logic_wrapper_holder, do_tick);
            })
        );
    };

    // Shared tail for §2 mutation queue: drain all Shared/Channel, publish dirty, tick.
    // Factored from tick path's for_each(drain_shared) + publish_dirty (not the
    // single-window drain_shared in the input path — secondary windows would starve).
    *mutation_drain_publish = [&] {
        registry.for_each([&](WindowId, WindowRegistry::Entry& entry) {
            entry.tree->drain_shared();
        });
        publish_dirty();
        schedule_tick();
    };

    // SDL's window creation and event pump must run on the process's real main
    // thread on macOS (AppKit requirement -- Cocoa_CreateDevice() silently fails
    // off-main-thread), so backend.run() stays on whichever thread calls model_app()
    // here, and the stdexec run_loop that drives view rebuilding moves to a worker
    // thread instead.
    // Heap-allocate AppContext so raw AppContext* cached by callers (tests,
    // and future Python bindings holding weak_ptr to queue) never dangles
    // when the logic thread exits and would otherwise destroy a stack `ctx`.
    // The shared_ptr is held both here and by the logic thread; raw
    // pointers observed in setup()/Backend::run remain valid until join.
    auto ctx_holder = std::make_shared<AppContext>(sched, anim_clock, window, backend, registry,
                                                   global_key_handler, post_dispatch_hook,
                                                   mutation_queue, mutation_scheduled,
                                                   mutation_closed, mutation_drain_publish,
                                                   logic_wrapper_holder);
    std::thread logic_thread([&, ctx_holder, mutation_queue, mutation_scheduled, mutation_closed, mutation_drain_publish] {
        detail_is_logic_thread = true;
        backend.wait_ready();
        registry.for_each([&](WindowId id, WindowRegistry::Entry& entry) {
            publish_entry(id, entry);
        });

        auto& ctx = *ctx_holder;

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
                auto* debug_entry = registry.find(*debug_window_id);
                if (!debug_entry) return;
                debug_controller.emplace(*primary_entry->tree, *debug_entry->tree, debug_model);
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
    });

    backend.run([&](const WindowEvent& we) {
            const auto& ev = we.event;
            WindowId wid = we.window;
            auto closed_copy = mutation_closed;
            auto wrapper_for_dispatch = logic_wrapper_holder;
            exec::start_detached(
                stdexec::schedule(sched)
                | stdexec::then([&, ev, wid, closed_copy, wrapper_for_dispatch] {
                    auto do_dispatch = [&]{
                        if (closed_copy->load(std::memory_order_acquire)) return;
                        if (std::holds_alternative<WindowClose>(ev)) {
                            if (wid == primary_id) {
                                anim_clock.clear();
                                closed_copy->store(true, std::memory_order_release);
                                backend.quit();
                            } else {
                                registry.remove(wid);
                                backend.close_window(wid);
#ifdef PRISM_DEBUG_TOOLS_ENABLED
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
                        if (auto* kp = std::get_if<KeyPress>(&ev)) {
                            if (global_key_handler) global_key_handler(*kp);
                            // global_key_handler may have destroyed entry (e.g. debug inspector hotkey toggles window)
                            entry = registry.find(wid);
                            if (!entry) return;
                        }
                        widget_detail::route_event(*entry->tree, entry->current_snap.get(), ev);
                        if (entry->current_snap)
                            entry->window->set_cursor(entry->tree->desired_cursor());

                        entry->tree->drain_shared();
                        if (post_dispatch_hook) post_dispatch_hook();
                        if (needs_publish) publish_entry(wid, *entry);
                        publish_dirty();
                        schedule_tick();
                    };
                    AppContext::apply_wrapper(wrapper_for_dispatch, do_dispatch);
                })
            );
        });

    // Finish only after the pump returned: it keeps dispatching already-queued OS events
    // after quit(), and each one schedules onto this loop (see app.hpp). Cross-thread
    // post() still races this by design; its closed-flag check is best effort.
    loop.finish();
    logic_thread.join();
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
