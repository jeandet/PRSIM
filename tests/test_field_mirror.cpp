#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/widgets/field_mirror.hpp>
#include <string>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism::inspector {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot; using namespace inspector;
}

struct DeviceState {
    float voltage;
    int mode;
    bool enabled;
    std::string name;
};

#if __cpp_impl_reflection
TEST_CASE("FieldMirror seeds leaf values and labels from sync_from") {
    prism::inspector::FieldMirror<DeviceState> mirror;
    DeviceState d{3.3f, 2, true, "dev0"};
    mirror.sync_from(d);

    CHECK(std::get<0>(mirror.slots).value.get() == doctest::Approx(3.3f));
    CHECK(std::get<0>(mirror.slots).name.get().value == "voltage");
    CHECK(std::get<1>(mirror.slots).value.get() == 2);
    CHECK(std::get<1>(mirror.slots).name.get().value == "mode");
    CHECK(std::get<2>(mirror.slots).value.get() == true);
    CHECK(std::get<3>(mirror.slots).value.get() == "dev0");
    CHECK(std::get<3>(mirror.slots).name.get().value == "name");
}

TEST_CASE("FieldMirror::build reconstructs T from current slot values") {
    prism::inspector::FieldMirror<DeviceState> mirror;
    DeviceState d{3.3f, 2, true, "dev0"};
    mirror.sync_from(d);

    std::get<1>(mirror.slots).value.set(99);
    DeviceState rebuilt = mirror.build();

    CHECK(rebuilt.voltage == doctest::Approx(3.3f));
    CHECK(rebuilt.mode == 99);
    CHECK(rebuilt.enabled == true);
    CHECK(rebuilt.name == "dev0");
}

TEST_CASE("FieldMirror::for_each_leaf visits every leaf exactly once") {
    prism::inspector::FieldMirror<DeviceState> mirror;
    mirror.sync_from(DeviceState{1.f, 1, false, "x"});

    int count = 0;
    mirror.for_each_leaf([&](auto&) { ++count; });
    CHECK(count == 4);
}

struct Inner {
    float scale;
    int count;
};

struct NestedDeviceState {
    float voltage;
    Inner inner;
};

TEST_CASE("FieldMirror recurses into nested plain structs") {
    prism::inspector::FieldMirror<NestedDeviceState> mirror;
    NestedDeviceState d{9.f, {1.5f, 7}};
    mirror.sync_from(d);

    CHECK(std::get<0>(mirror.slots).value.get() == doctest::Approx(9.f));
    auto& inner_mirror = std::get<1>(mirror.slots);
    CHECK(std::get<0>(inner_mirror.slots).value.get() == doctest::Approx(1.5f));
    CHECK(std::get<1>(inner_mirror.slots).value.get() == 7);

    std::get<1>(inner_mirror.slots).value.set(42);
    NestedDeviceState rebuilt = mirror.build();
    CHECK(rebuilt.voltage == doctest::Approx(9.f));
    CHECK(rebuilt.inner.scale == doctest::Approx(1.5f));
    CHECK(rebuilt.inner.count == 42);
}

TEST_CASE("FieldMirror::for_each_leaf visits nested leaves too") {
    prism::inspector::FieldMirror<NestedDeviceState> mirror;
    mirror.sync_from(NestedDeviceState{1.f, {2.f, 3}});
    int count = 0;
    mirror.for_each_leaf([&](auto&) { ++count; });
    CHECK(count == 3); // voltage, inner.scale, inner.count
}

TEST_CASE("FieldMirror is a WidgetTree component with one leaf per member") {
    prism::inspector::FieldMirror<DeviceState> mirror;
    mirror.sync_from(DeviceState{1.f, 1, false, "x"});
    prism::WidgetTree tree(mirror);
    // 4 leaves, each rendered as a LeafSlot (name label + value) = 8 rendered leaf widgets
    CHECK(tree.leaf_count() == 8);
}

TEST_CASE("FieldMirror WidgetTree recurses into nested struct") {
    prism::inspector::FieldMirror<NestedDeviceState> mirror;
    mirror.sync_from(NestedDeviceState{1.f, {2.f, 3}});
    prism::WidgetTree tree(mirror);
    // voltage (2) + inner.scale (2) + inner.count (2) = 6
    CHECK(tree.leaf_count() == 6);
}
#endif // __cpp_impl_reflection
