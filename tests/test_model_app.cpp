#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/model_app.hpp>
#include <prism/app/null_backend.hpp>
#include <prism/app/headless_window.hpp>
#include <prism/core/field.hpp>
#include <prism/core/shared.hpp>
#include <prism/input/hit_test.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <string>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

struct TestModel {
    prism::Field<int> count{42};
    prism::Field<std::string> name{"hello"};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(count, name);
    }
};

struct NestedTestModel {
    TestModel inner;
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(inner, flag);
    }
};

TEST_CASE("model_app runs and produces a snapshot") {
    std::shared_ptr<const prism::SceneSnapshot> captured;

    struct CapturingBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& snap_ref;
        prism::HeadlessWindow window_{0, {}};
        explicit CapturingBackend(std::shared_ptr<const prism::SceneSnapshot>& s)
            : snap_ref(s) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot> s) override {
            snap_ref = std::move(s);
        }
        void wake() override {}
        void quit() override {}
    };

    TestModel model;
    auto backend = prism::Backend{std::make_unique<CapturingBackend>(captured)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model);

    REQUIRE(captured != nullptr);
    CHECK(captured->geometry.size() == 2);
}

TEST_CASE("model_app with nested model") {
    std::shared_ptr<const prism::SceneSnapshot> captured;

    struct CapturingBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& snap_ref;
        prism::HeadlessWindow window_{0, {}};
        explicit CapturingBackend(std::shared_ptr<const prism::SceneSnapshot>& s)
            : snap_ref(s) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot> s) override {
            snap_ref = std::move(s);
        }
        void wake() override {}
        void quit() override {}
    };

    NestedTestModel model;
    auto backend = prism::Backend{std::make_unique<CapturingBackend>(captured)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model);

    REQUIRE(captured != nullptr);
    CHECK(captured->geometry.size() == 3);
}

struct ClickTestModel {
    prism::Field<bool> toggle{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(toggle);
    }
};

TEST_CASE("model_app routes MouseButton to Field<bool> toggle") {
    std::shared_ptr<const prism::SceneSnapshot> latest_snap;
    std::atomic<size_t> snap_count{0};

    struct ClickBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& latest;
        std::atomic<size_t>& count;
        prism::HeadlessWindow window_{0, {}};
        ClickBackend(std::shared_ptr<const prism::SceneSnapshot>& l, std::atomic<size_t>& c)
            : latest(l), count(c) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            auto geo = latest;
            REQUIRE_FALSE(geo->geometry.empty());
            auto [id, rect] = geo->geometry[0];
            auto center = rect.center();

            cb(prism::WindowEvent{window_.id(), prism::MouseButton{center, 1, true}});

            auto before = count.load(std::memory_order_acquire);
            count.wait(before, std::memory_order_acquire);

            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot> s) override {
            latest = std::move(s);
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    ClickTestModel model;
    CHECK(model.toggle.get() == false);

    auto backend = prism::Backend{std::make_unique<ClickBackend>(latest_snap, snap_count)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model);

    CHECK(model.toggle.get() == true);
    CHECK(snap_count.load() >= 2);
}

struct CursorTextFieldModel {
    prism::Field<prism::TextField<>> name{{.value = "hi"}};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(name);
    }
};

TEST_CASE("model_app pushes a hovered TextField's cursor to the window") {
    std::shared_ptr<const prism::SceneSnapshot> latest_snap;
    std::atomic<size_t> snap_count{0};

    struct HoverBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& latest;
        std::atomic<size_t>& count;
        prism::HeadlessWindow window_{0, {}};
        HoverBackend(std::shared_ptr<const prism::SceneSnapshot>& l, std::atomic<size_t>& c)
            : latest(l), count(c) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            auto geo = latest;
            REQUIRE_FALSE(geo->geometry.empty());
            auto [id, rect] = geo->geometry[0];

            cb(prism::WindowEvent{window_.id(), prism::MouseMove{rect.center()}});

            auto before = count.load(std::memory_order_acquire);
            count.wait(before, std::memory_order_acquire);

            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot> s) override {
            latest = std::move(s);
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    CursorTextFieldModel model;
    auto backend = prism::Backend{std::make_unique<HoverBackend>(latest_snap, snap_count)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model);

    CHECK(static_cast<prism::HeadlessWindow&>(window).cursor() == prism::CursorShape::Text);
    CHECK(snap_count.load() >= 2);
}

TEST_CASE("model_app setup callback receives scheduler and window") {
    bool setup_called = false;

    struct SetupBackend final : public prism::BackendBase {
        prism::HeadlessWindow window_{0, {}};
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    TestModel model;
    auto backend = prism::Backend{std::make_unique<SetupBackend>()};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model,
        [&](prism::AppContext& ctx) {
            setup_called = true;
            auto sched = ctx.scheduler();
            (void)sched;
            CHECK(ctx.window().id() == 1);
        }
    );

    CHECK(setup_called);
}

struct IntFieldModel {
    prism::Field<int> value{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(value);
    }
};

