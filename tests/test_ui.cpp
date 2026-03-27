#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/ui.hpp>
#include <prism/core/null_backend.hpp>

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
