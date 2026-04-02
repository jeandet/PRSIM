#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/ui.hpp>
#include <prism/app/null_backend.hpp>
#include <prism/app/test_backend.hpp>
#include <prism/app/headless_window.hpp>

#include <string>

namespace {
prism::Rect R(float x, float y, float w, float h) {
    return {prism::Point{prism::X{x}, prism::Y{y}}, prism::Size{prism::Width{w}, prism::Height{h}}};
}
prism::Point P(float x, float y) { return {prism::X{x}, prism::Y{y}}; }
}

struct TestState {
    int count = 42;
    std::string name = "hello";
};

TEST_CASE("app<State> runs view at least once") {
    int call_count = 0;

    auto backend = prism::Backend{std::make_unique<prism::NullBackend>()};
    auto& window = backend.create_window({});
    prism::app<TestState>(
        backend, window,
        TestState{},
        [&](prism::Ui<TestState>& /*ui*/) {
            ++call_count;
        }
    );

    CHECK(call_count >= 1);
}

TEST_CASE("ui-> gives read-only state access") {
    auto backend = prism::Backend{std::make_unique<prism::NullBackend>()};
    auto& window = backend.create_window({});
    prism::app<TestState>(
        backend, window,
        TestState{.count = 7, .name = "world"},
        [](prism::Ui<TestState>& ui) {
            CHECK(ui->count == 7);
            CHECK(ui->name == "world");
            CHECK(ui.state().count == 7);
        }
    );
}

TEST_CASE("ui.frame() records draw commands") {
    bool drew = false;

    auto backend = prism::Backend{std::make_unique<prism::NullBackend>()};
    auto& window = backend.create_window({});
    prism::app<TestState>(
        backend, window,
        TestState{},
        [&](prism::Ui<TestState>& ui) {
            ui.frame().filled_rect(R(10, 10, 50, 50), prism::Color::rgba(255, 0, 0));
            drew = true;
        }
    );

    CHECK(drew);
}

TEST_CASE("app<State> default-constructs state when not provided") {
    auto backend = prism::Backend{std::make_unique<prism::NullBackend>()};
    auto& window = backend.create_window({});
    prism::app<TestState>(
        backend, window,
        TestState{},
        [](prism::Ui<TestState>& ui) {
            CHECK(ui->count == 42);
            CHECK(ui->name == "hello");
        }
    );
}

TEST_CASE("convenience overload with title creates software-like flow") {
    int called = 0;
    auto backend = prism::Backend{std::make_unique<prism::NullBackend>()};
    auto& window = backend.create_window({});
    prism::app<TestState>(
        backend, window,
        TestState{.count = 99},
        [&](prism::Ui<TestState>& ui) {
            CHECK(ui->count == 99);
            ++called;
        }
    );
    CHECK(called >= 1);
}

TEST_CASE("update callback mutates state on mouse click") {
    struct ClickState { int clicks = 0; };

    std::vector<prism::InputEvent> events = {
        prism::MouseButton{P(50, 50), 1, true},
    };

    int last_seen_clicks = -1;
    auto backend = prism::Backend{std::make_unique<prism::TestBackend>(events)};
    auto& window = backend.create_window({});
    prism::app<ClickState>(
        backend, window,
        ClickState{},
        [&](prism::Ui<ClickState>& ui) {
            last_seen_clicks = ui->clicks;
        },
        [](ClickState& s, const prism::InputEvent& ev) {
            if (std::holds_alternative<prism::MouseButton>(ev))
                ++s.clicks;
        }
    );

    CHECK(last_seen_clicks == 1);
}

TEST_CASE("update callback mutates state on key press") {
    struct MoveState { float x = 0; };

    std::vector<prism::InputEvent> events = {
        prism::KeyPress{123, 0},
        prism::KeyPress{123, 0},
    };

    float last_x = -1;
    auto backend = prism::Backend{std::make_unique<prism::TestBackend>(events)};
    auto& window = backend.create_window({});
    prism::app<MoveState>(
        backend, window,
        MoveState{},
        [&](prism::Ui<MoveState>& ui) {
            last_x = ui->x;
        },
        [](MoveState& s, const prism::InputEvent& ev) {
            if (std::holds_alternative<prism::KeyPress>(ev))
                s.x += 10.f;
        }
    );

    CHECK(last_x == 20.f);
}

TEST_CASE("multiple events accumulate state changes") {
    struct CountState { int n = 0; };

    std::vector<prism::InputEvent> events = {
        prism::MouseButton{P(0, 0), 1, true},
        prism::KeyPress{1, 0},
        prism::MouseMove{P(10, 20)},
    };

    int last_n = -1;
    auto backend = prism::Backend{std::make_unique<prism::TestBackend>(events)};
    auto& window = backend.create_window({});
    prism::app<CountState>(
        backend, window,
        CountState{},
        [&](prism::Ui<CountState>& ui) {
            last_n = ui->n;
        },
        [](CountState& s, const prism::InputEvent&) {
            ++s.n;
        }
    );

    CHECK(last_n == 3);
}