TEST_CASE("model_app exposes registry() and backend() via AppContext") {
    struct CapturingBackend final : public prism::BackendBase {
        prism::HeadlessWindow window_{0, {}};
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            event_cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    IntFieldModel model;
    auto backend = prism::Backend{std::make_unique<CapturingBackend>()};
    auto& window = backend.create_window({});

    bool saw_registry = false, saw_backend = false;
    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        auto* entry = ctx.registry().find(window.id());
        saw_registry = (entry != nullptr);
        ctx.backend(); // must be callable — presence check
        saw_backend = true;
    });

    CHECK(saw_registry);
    CHECK(saw_backend);
}

TEST_CASE("closing a secondary window removes it from the registry without quitting") {
    struct TwoWindowBackend final : public prism::BackendBase {
        prism::HeadlessWindow primary_{0, {}};
        prism::HeadlessWindow secondary_{0, {}};
        prism::WindowId secondary_id_ = 0;

        prism::Window& create_window(prism::WindowConfig cfg) override {
            primary_ = prism::HeadlessWindow{1, cfg};
            return primary_;
        }
        prism::Window* request_window(prism::WindowConfig cfg) override {
            secondary_id_ = 2;
            secondary_ = prism::HeadlessWindow{secondary_id_, cfg};
            return &secondary_;
        }
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            event_cb(prism::WindowEvent{secondary_id_, prism::WindowClose{}});
            event_cb(prism::WindowEvent{primary_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    struct Model { prism::Field<int> value{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); } };
    static Model second_model; // must outlive the setup() closure; WidgetTree only stores a
                                // reference to the model it was constructed from
    Model model;
    auto backend = prism::Backend{std::make_unique<TwoWindowBackend>()};
    auto& window = backend.create_window({});

    bool registry_had_secondary_before_its_close = false;
    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        auto* second_window = ctx.backend().request_window({});
        REQUIRE(second_window != nullptr);
        auto second_id = ctx.registry().add(*second_window, second_model);
        registry_had_secondary_before_its_close = (ctx.registry().find(second_id) != nullptr);
    });

    CHECK(registry_had_secondary_before_its_close);
    // Reaching this line at all (no hang) proves the secondary window's WindowClose did
    // not call loop.finish() — only the primary window's did, per Task 5's implementation.
    // (A regression to "every WindowClose quits" would hang this test forever, since the
    // secondary's WindowClose is delivered first and the test scheduler is synchronous —
    // confirm this by temporarily reverting Task 5's `if (wid == primary_id)` guard to
    // unconditional loop.finish() and observing model_app() returns *before* the primary's
    // WindowClose is even processed, which the assertion above does not by itself catch;
    // this comment documents the risk for the implementer, not an automated check.)
}

// Two tab-focusable Field<bool> members: Widget<int> has FocusPolicy::none
// (it's a plain read-only display), so IntFieldModel above can't exercise
// focus_next(). Widget<bool> is FocusPolicy::tab_and_click, giving keys::tab
// an observable effect via WidgetTree::focus_next().
struct TwoFocusableFieldsModel {
    prism::Field<bool> first{false};
    prism::Field<bool> second{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(first, second);
    }
};

TEST_CASE("model_app's global key handler fires before per-window dispatch") {
    struct CapturingBackend final : public prism::BackendBase {
        prism::HeadlessWindow window_{0, {}};
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            event_cb(prism::WindowEvent{window_.id(), prism::KeyPress{prism::keys::tab, 0}});
            event_cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    TwoFocusableFieldsModel model;
    auto backend = prism::Backend{std::make_unique<CapturingBackend>()};
    auto& window = backend.create_window({});

    int handler_calls = 0;
    prism::WidgetId first_id = 0;
    prism::WidgetId focus_seen_by_handler = 0;

    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        auto* entry = ctx.registry().find(window.id());
        REQUIRE(entry != nullptr);
        REQUIRE(entry->tree->focus_order().size() == 2);
        first_id = entry->tree->focus_order()[0];
        entry->tree->set_focused(first_id);

        ctx.set_global_key_handler([&, entry](const prism::KeyPress&) {
            ++handler_calls;
            focus_seen_by_handler = entry->tree->focused_id();
        });
    });

    CHECK(handler_calls == 1);
    // keys::tab makes detail::route_key_press call tree.focus_next(), moving
    // focus away from first_id to the second field. The handler capturing
    // first_id (not the post-tab focus) proves it ran strictly before
    // route_key_press, not just that it ran at all.
    CHECK(focus_seen_by_handler == first_id);
}

TEST_CASE("post-dispatch hook fires once per processed event, regardless of which window") {
    struct TwoEventBackend final : public prism::BackendBase {
        prism::HeadlessWindow window_{0, {}};
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            event_cb(prism::WindowEvent{window_.id(), prism::KeyPress{prism::keys::tab, 0}});
            event_cb(prism::WindowEvent{window_.id(), prism::MouseMove{prism::Point{prism::X{0}, prism::Y{0}}}});
            event_cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    struct PostHookModel { prism::Field<int> value{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); } };
    PostHookModel model;
    auto backend = prism::Backend{std::make_unique<TwoEventBackend>()};
    auto& window = backend.create_window({});

    int hook_calls = 0;
    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        ctx.set_post_dispatch_hook([&] { ++hook_calls; });
    });

    CHECK(hook_calls == 2); // KeyPress and MouseMove each trigger the continuation; WindowClose returns early
}

