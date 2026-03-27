#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/ui.hpp>
#include <prism/core/null_backend.hpp>
#include <prism/core/test_backend.hpp>

#include <string>

struct TestState {
    int count = 42;
    std::string name = "hello";
};

TEST_CASE("app<State> runs view at least once") {
    int call_count = 0;

    prism::app<TestState>(
        prism::Backend{std::make_unique<prism::NullBackend>()},
        {},
        TestState{},
        [&](prism::Ui<TestState>& /*ui*/) {
            ++call_count;
        }
    );

    CHECK(call_count >= 1);
}

TEST_CASE("ui-> gives read-only state access") {
    prism::app<TestState>(
        prism::Backend{std::make_unique<prism::NullBackend>()},
        {},
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

    prism::app<TestState>(
        prism::Backend{std::make_unique<prism::NullBackend>()},
        {},
        TestState{},
        [&](prism::Ui<TestState>& ui) {
            ui.frame().filled_rect({10, 10, 50, 50}, prism::Color::rgba(255, 0, 0));
            drew = true;
        }
    );

    CHECK(drew);
}

TEST_CASE("app<State> default-constructs state when not provided") {
    prism::app<TestState>(
        prism::Backend{std::make_unique<prism::NullBackend>()},
        {},
        [](prism::Ui<TestState>& ui) {
            CHECK(ui->count == 42);
            CHECK(ui->name == "hello");
        }
    );
}

TEST_CASE("convenience overload with title creates software-like flow") {
    int called = 0;
    prism::app<TestState>(
        prism::Backend{std::make_unique<prism::NullBackend>()},
        {},
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
        prism::MouseButton{{50, 50}, 1, true},
    };

    int last_seen_clicks = -1;
    prism::app<ClickState>(
        prism::Backend{std::make_unique<prism::TestBackend>(events)},
        {},
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
    prism::app<MoveState>(
        prism::Backend{std::make_unique<prism::TestBackend>(events)},
        {},
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
        prism::MouseButton{{0, 0}, 1, true},
        prism::KeyPress{1, 0},
        prism::MouseMove{{10, 20}},
    };

    int last_n = -1;
    prism::app<CountState>(
        prism::Backend{std::make_unique<prism::TestBackend>(events)},
        {},
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
    prism::app<TestState>(
        prism::Backend{std::make_unique<prism::NullBackend>()},
        {},
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
    prism::app<ResizeState>(
        prism::Backend{std::make_unique<prism::TestBackend>(events)},
        {},
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
