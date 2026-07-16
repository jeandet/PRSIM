#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

TEST_CASE("PRISM_DEBUG_TOOLS_ENABLED is defined by default (non-release build)") {
#ifdef PRISM_DEBUG_TOOLS_ENABLED
    CHECK(true);
#else
    FAIL("expected PRISM_DEBUG_TOOLS_ENABLED to be defined in a non-release buildtype");
#endif
}

#if defined(PRISM_DEBUG_TOOLS_ENABLED) && __cpp_impl_reflection
#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>

namespace {
struct ReflectedOnlyModel {
    prism::core::Field<int> volume{0};
};
}

TEST_CASE("reflection-only model captures the real field name as debug_name") {
    ReflectedOnlyModel model;
    prism::app::WidgetTree tree(model);
    REQUIRE(!tree.root().children.empty());
    CHECK(tree.root().children[0].debug_name == "volume");
}
#endif
