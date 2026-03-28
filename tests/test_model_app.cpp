#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/model_app.hpp>
#include <prism/core/null_backend.hpp>
#include <prism/core/field.hpp>
#include <prism/core/hit_test.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <string>

struct TestModel {
    prism::Field<int> count{42};
    prism::Field<std::string> name{"hello"};
};

struct NestedTestModel {
    TestModel inner;
    prism::Field<bool> flag{false};
};

TEST_CASE("model_app runs and produces a snapshot") {
    std::shared_ptr<const prism::SceneSnapshot> captured;

    struct CapturingBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& snap_ref;
        explicit CapturingBackend(std::shared_ptr<const prism::SceneSnapshot>& s)
            : snap_ref(s) {}
        void run(std::function<void(const prism::InputEvent&)> cb) override {
            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot> s) override {
            snap_ref = std::move(s);
        }
        void wake() override {}
        void quit() override {}
    };

    TestModel model;
    prism::model_app(
        prism::Backend{std::make_unique<CapturingBackend>(captured)},
        prism::BackendConfig{.width = 800, .height = 600},
        model
    );

    REQUIRE(captured != nullptr);
    CHECK(captured->geometry.size() == 2);
}

TEST_CASE("model_app with nested model") {
    std::shared_ptr<const prism::SceneSnapshot> captured;

    struct CapturingBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& snap_ref;
        explicit CapturingBackend(std::shared_ptr<const prism::SceneSnapshot>& s)
            : snap_ref(s) {}
        void run(std::function<void(const prism::InputEvent&)> cb) override {
            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot> s) override {
            snap_ref = std::move(s);
        }
        void wake() override {}
        void quit() override {}
    };

    NestedTestModel model;
    prism::model_app(
        prism::Backend{std::make_unique<CapturingBackend>(captured)},
        prism::BackendConfig{.width = 800, .height = 600},
        model
    );

    REQUIRE(captured != nullptr);
    CHECK(captured->geometry.size() == 3);
}

struct ClickTestModel {
    prism::Field<bool> toggle{false};
};

TEST_CASE("model_app routes MouseButton to Field<bool> toggle") {
    std::shared_ptr<const prism::SceneSnapshot> latest_snap;
    std::atomic<size_t> snap_count{0};

    struct ClickBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& latest;
        std::atomic<size_t>& count;
        ClickBackend(std::shared_ptr<const prism::SceneSnapshot>& l, std::atomic<size_t>& c)
            : latest(l), count(c) {}
        void run(std::function<void(const prism::InputEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            auto geo = latest;
            REQUIRE_FALSE(geo->geometry.empty());
            auto [id, rect] = geo->geometry[0];
            auto center = rect.center();

            cb(prism::MouseButton{center, 1, true});

            auto before = count.load(std::memory_order_acquire);
            count.wait(before, std::memory_order_acquire);

            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot> s) override {
            latest = std::move(s);
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    ClickTestModel model;
    CHECK(model.toggle.get() == false);

    prism::model_app(
        prism::Backend{std::make_unique<ClickBackend>(latest_snap, snap_count)},
        prism::BackendConfig{.width = 800, .height = 600},
        model
    );

    CHECK(model.toggle.get() == true);
    CHECK(snap_count.load() >= 2);
}

TEST_CASE("model_app setup callback receives scheduler") {
    bool setup_called = false;

    struct SetupBackend final : public prism::BackendBase {
        void run(std::function<void(const prism::InputEvent&)> cb) override {
            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    TestModel model;
    prism::model_app(
        prism::Backend{std::make_unique<SetupBackend>()},
        prism::BackendConfig{.width = 800, .height = 600},
        model,
        [&](prism::AppContext& ctx) {
            setup_called = true;
            auto sched = ctx.scheduler();
            (void)sched;
        }
    );

    CHECK(setup_called);
}

#include <prism/core/delegate.hpp>

struct SliderClickModel {
    prism::Field<prism::Slider<>> volume{{.value = 0.0}};
};

TEST_CASE("model_app routes click to Slider and updates value") {
    std::shared_ptr<const prism::SceneSnapshot> latest_snap;
    std::atomic<size_t> snap_count{0};

    struct SliderBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& latest;
        std::atomic<size_t>& count;
        SliderBackend(std::shared_ptr<const prism::SceneSnapshot>& l, std::atomic<size_t>& c)
            : latest(l), count(c) {}
        void run(std::function<void(const prism::InputEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            auto geo = latest;
            REQUIRE_FALSE(geo->geometry.empty());
            auto [id, rect] = geo->geometry[0];
            // Click at the right edge of the track → value near 1.0
            cb(prism::MouseButton{{rect.x + 190, rect.y + 15}, 1, true});

            auto before = count.load(std::memory_order_acquire);
            count.wait(before, std::memory_order_acquire);

            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot> s) override {
            latest = std::move(s);
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    SliderClickModel model;
    prism::model_app(
        prism::Backend{std::make_unique<SliderBackend>(latest_snap, snap_count)},
        prism::BackendConfig{.width = 800, .height = 600},
        model
    );

    CHECK(model.volume.get().value > 0.8);
}