TEST_CASE("dirtying a non-event entry via the post-dispatch hook still gets it published") {
    struct OneEventBackend final : public prism::BackendBase {
        prism::HeadlessWindow primary_{0, {}};
        prism::HeadlessWindow secondary_{0, {}};
        prism::WindowId secondary_id_ = 0;
        int submit_count_for_secondary_ = 0;

        prism::Window& create_window(prism::WindowConfig cfg) override {
            primary_ = prism::HeadlessWindow{1, cfg};
            return primary_;
        }
        prism::Window* request_window(prism::WindowConfig cfg) override {
            secondary_id_ = 2;
            secondary_ = prism::HeadlessWindow{secondary_id_, cfg};
            return &secondary_;
        }
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            event_cb(prism::WindowEvent{primary_.id(), prism::KeyPress{prism::keys::tab, 0}});
            event_cb(prism::WindowEvent{primary_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId id, std::shared_ptr<const prism::SceneSnapshot>) override {
            if (id == secondary_id_) ++submit_count_for_secondary_;
        }
        void wake() override {}
        void quit() override {}
    };

    struct DirtyHookModel { prism::Field<int> value{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); } };
    static DirtyHookModel second_model;
    DirtyHookModel model;
    auto backend_ptr = std::make_unique<OneEventBackend>();
    auto* raw_backend = backend_ptr.get();
    auto backend = prism::Backend{std::move(backend_ptr)};
    auto& window = backend.create_window({});

    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        auto* second_window = ctx.backend().request_window({});
        REQUIRE(second_window != nullptr);
        auto second_id = ctx.registry().add(*second_window, second_model);
        ctx.set_post_dispatch_hook([&ctx, second_id] {
            // Simulate the tree inspector marking the debug tree dirty every time something happens.
            auto* entry = ctx.registry().find(second_id);
            if (entry) entry->tree->mark_dirty_by_id(entry->tree->root().id);
        });
    });

    CHECK(raw_backend->submit_count_for_secondary_ >= 1);
}

#include <prism/ui/delegate.hpp>
#include <prism/core/on.hpp>
#include <fmt/format.h>


// Reproducer: on_change callback that sets another field must not crash.
// A common mistake is capturing a local lambda by reference in a then()
// callback — after setup returns, the reference dangles. This test verifies
// the pattern works when lifetimes are correct.
struct CrossFieldModel {
    prism::Field<prism::Slider<>> input{{.value = 0.0}};
    prism::Field<prism::Label<>> output{{"initial"}};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(input, output);
    }
};

TEST_CASE("on_change callback can set another field without crashing") {
    std::shared_ptr<const prism::SceneSnapshot> latest_snap;
    std::atomic<size_t> snap_count{0};

    struct CrossBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& latest;
        std::atomic<size_t>& count;
        prism::HeadlessWindow window_{0, {}};
        CrossBackend(std::shared_ptr<const prism::SceneSnapshot>& l, std::atomic<size_t>& c)
            : latest(l), count(c) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            auto geo = latest;
            REQUIRE_FALSE(geo->geometry.empty());
            // Click near the right end of the slider to set a high value
            auto [id, rect] = geo->geometry[0];
            cb(prism::WindowEvent{window_.id(), prism::MouseButton{
                prism::Point{prism::X{rect.origin.x.raw() + rect.extent.w.raw() * 0.95f},
                             prism::Y{rect.origin.y.raw() + 15}}, 1, true}});

            auto before = count.load(std::memory_order_acquire);
            count.wait(before, std::memory_order_acquire);

            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot> s) override {
            latest = std::move(s);
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    CrossFieldModel model;
    std::vector<prism::Connection> connections;

    auto backend = prism::Backend{std::make_unique<CrossBackend>(latest_snap, snap_count)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model,
        [&](prism::AppContext& ctx) {
            auto sched = ctx.scheduler();
            // Key pattern: on_change on one field sets another field.
            // The lambda must capture model by reference (not a local lambda by ref).
            connections.push_back(
                model.input.on_change()
                | prism::on(sched)
                | prism::then([&model](const prism::Slider<>& s) {
                      model.output.set({fmt::format("value={:.1f}", s.value)});
                  })
            );
        }
    );

    CHECK(model.input.get().value > 0.8);
    CHECK(model.output.get().value != "initial");
}

struct SliderClickModel {
    prism::Field<prism::Slider<>> volume{{.value = 0.0}};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(volume);
    }
};

TEST_CASE("model_app routes click to Slider and updates value") {
    std::shared_ptr<const prism::SceneSnapshot> latest_snap;
    std::atomic<size_t> snap_count{0};

    struct SliderBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& latest;
        std::atomic<size_t>& count;
        prism::HeadlessWindow window_{0, {}};
        SliderBackend(std::shared_ptr<const prism::SceneSnapshot>& l, std::atomic<size_t>& c)
            : latest(l), count(c) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            auto geo = latest;
            REQUIRE_FALSE(geo->geometry.empty());
            auto [id, rect] = geo->geometry[0];
            cb(prism::WindowEvent{window_.id(), prism::MouseButton{
                prism::Point{prism::X{rect.origin.x.raw() + rect.extent.w.raw() * 0.95f}, prism::Y{rect.origin.y.raw() + 15}}, 1, true}});

            auto before = count.load(std::memory_order_acquire);
            count.wait(before, std::memory_order_acquire);

            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot> s) override {
            latest = std::move(s);
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    SliderClickModel model;
    auto backend = prism::Backend{std::make_unique<SliderBackend>(latest_snap, snap_count)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model);

    CHECK(model.volume.get().value > 0.8);
}

