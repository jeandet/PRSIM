#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/model_app.hpp>
#include <prism/core/null_backend.hpp>
#include <prism/core/headless_window.hpp>
#include <prism/core/field.hpp>
#include <prism/core/hit_test.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <string>

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

#include <prism/core/delegate.hpp>
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
