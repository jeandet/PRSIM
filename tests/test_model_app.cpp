#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/model_app.hpp>
#include <prism/core/null_backend.hpp>
#include <prism/core/field.hpp>
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