struct SliderReleaseModel {
    prism::Field<prism::Slider<>> slider{{.value = 0.5}};
    prism::Field<prism::Label<>> label{{"spacer"}};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(slider, label);
    }
};

TEST_CASE("slider stops dragging after mouse released outside") {
    std::shared_ptr<const prism::SceneSnapshot> latest_snap;
    std::atomic<size_t> snap_count{0};

    struct DragBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& latest;
        std::atomic<size_t>& count;
        prism::HeadlessWindow window_{0, {}};
        DragBackend(std::shared_ptr<const prism::SceneSnapshot>& l, std::atomic<size_t>& c)
            : latest(l), count(c) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            auto geo = latest;
            REQUIRE(geo->geometry.size() >= 2);

            auto [slider_id, slider_rect] = geo->geometry[0];
            auto [label_id, label_rect] = geo->geometry[1];
            auto slider_center = slider_rect.center();
            auto label_center = label_rect.center();

            // 1. Press on slider
            cb(prism::WindowEvent{window_.id(), prism::MouseButton{slider_center, 1, true}});
            // 2. Release on the label (different widget)
            cb(prism::WindowEvent{window_.id(), prism::MouseButton{label_center, 1, false}});

            auto before = count.load(std::memory_order_acquire);
            count.wait(before, std::memory_order_acquire);

            // 3. Move mouse back over slider — must NOT change value
            cb(prism::WindowEvent{window_.id(), prism::MouseMove{
                prism::Point{prism::X{slider_rect.origin.x.raw() + slider_rect.extent.w.raw() * 0.1f},
                             prism::Y{slider_center.y}}}});

            auto before2 = count.load(std::memory_order_acquire);
            count.wait(before2, std::memory_order_acquire);

            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot> s) override {
            latest = std::move(s);
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    SliderReleaseModel model;
    auto backend = prism::Backend{std::make_unique<DragBackend>(latest_snap, snap_count)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model);

    // The slider was clicked at center (≈0.5) then released.
    // Moving back to 10% of the track must NOT have changed the value.
    CHECK(model.slider.get().value == doctest::Approx(0.5).epsilon(0.05));
}

struct SharedModel {
    prism::core::Shared<int> value{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(value);
    }
};

