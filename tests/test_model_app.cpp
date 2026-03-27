#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/model_app.hpp>
#include <prism/core/null_backend.hpp>
#include <prism/core/field.hpp>
#include <prism/core/hit_test.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <string>

struct TestModel {
    prism::Field<int> count{"Count", 42};
    prism::Field<std::string> name{"Name", "hello"};
};

struct NestedTestModel {
    TestModel inner;
    prism::Field<bool> flag{"Flag", false};
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
    prism::Field<bool> toggle{"Toggle", false};
};

TEST_CASE("model_app routes MouseButton to Field<bool> toggle") {
    std::vector<std::shared_ptr<const prism::SceneSnapshot>> snapshots;

    struct ClickBackend final : public prism::BackendBase {
        std::vector<std::shared_ptr<const prism::SceneSnapshot>>& snaps;
        explicit ClickBackend(std::vector<std::shared_ptr<const prism::SceneSnapshot>>& s)
            : snaps(s) {}
        void run(std::function<void(const prism::InputEvent&)> cb) override {
            // Wait for first snapshot to know geometry
            while (snaps.empty()) {}
            auto& geo = snaps.back()->geometry;
            REQUIRE_FALSE(geo.empty());
            auto [id, rect] = geo[0];
            auto center = rect.center();

            // Click in the widget
            cb(prism::MouseButton{center, 1, true});
            // Give app thread time to process
            while (snaps.size() < 2) {}

            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot> s) override {
            snaps.push_back(std::move(s));
        }
        void wake() override {}
        void quit() override {}
    };

    ClickTestModel model;
    CHECK(model.toggle.get() == false);

    prism::model_app(
        prism::Backend{std::make_unique<ClickBackend>(snapshots)},
        prism::BackendConfig{.width = 800, .height = 600},
        model
    );

    CHECK(model.toggle.get() == true);
    CHECK(snapshots.size() >= 2);
}
