#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/tree.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

#if __cpp_impl_reflection

namespace {
struct BinaryNode {
    int value = 0;
    BinaryNode* A = nullptr;
    BinaryNode* B = nullptr;
};
}

TEST_CASE("wrap_struct_tree exposes a root and descends into non-null pointer members") {
    BinaryNode leaf_a;
    leaf_a.value = 10;
    BinaryNode root;
    root.value = 1;
    root.A = &leaf_a;
    root.B = nullptr;

    auto src = prism::wrap_struct_tree(root);
    REQUIRE(src.root_count() == 1);
    auto root_id = src.root_at(0);

    CHECK(src.has_children(root_id) == true);
    REQUIRE(src.child_count(root_id) == 1); // only A -- B is null, not shown at all
    auto a_id = src.child_at(root_id, 0);
    CHECK(a_id != root_id); // distinct nodes get distinct ids
    CHECK(src.label(a_id) == "A"); // labeled by the member name that referenced it
    CHECK(src.has_children(a_id) == false); // leaf_a's own A/B are both null
}

TEST_CASE("wrap_struct_tree's root is labeled by its own type name") {
    BinaryNode root;
    auto src = prism::wrap_struct_tree(root);
    CHECK(src.label(src.root_at(0)) == "BinaryNode");
}

TEST_CASE("wrap_struct_tree collects primitive members as attributes, not child rows") {
    BinaryNode root;
    root.value = 42;
    auto src = prism::wrap_struct_tree(root);
    auto root_id = src.root_at(0);

    REQUIRE(src.attributes);
    auto attrs = src.attributes(root_id);
    bool found = false;
    for (auto& [k, v] : attrs)
        if (k == "value" && v == "42") found = true;
    CHECK(found);
    // `value` must not appear as a child row
    CHECK(src.child_count(root_id) == 0); // both A and B are null here
}

namespace {
struct Inner {
    int x = 0;
};
struct WithNested {
    int value = 0;
    Inner inner;
};
}

TEST_CASE("wrap_struct_tree always descends into a directly-nested class member") {
    WithNested root;
    root.inner.x = 7;

    auto src = prism::wrap_struct_tree(root);
    auto root_id = src.root_at(0);
    REQUIRE(src.child_count(root_id) == 1); // nested class member is always a child slot
    auto inner_id = src.child_at(root_id, 0);
    CHECK(src.label(inner_id) == "inner"); // labeled by the member name

    auto attrs = src.attributes(inner_id);
    bool found = false;
    for (auto& [k, v] : attrs)
        if (k == "x" && v == "7") found = true;
    CHECK(found);
}

namespace {
struct WithVector {
    int value = 0;
    std::vector<int> kids;
};
}

TEST_CASE("wrap_struct_tree skips std::vector members entirely (documented v1 non-goal)") {
    WithVector root;
    root.value = 5;
    root.kids.push_back(1);
    root.kids.push_back(2);

    auto src = prism::wrap_struct_tree(root);
    auto root_id = src.root_at(0);
    // Not a child row...
    CHECK(src.child_count(root_id) == 0);
    // ...and not an attribute either.
    auto attrs = src.attributes(root_id);
    for (auto& [k, v] : attrs) {
        (void)v;
        CHECK(k != "kids");
    }
}

namespace {
struct FirstMember {
    int x = 0;
};
// NestedFirst's first member is a class type, so &NestedFirst == &NestedFirst::inner for any
// standard-layout instance -- this is the regression case for address-derived TreeNodeId, where
// the child and parent would collide on the very same cache key.
struct NestedFirst {
    FirstMember inner;
    int value = 0;
};
}

TEST_CASE("wrap_struct_tree keeps parent and child distinct when a nested class member is first "
          "(same address as the parent)") {
    NestedFirst root;
    root.inner.x = 3;
    root.value = 9;

    auto src = prism::wrap_struct_tree(root);
    auto root_id = src.root_at(0);
    CHECK(src.label(root_id) == "NestedFirst"); // not overwritten/shadowed by the child's entry
    REQUIRE(src.child_count(root_id) == 1);

    auto attrs = src.attributes(root_id);
    bool found_value = false;
    for (auto& [k, v] : attrs)
        if (k == "value" && v == "9") found_value = true;
    CHECK(found_value); // root's own attribute must survive, not just the child's
}

namespace {
enum class Color { Red, Green };
struct WithEnum {
    Color color = Color::Green;
};
}

TEST_CASE("wrap_struct_tree collects enum members as attributes with their underlying value") {
    WithEnum root;
    auto src = prism::wrap_struct_tree(root);
    auto root_id = src.root_at(0);

    auto attrs = src.attributes(root_id);
    bool found = false;
    for (auto& [k, v] : attrs)
        if (k == "color" && v == "1") found = true; // Color::Green's underlying value
    CHECK(found);
}

#endif // __cpp_impl_reflection