TEST_CASE("no update callback is safe (existing behavior)") {
    int called = 0;
    auto backend = prism::Backend{std::make_unique<prism::NullBackend>()};
    auto& window = backend.create_window({});
    prism::app<TestState>(
        backend, window,
        TestState{},
        [&](prism::Ui<TestState>& /*ui*/) {
            ++called;
        }
    );
    CHECK(called >= 1);
}

TEST_CASE("WindowResize is forwarded to update callback") {
    struct ResizeState { int last_w = 0; };

    std::vector<prism::InputEvent> events = {
        prism::WindowResize{1024, 768},
    };

    int last_w = -1;
    auto backend = prism::Backend{std::make_unique<prism::TestBackend>(events)};
    auto& window = backend.create_window({});
    prism::app<ResizeState>(
        backend, window,
        ResizeState{},
        [&](prism::Ui<ResizeState>& ui) {
            last_w = ui->last_w;
        },
        [](ResizeState& s, const prism::InputEvent& ev) {
            if (auto* r = std::get_if<prism::WindowResize>(&ev))
                s.last_w = r->width;
        }
    );

    CHECK(last_w == 1024);
}

TEST_CASE("ui.row() produces per-widget geometry in snapshot") {
    struct S {};
    std::shared_ptr<const prism::SceneSnapshot> captured_snap;

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

    auto backend = prism::Backend{std::make_unique<CapturingBackend>(captured_snap)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::app<S>(
        backend, window,
        S{},
        [](prism::Ui<S>& ui) {
            ui.row([&] {
                ui.frame().filled_rect(R(0, 0, 200, 100),
                    prism::Color::rgba(255, 0, 0));
                ui.spacer();
                ui.frame().filled_rect(R(0, 0, 100, 50),
                    prism::Color::rgba(0, 255, 0));
            });
        }
    );

    REQUIRE(captured_snap != nullptr);
    // Two leaf widgets (spacer has no draw commands, skipped)
    CHECK(captured_snap->geometry.size() == 2);
    // First widget at x=0, width=200
    CHECK(captured_snap->geometry[0].second.origin.x.raw() == 0);
    CHECK(captured_snap->geometry[0].second.extent.w.raw() == 200);
    // Second widget at x=700 (800-100), width=100
    CHECK(captured_snap->geometry[1].second.origin.x.raw() == doctest::Approx(700));
    CHECK(captured_snap->geometry[1].second.extent.w.raw() == 100);
}

TEST_CASE("ui.column() stacks children vertically") {
    struct S {};
    std::shared_ptr<const prism::SceneSnapshot> captured_snap;

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

    auto backend = prism::Backend{std::make_unique<CapturingBackend>(captured_snap)};
    auto& window = backend.create_window({.width = 400, .height = 300});
    prism::app<S>(
        backend, window,
        S{},
        [](prism::Ui<S>& ui) {
            ui.column([&] {
                ui.frame().filled_rect(R(0, 0, 100, 80),
                    prism::Color::rgba(255, 0, 0));
                ui.frame().filled_rect(R(0, 0, 100, 60),
                    prism::Color::rgba(0, 255, 0));
            });
        }
    );

    REQUIRE(captured_snap != nullptr);
    CHECK(captured_snap->geometry.size() == 2);
    CHECK(captured_snap->geometry[0].second.origin.y.raw() == 0);
    CHECK(captured_snap->geometry[0].second.extent.h.raw() == 80);
    CHECK(captured_snap->geometry[1].second.origin.y.raw() == 80);
    CHECK(captured_snap->geometry[1].second.extent.h.raw() == 60);
}

TEST_CASE("ui.frame() without layout works as before (backward compat)") {
    struct S {};
    std::shared_ptr<const prism::SceneSnapshot> captured_snap;

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

    auto backend = prism::Backend{std::make_unique<CapturingBackend>(captured_snap)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::app<S>(
        backend, window,
        S{},
        [](prism::Ui<S>& ui) {
            ui.frame().filled_rect(R(10, 10, 50, 50),
                prism::Color::rgba(255, 0, 0));
        }
    );

    REQUIRE(captured_snap != nullptr);
    // Single full-viewport entry, same as before
    CHECK(captured_snap->geometry.size() == 1);
    CHECK(captured_snap->geometry[0].second.extent.w.raw() == 800);
    CHECK(captured_snap->geometry[0].second.extent.h.raw() == 600);
}