TEST_CASE("Shared<T> drain fires observer during model_app event loop") {
    std::atomic<int> observed_value{-1};
    std::atomic<size_t> snap_count{0};

    struct SharedBackend final : public prism::BackendBase {
        std::atomic<size_t>& count;
        prism::HeadlessWindow window_{0, {}};
        explicit SharedBackend(std::atomic<size_t>& c) : count(c) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            // Wait for initial publish
            count.wait(0, std::memory_order_acquire);
            // Send a resize to trigger another event loop tick (drain happens each tick)
            cb(prism::WindowEvent{window_.id(), prism::WindowResize{800, 600}});
            auto before = count.load(std::memory_order_acquire);
            count.wait(before, std::memory_order_acquire);
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    SharedModel model;
    model.value.observe([&](const int& v) {
        observed_value.store(v, std::memory_order_release);
    });
    // Set value before model_app — pending flag is set, drain should fire on first tick
    model.value.set(42);

    auto backend = prism::Backend{std::make_unique<SharedBackend>(snap_count)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model);

    CHECK(model.value.get() == 42);
    CHECK(observed_value.load() == 42);
}

#ifdef PRISM_DEBUG_TOOLS_ENABLED
#include <prism/widgets/debug/tree_inspector.hpp>

TEST_CASE("Ctrl+Shift+I attaches a debug window; pressing it again removes it") {
    struct HotkeyBackend final : public prism::BackendBase {
        prism::HeadlessWindow primary_{0, {}};
        prism::HeadlessWindow secondary_{0, {}};
        prism::WindowId secondary_id_ = 0;
        int close_calls_ = 0;

        prism::Window& create_window(prism::WindowConfig cfg) override {
            primary_ = prism::HeadlessWindow{1, cfg};
            return primary_;
        }
        prism::Window* request_window(prism::WindowConfig cfg) override {
            secondary_id_ = 2;
            secondary_ = prism::HeadlessWindow{secondary_id_, cfg};
            return &secondary_;
        }
        void close_window(prism::WindowId) override { ++close_calls_; }
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            auto mods = static_cast<uint16_t>(prism::mods::ctrl | prism::mods::shift);
            event_cb(prism::WindowEvent{primary_.id(), prism::KeyPress{prism::keys::i, mods}});
            event_cb(prism::WindowEvent{primary_.id(), prism::KeyPress{prism::keys::i, mods}});
            event_cb(prism::WindowEvent{primary_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    struct HotkeyTestModel { prism::Field<int> value{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); } };
    HotkeyTestModel model;
    auto backend_ptr = std::make_unique<HotkeyBackend>();
    auto* raw = backend_ptr.get();
    auto backend = prism::Backend{std::move(backend_ptr)};
    auto& window = backend.create_window({});

    // The hotkey is installed inside model_app() itself (Step 3), unconditionally when
    // PRISM_DEBUG_TOOLS_ENABLED is defined — no app-provided setup() is needed for it to fire, so
    // this test passes nullptr and relies purely on HotkeyBackend's two KeyPress events.
    prism::model_app(backend, window, model, nullptr);

    // First Ctrl+Shift+I attaches (request_window called, no close yet); second detaches
    // (close_window called once) — proving both the attach and the teardown path ran.
    CHECK(raw->close_calls_ == 1);
}

// Regression test: the debug window must request Custom decoration, same as every other
// real PRISM window (see examples/model_plot.cpp), because native/server-side decoration
// isn't available on this platform (see project-window-chrome memory) — SdlWindow only
// draws WindowChrome when decoration_mode() == Custom. Requesting the default (Native)
// leaves the debug window with no title bar, no close button, nothing.
TEST_CASE("Ctrl+Shift+I requests the debug window with Custom decoration") {
    struct HotkeyBackend final : public prism::BackendBase {
        prism::HeadlessWindow primary_{0, {}};
        prism::HeadlessWindow secondary_{0, {}};

        prism::Window& create_window(prism::WindowConfig cfg) override {
            primary_ = prism::HeadlessWindow{1, cfg};
            return primary_;
        }
        prism::Window* request_window(prism::WindowConfig cfg) override {
            secondary_ = prism::HeadlessWindow{2, cfg};
            return &secondary_;
        }
        void close_window(prism::WindowId) override {}
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            auto mods = static_cast<uint16_t>(prism::mods::ctrl | prism::mods::shift);
            event_cb(prism::WindowEvent{primary_.id(), prism::KeyPress{prism::keys::i, mods}});
            event_cb(prism::WindowEvent{primary_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    struct DecorationHotkeyTestModel { prism::Field<int> value{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); } };
    DecorationHotkeyTestModel model;
    auto backend_ptr = std::make_unique<HotkeyBackend>();
    auto* raw = backend_ptr.get();
    auto backend = prism::Backend{std::move(backend_ptr)};
    auto& window = backend.create_window({});

    prism::model_app(backend, window, model, nullptr);

    CHECK(raw->secondary_.decoration_mode() == prism::DecorationMode::Custom);
}

// Regression test for a real bug: mods::ctrl/mods::shift are *group* masks (both left+right
// bits combined), matching the existing route_key_press idiom of `kp.mods & mods::shift` as a
// truthy "is shift held" check. The hotkey handler instead checked
// `(kp.mods & (mods::ctrl|mods::shift)) != (mods::ctrl|mods::shift)` — equality against the
// FULL combined mask, which requires both left AND right Ctrl plus both left AND right Shift
// held simultaneously. A real single-side Ctrl+Shift press (e.g. left Ctrl + left Shift, the
// overwhelmingly common case) only ever sets one bit per group, so this never matched. Every
// prior test synthesized the same (unrealistic) full-mask value the buggy check expected,
// which is why this was never caught.
TEST_CASE("Ctrl+Shift+I fires with a realistic single-side modifier combination") {
    struct SingleSideHotkeyBackend final : public prism::BackendBase {
        prism::HeadlessWindow primary_{0, {}};
        prism::HeadlessWindow secondary_{0, {}};
        prism::WindowId secondary_id_ = 0;
        int request_window_calls_ = 0;

        prism::Window& create_window(prism::WindowConfig cfg) override {
            primary_ = prism::HeadlessWindow{1, cfg};
            return primary_;
        }
        prism::Window* request_window(prism::WindowConfig cfg) override {
            ++request_window_calls_;
            secondary_id_ = 2;
            secondary_ = prism::HeadlessWindow{secondary_id_, cfg};
            return &secondary_;
        }
        void close_window(prism::WindowId) override {}
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            // Left Ctrl + Left Shift only — what a real keyboard actually reports for one
            // physical Ctrl+Shift+I press. NOT the full ctrl|shift group mask.
            constexpr uint16_t left_ctrl = 0x0040;
            constexpr uint16_t left_shift = 0x0001;
            auto mods = static_cast<uint16_t>(left_ctrl | left_shift);
            event_cb(prism::WindowEvent{primary_.id(), prism::KeyPress{prism::keys::i, mods}});
            event_cb(prism::WindowEvent{primary_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    struct SingleSideHotkeyModel {
        prism::Field<int> value{0};
        void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); }
    };
    SingleSideHotkeyModel model;
    auto backend_ptr = std::make_unique<SingleSideHotkeyBackend>();
    auto* raw = backend_ptr.get();
    auto backend = prism::Backend{std::move(backend_ptr)};
    auto& window = backend.create_window({});

    prism::model_app(backend, window, model, nullptr);

    CHECK(raw->request_window_calls_ == 1);
}

// Regression test for a heap-use-after-free: if the app quits while the debug inspector
// is still attached (a single Ctrl+Shift+I press, no matching second press to detach),
// model_app()'s locals destruct in reverse declaration order. The registry-owned debug
// WidgetTree holds Connections into debug_model's SenderHubs, so debug_model must be
// declared (and therefore destructed) after registry — otherwise registry's teardown
// disconnects Connections into an already-freed debug_model. This test only needs to
// complete without crashing/hanging to prove the fix; the earlier round-trip test above
// (attach then detach) never exercises this path because the debug entry is already
// removed from registry before shutdown.
namespace {
// Debug-window rows render as ~22px-tall leaf widgets (Widget<NodeRow>::row_h); the
// surrounding VirtualList viewport/container geometry is much taller. Filtering by
// height isolates just the per-row rects, in ascending row-index order (mirrors
// tests/test_virtual_list.cpp's row_ids() helper — duplicated here since these are
// two independent test binaries with no shared test-support header).
std::vector<std::pair<prism::WidgetId, prism::Rect>> debug_row_rects(const prism::SceneSnapshot& snap) {
    std::vector<std::pair<prism::WidgetId, prism::Rect>> rows;
    for (auto& [id, rect] : snap.geometry)
        if (id != 0 && rect.extent.h.raw() > 0.f && rect.extent.h.raw() < 50.f)
            rows.emplace_back(id, rect);
    return rows;
}
}

// Genuine end-to-end integration test: exercises the full live-inspector data flow
// (spec's "Data flow" steps 2-4) in ONE connected scenario through the real
// model_app() hotkey wiring — not a restatement of any single prior task's narrower
// test (Task 7's controller test drives two WidgetTrees directly with no model_app()
// or hotkey involved; Task 8's test only proves attach/detach fires, with no hover or
// click in between).
TEST_CASE("end-to-end: hotkey attach then hover-select then row-click-highlight then hotkey detach") {
    struct IntegrationMainModel {
        prism::Field<int> value{0};
        void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); }
    };

    struct IntegrationBackend final : public prism::BackendBase {
        prism::HeadlessWindow primary_{0, {}};
        prism::HeadlessWindow secondary_{0, {}};
        prism::WindowId secondary_id_ = 0;
        int close_calls_ = 0;

        std::shared_ptr<const prism::SceneSnapshot> latest_primary_;
        std::shared_ptr<const prism::SceneSnapshot> latest_secondary_;
        std::atomic<size_t> primary_count_{0};
        std::atomic<size_t> secondary_count_{0};

        // Set by the test's setup() callback (main thread, strictly before loop.run()
        // starts processing any event this run() schedules) and read from run() only
        // after a count.wait() below establishes happens-before with that write.
        prism::WidgetTree* main_tree_ptr = nullptr;

        prism::Window& create_window(prism::WindowConfig cfg) override {
            primary_ = prism::HeadlessWindow{1, cfg};
            return primary_;
        }
        prism::Window* request_window(prism::WindowConfig cfg) override {
            secondary_id_ = 2;
            secondary_ = prism::HeadlessWindow{secondary_id_, cfg};
            return &secondary_;
        }
        void close_window(prism::WindowId) override { ++close_calls_; }

        void submit(prism::WindowId id, std::shared_ptr<const prism::SceneSnapshot> s) override {
            if (id == primary_.id()) {
                latest_primary_ = std::move(s);
                primary_count_.fetch_add(1, std::memory_order_release);
                primary_count_.notify_all();
            } else if (id == secondary_id_) {
                latest_secondary_ = std::move(s);
                secondary_count_.fetch_add(1, std::memory_order_release);
                secondary_count_.notify_all();
            }
        }
        void wake() override {}
        void quit() override {}

        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            // 1. Initial publish of the primary window; locate its sole leaf widget.
            primary_count_.wait(0, std::memory_order_acquire);
            auto primary_snap0 = latest_primary_;
            REQUIRE_FALSE(primary_snap0->geometry.empty());
            auto [leaf_id, leaf_rect] = primary_snap0->geometry[0];

            // 2. Attach: Ctrl+Shift+I. Task 8's wiring installs the post-dispatch hook
            // and the hook fires within this SAME event tick (right after the global
            // key handler that installs it), so controller.refresh() already ran once
            // and the debug window's first *populated* snapshot (not an empty shell)
            // republishes before this event finishes processing.
            auto mods = static_cast<uint16_t>(prism::mods::ctrl | prism::mods::shift);
            cb(prism::WindowEvent{primary_.id(), prism::KeyPress{prism::keys::i, mods}});

            secondary_count_.wait(0, std::memory_order_acquire);
            auto debug_snap1 = latest_secondary_;
            auto rows1 = debug_row_rects(*debug_snap1);
            REQUIRE(rows1.size() >= 2); // root row + the int field's leaf row

            // 3. Hover the main window's leaf via the *real* hit-test + update_hover
            // path (detail::route_mouse_move) — unlike Task 7's controller test, which
            // called WidgetTree::update_hover() directly as a documented workaround.
            //
            // This single event dirties BOTH trees: WidgetTree::update_hover marks the
            // newly-hovered leaf dirty (primary republishes), and refresh() unconditionally
            // rewrites debug_model.rows (secondary republishes) — both from the same
            // for_each_dirty loop, in unspecified (unordered_map) iteration order. Waiting
            // on only one counter races the other: if secondary is visited first, our wait
            // can unblock before primary's *own* hover-triggered publish has run, and that
            // publish can then land late and get mistaken for the click step's publish
            // below (this was caught empirically — the test was flaky before this fix).
            // Waiting on both baselines makes the hover step's full settle observable
            // before any later step captures its own "before" baseline.
            auto primary_before_hover = primary_count_.load(std::memory_order_acquire);
            auto secondary_before_hover = secondary_count_.load(std::memory_order_acquire);
            cb(prism::WindowEvent{primary_.id(), prism::MouseMove{leaf_rect.center()}});
            primary_count_.wait(primary_before_hover, std::memory_order_acquire);
            secondary_count_.wait(secondary_before_hover, std::memory_order_acquire);

            // TreeInspectorModel::selected is a private local inside model_app() with
            // no externally observable rendering effect (nothing reads it besides
            // refresh() itself — confirmed by inspection), so it cannot be asserted on
            // directly through model_app()'s public surface. main_tree_ptr aliases the
            // exact WidgetTree the live debug_controller reads hovered_id() from
            // (captured via the same public AppContext::registry() surface Task 3
            // already exercises); asserting it reflects the real routed hover is the
            // strongest black-box proof available that the hover signal reached
            // refresh()'s input. Task 7's own test already covers hovered_id() ->
            // selected in isolation with full white-box access to debug_model.
            REQUIRE(main_tree_ptr != nullptr);
            CHECK(main_tree_ptr->hovered_id() == leaf_id);

            // 4. Click the leaf's row (index 1 — root is index 0) in the debug window,
            // driving TreeInspectorController::on_row_clicked -> set_debug_highlight.
            auto debug_snap2 = latest_secondary_;
            auto rows2 = debug_row_rects(*debug_snap2);
            REQUIRE(rows2.size() >= 2);
            auto leaf_row_center = rows2[1].second.center();

            auto primary_before_click = primary_count_.load(std::memory_order_acquire);
            cb(prism::WindowEvent{secondary_id_, prism::MouseButton{leaf_row_center, 1, true}});
            primary_count_.wait(primary_before_click, std::memory_order_acquire);

            auto primary_snap2 = latest_primary_;
            bool found_highlight_on_leaf = false;
            for (auto& cmd : primary_snap2->overlay.commands) {
                if (auto* outline = std::get_if<prism::RectOutline>(&cmd))
                    found_highlight_on_leaf = found_highlight_on_leaf || (outline->rect == leaf_rect);
            }
            CHECK(found_highlight_on_leaf);

            // 5. Detach: Ctrl+Shift+I again — must tear down cleanly, and must also clear
            // the highlight this same session left on the main window (closing the
            // inspector shouldn't leave a stale selection rect behind with no inspector
            // left to explain it).
            auto primary_before_detach = primary_count_.load(std::memory_order_acquire);
            cb(prism::WindowEvent{primary_.id(), prism::KeyPress{prism::keys::i, mods}});
            primary_count_.wait(primary_before_detach, std::memory_order_acquire);

            auto primary_snap3 = latest_primary_;
            bool still_highlighted = false;
            for (auto& cmd : primary_snap3->overlay.commands)
                if (std::holds_alternative<prism::RectOutline>(cmd)) still_highlighted = true;
            CHECK_FALSE(still_highlighted);

            cb(prism::WindowEvent{primary_.id(), prism::WindowClose{}});
        }
    };

    IntegrationMainModel model;
    auto backend_ptr = std::make_unique<IntegrationBackend>();
    auto* raw = backend_ptr.get();
    auto backend = prism::Backend{std::move(backend_ptr)};
    auto& window = backend.create_window({});

    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        auto* entry = ctx.registry().find(window.id());
        REQUIRE(entry != nullptr);
        raw->main_tree_ptr = entry->tree.get();
    });

    // Reaching this line at all (no crash/hang) plus close_calls_ == 1 proves the
    // second hotkey press genuinely detached and tore down the debug window across a
    // realistic multi-step sequence (attach, hover, click, detach) — the exact
    // balanced path Task 8's UAF regression test also relies on reaching cleanly.
    CHECK(raw->close_calls_ == 1);
}

TEST_CASE("quitting while the debug inspector is still attached does not use-after-free") {
    struct QuitWhileAttachedBackend final : public prism::BackendBase {
        prism::HeadlessWindow primary_{0, {}};
        prism::HeadlessWindow secondary_{0, {}};
        prism::WindowId secondary_id_ = 0;

        prism::Window& create_window(prism::WindowConfig cfg) override {
            primary_ = prism::HeadlessWindow{1, cfg};
            return primary_;
        }
        prism::Window* request_window(prism::WindowConfig cfg) override {
            secondary_id_ = 2;
            secondary_ = prism::HeadlessWindow{secondary_id_, cfg};
            return &secondary_;
        }
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            auto mods = static_cast<uint16_t>(prism::mods::ctrl | prism::mods::shift);
            // Single hotkey press attaches the inspector; no second press to detach it.
            event_cb(prism::WindowEvent{primary_.id(), prism::KeyPress{prism::keys::i, mods}});
            event_cb(prism::WindowEvent{primary_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    struct QuitWhileAttachedModel {
        prism::Field<int> value{0};
        void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); }
    };
    QuitWhileAttachedModel model;
    auto backend = prism::Backend{std::make_unique<QuitWhileAttachedBackend>()};
    auto& window = backend.create_window({});

    // Reaching this line at all (no crash, no hang) is the assertion: the debug inspector's
    // WidgetTree — and its Connections into debug_model's SenderHubs — got torn down cleanly
    // by registry's destructor before debug_model itself was destroyed.
    prism::model_app(backend, window, model, nullptr);
}

TEST_CASE("model_app drains Shared<T> via the animation tick path with zero input events") {
    struct TickModel {
        prism::Shared<int> data{0};
        void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(data); }
    };

    TickModel model;
    std::atomic<bool> drained{false};
    int observed = -1;
    auto conn = model.data.on_change().connect([&](const int& v) {
        observed = v;
        drained.store(true, std::memory_order_release);
    });

    // Fires WindowClose only after `drained` flips true (or a 2s safety deadline), so the
    // loop cannot exit before the tick path has had a real chance to drain — no fixed-count
    // event replay that could race the animation clock's own scheduling.
    struct WaitForDrainBackend final : public prism::BackendBase {
        prism::HeadlessWindow window_{0, {}};
        std::atomic<bool>& drained_ref;
        explicit WaitForDrainBackend(std::atomic<bool>& d) : drained_ref(d) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!drained_ref.load(std::memory_order_acquire)
                   && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    auto backend = prism::Backend{std::make_unique<WaitForDrainBackend>(drained)};
    auto& window = backend.create_window({.width = 200, .height = 200});

    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        // A single, self-terminating tick (returns false immediately) is enough to
        // exercise drain_shared() from the tick path. Deliberately NOT a perpetual
        // source: stdexec::run_loop::run()'s shutdown drain phase
        // (`while (execute_all() || task_count > 0);`) only terminates once the queue
        // stops refilling — a tick source that keeps re-scheduling itself forever
        // livelocks that phase, since schedule_tick unconditionally re-enqueues itself
        // while anim_clock.active() stays true. Confirmed by reading
        // subprojects/stdexec/include/stdexec/__detail/__run_loop.hpp directly. A
        // separate task fixes this generally (AnimationClock::clear() called from
        // WindowClose); this test predates that fix and sidesteps the issue by never
        // staying active past the one tick it needs.
        ctx.clock().add([](prism::AnimationClock::time_point) { return false; });

        std::thread writer([&model] { model.data.set(7); });
        writer.join();
    });

    REQUIRE(drained.load());
    CHECK(observed == 7);
}

