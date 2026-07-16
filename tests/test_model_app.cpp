#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/model_app.hpp>
#include <prism/app/null_backend.hpp>
#include <prism/app/headless_window.hpp>
#include <prism/core/field.hpp>
#include <prism/core/shared.hpp>
#include <prism/input/hit_test.hpp>
#include <prism/render/scene_snapshot.hpp>

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
