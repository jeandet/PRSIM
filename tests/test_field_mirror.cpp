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

struct AnnotationProbe {
    [[=prism::inspector::skip]] int a;
    [[=prism::inspector::readonly]] int b;
    [[=prism::inspector::label<"Custom Name">]] int c;
    [[=prism::inspector::section<"Audio">]] int d;
    int e;
};

TEST_CASE("annotation helpers detect skip/readonly and extract label/section text") {
    static_assert(prism::inspector::has_annotation<^^AnnotationProbe::a, decltype(prism::inspector::skip)>());
    static_assert(!prism::inspector::has_annotation<^^AnnotationProbe::e, decltype(prism::inspector::skip)>());

    static_assert(prism::inspector::has_annotation<^^AnnotationProbe::b, decltype(prism::inspector::readonly)>());
    static_assert(!prism::inspector::has_annotation<^^AnnotationProbe::e, decltype(prism::inspector::readonly)>());

    static_assert(prism::inspector::extract_string_annotation<^^AnnotationProbe::c, prism::inspector::label_t>() == "Custom Name");
    static_assert(prism::inspector::extract_string_annotation<^^AnnotationProbe::e, prism::inspector::label_t>().empty());

    static_assert(prism::inspector::extract_string_annotation<^^AnnotationProbe::d, prism::inspector::section_t>() == "Audio");
    static_assert(prism::inspector::extract_string_annotation<^^AnnotationProbe::e, prism::inspector::section_t>().empty());

    CHECK(true); // presence of this TEST_CASE proves the file compiled with the static_asserts above
}

struct CoreDirectProbe {
    [[=prism::core::skip]] int a;
    [[=prism::core::label<"Direct">]] int b;
};

TEST_CASE("annotations are directly usable via prism::core, not only prism::inspector") {
    static_assert(prism::core::has_annotation<^^CoreDirectProbe::a, decltype(prism::core::skip)>());
    static_assert(prism::core::extract_string_annotation<^^CoreDirectProbe::b,
                                                           prism::core::label_t>() == "Direct");
    CHECK(true);
}

struct DeviceStateWithSkip {
    float voltage;
    [[=prism::inspector::skip]] int internal_version;
    bool enabled;
};

TEST_CASE("skip excludes a member from for_each_leaf but preserves it through build()") {
    prism::inspector::FieldMirror<DeviceStateWithSkip> mirror;
    DeviceStateWithSkip d{3.3f, 42, true};
    mirror.sync_from(d);

    // for_each_leaf must not visit the skipped member.
    int count = 0;
    mirror.for_each_leaf([&](auto&) { ++count; });
    CHECK(count == 2); // voltage, enabled -- not internal_version

    // Editing an unrelated field and rebuilding must not reset internal_version.
    std::get<0>(mirror.slots).value.set(9.9f);
    DeviceStateWithSkip rebuilt = mirror.build();
    CHECK(rebuilt.voltage == doctest::Approx(9.9f));
    CHECK(rebuilt.internal_version == 42); // preserved, not reset to 0
    CHECK(rebuilt.enabled == true);
}

TEST_CASE("skip removes the member's widgets from the generated tree") {
    prism::inspector::FieldMirror<DeviceStateWithSkip> mirror;
    mirror.sync_from(DeviceStateWithSkip{1.f, 7, false});
    prism::WidgetTree tree(mirror);
    // voltage (2: name+value) + enabled (2: name+value) = 4. internal_version: 0.
    CHECK(tree.leaf_count() == 4);
}

struct DeviceStateWithLabel {
    [[=prism::inspector::label<"Sample Rate (Hz)">]] int sample_rate;
    bool enabled;
};

TEST_CASE("label overrides the name caption instead of using identifier_of") {
    prism::inspector::FieldMirror<DeviceStateWithLabel> mirror;
    mirror.sync_from(DeviceStateWithLabel{44100, true});

    CHECK(std::get<0>(mirror.slots).name.get().value == "Sample Rate (Hz)");
    CHECK(std::get<1>(mirror.slots).name.get().value == "enabled"); // unannotated: falls back to identifier_of
}

struct DeviceStateWithSection {
    float voltage;
    [[=prism::inspector::section<"Audio">]] float volume;
    bool enabled;
};

TEST_CASE("section stores a header title at the annotated member's index") {
    prism::inspector::FieldMirror<DeviceStateWithSection> mirror;
    mirror.sync_from(DeviceStateWithSection{1.f, 2.f, true});

    CHECK(mirror.section_headers[0].get().value.empty());
    CHECK(mirror.section_headers[1].get().value == "Audio");
    CHECK(mirror.section_headers[2].get().value.empty());
}

TEST_CASE("section inserts one extra header widget into the generated tree") {
    prism::inspector::FieldMirror<DeviceStateWithSection> mirror;
    mirror.sync_from(DeviceStateWithSection{1.f, 2.f, true});
    prism::WidgetTree tree(mirror);
    // voltage(2) + [header(1) + volume(2)] + enabled(2) = 7
    CHECK(tree.leaf_count() == 7);
}

struct DeviceStateWithReadonly {
    [[=prism::inspector::readonly]] std::string device_id;
    float voltage;
};

TEST_CASE("readonly routes the member to LeafSlot<M, true>") {
    using Tup = prism::inspector::FieldMirrorTuple<DeviceStateWithReadonly>;
    static_assert(std::is_same_v<std::tuple_element_t<0, Tup>,
                                  prism::inspector::LeafSlot<std::string, true>>);
    static_assert(std::is_same_v<std::tuple_element_t<1, Tup>,
                                  prism::inspector::LeafSlot<float, false>>);

    // Data still flows through a readonly slot exactly like any other leaf --
    // "readonly" only changes how it renders, not whether it holds a value.
    prism::inspector::FieldMirror<DeviceStateWithReadonly> mirror;
    mirror.sync_from(DeviceStateWithReadonly{"dev-42", 3.3f});
    CHECK(std::get<0>(mirror.slots).value.get() == "dev-42");
}

TEST_CASE("readonly still produces the same leaf count as an editable member") {
    prism::inspector::FieldMirror<DeviceStateWithReadonly> mirror;
    mirror.sync_from(DeviceStateWithReadonly{"dev-42", 3.3f});
    prism::WidgetTree tree(mirror);
    // device_id (name+value=2) + voltage (name+value=2) = 4
    CHECK(tree.leaf_count() == 4);
}

struct DeviceStateReadonlyFocus {
    // bool, not std::string/float: Widget<bool>::focus_policy is tab_and_click by default
    // (delegate.hpp), so forcing it through node_readonly_leaf's focus_policy::none is the
    // only thing standing between "focusable" and "not" -- this is what makes the test below
    // discriminate. (std::string/float are Widget<T>::focus_policy == none even when NOT
    // readonly, so a readonly member of those types can't tell node_readonly_leaf and
    // node_leaf apart via focus_order.)
    [[=prism::inspector::readonly]] bool locked;
    bool armed;
};

TEST_CASE("readonly excludes the member from focus order, editable member stays focusable") {
    prism::inspector::FieldMirror<DeviceStateReadonlyFocus> mirror;
    mirror.sync_from(DeviceStateReadonlyFocus{true, true});
    prism::WidgetTree tree(mirror);
    // locked is readonly (node_readonly_leaf forces focus_policy::none) and its name
    // caption is a Label (also none). armed is a plain editable bool, which IS focusable
    // (Widget<bool>::focus_policy == tab_and_click). Only armed's value widget should be
    // focusable.
    CHECK(tree.focus_order().size() == 1);
}
#endif // __cpp_impl_reflection