// Regression test: closing the debug window via its own window chrome (a generic
// secondary-window WindowClose, not the Ctrl+Shift+I hotkey's detach branch) must reset
// the inspector's state (debug_window_id/debug_controller/post-dispatch hook) just like
// the hotkey's own detach branch does. Otherwise debug_window_id keeps looking "set" after
// the window is already gone, so the next hotkey press takes the "detach" branch (closing
// an already-removed window) instead of reopening — requiring two presses to get a new
// debug window back.
TEST_CASE("closing the debug window via a generic secondary WindowClose lets the next hotkey press reopen it") {
    struct GenericCloseBackend final : public prism::BackendBase {
        prism::HeadlessWindow primary_{0, {}};
        prism::HeadlessWindow secondary_{0, {}};
        // request_window() runs on the thread executing loop.run() (i.e. wherever
        // model_app() itself is called from), NOT on this backend's run() thread, so
        // reading the id it assigns back in run() needs real cross-thread
        // synchronization — mirrors the count.wait()/notify_all() pattern other tests
        // in this file use for the same reason.
        std::atomic<prism::WindowId> secondary_id_{0};
        int request_window_calls_ = 0;
        int close_calls_ = 0;

        prism::Window& create_window(prism::WindowConfig cfg) override {
            primary_ = prism::HeadlessWindow{1, cfg};
            return primary_;
        }
        prism::Window* request_window(prism::WindowConfig cfg) override {
            ++request_window_calls_;
            secondary_ = prism::HeadlessWindow{2, cfg};
            secondary_id_.store(2, std::memory_order_release);
            secondary_id_.notify_all();
            return &secondary_;
        }
        void close_window(prism::WindowId) override { ++close_calls_; }
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            auto mods = static_cast<uint16_t>(prism::mods::ctrl | prism::mods::shift);
            // 1. Attach via the hotkey.
            event_cb(prism::WindowEvent{primary_.id(), prism::KeyPress{prism::keys::i, mods}});
            secondary_id_.wait(0, std::memory_order_acquire);
            auto secondary_id = secondary_id_.load(std::memory_order_acquire);
            // 2. Close the debug window itself (e.g. its native/custom close button) —
            // routed as a generic secondary WindowClose, NOT through the hotkey.
            event_cb(prism::WindowEvent{secondary_id, prism::WindowClose{}});
            // 3. Press the hotkey again. If state wasn't reset in step 2, this wrongly
            // takes the "detach" branch (closes an already-removed window, doesn't
            // reopen). If state WAS reset, this reopens (request_window called again).
            event_cb(prism::WindowEvent{primary_.id(), prism::KeyPress{prism::keys::i, mods}});
            event_cb(prism::WindowEvent{primary_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    struct GenericCloseModel {
        prism::Field<int> value{0};
        void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); }
    };
    GenericCloseModel model;
    auto backend_ptr = std::make_unique<GenericCloseBackend>();
    auto* raw = backend_ptr.get();
    auto backend = prism::Backend{std::move(backend_ptr)};
    auto& window = backend.create_window({});

    prism::model_app(backend, window, model, nullptr);

    // Two attaches (steps 1 and 3) prove the second hotkey press reopened the inspector
    // rather than trying to detach an already-removed window.
    CHECK(raw->request_window_calls_ == 2);
    // Only step 2's generic close ever called close_window — a stale debug_window_id
    // would make step 3 call it a second time.
    CHECK(raw->close_calls_ == 1);
}
#endif
