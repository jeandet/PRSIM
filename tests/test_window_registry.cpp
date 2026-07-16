// tests/test_window_registry.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/window_registry.hpp>
#include <prism/app/headless_window.hpp>
#include <prism/core/field.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
struct Counter {
    prism::Field<int> value{0};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); }
};
}

TEST_CASE("WindowRegistry adds an entry and routes a dispatch to it") {
    prism::HeadlessWindow win{1, {}};
    Counter model;
    prism::WindowRegistry registry;

    auto id = registry.add(win, model);
    CHECK(id == 1);

    auto* entry = registry.find(id);
    REQUIRE(entry != nullptr);
    CHECK(entry->window == &win);
    CHECK(entry->tree != nullptr);
}

TEST_CASE("WindowRegistry::find returns nullptr for an unknown id") {
    prism::WindowRegistry registry;
    CHECK(registry.find(999) == nullptr);
}

TEST_CASE("WindowRegistry routes two entries independently") {
    prism::HeadlessWindow win_a{1, {}};
    prism::HeadlessWindow win_b{2, {}};
    Counter model_a, model_b;
    prism::WindowRegistry registry;

    auto id_a = registry.add(win_a, model_a);
    auto id_b = registry.add(win_b, model_b);
    CHECK(id_a != id_b);

    registry.find(id_a)->tree->root().dirty = true;

    size_t dirty_count = 0;
    prism::WindowId dirty_id = 0;
    registry.for_each_dirty([&](prism::WindowId id, prism::WindowRegistry::Entry&) {
        ++dirty_count;
        dirty_id = id;
    });
    CHECK(dirty_count == 1);
    CHECK(dirty_id == id_a);
}

TEST_CASE("WindowRegistry::remove erases an entry") {
    prism::HeadlessWindow win{1, {}};
    Counter model;
    prism::WindowRegistry registry;
    auto id = registry.add(win, model);
    registry.remove(id);
    CHECK(registry.find(id) == nullptr);
}
