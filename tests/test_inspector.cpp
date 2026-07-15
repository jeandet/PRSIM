#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/widgets/inspector.hpp>
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
};

#if __cpp_impl_reflection
TEST_CASE("Inspector seeds mirror from Shared<T> initial value") {
    prism::Shared<DeviceState> source{DeviceState{3.3f, 2, true}};
    prism::inspector::Inspector<DeviceState> inspector(source);

    CHECK(std::get<0>(inspector.mirror().slots).value.get() == doctest::Approx(3.3f));
    CHECK(std::get<1>(inspector.mirror().slots).value.get() == 2);
    CHECK(std::get<2>(inspector.mirror().slots).value.get() == true);
}

TEST_CASE("Inspector syncs mirror when Shared<T> changes and drains") {
    prism::Shared<DeviceState> source{DeviceState{0.f, 0, false}};
    prism::inspector::Inspector<DeviceState> inspector(source);

    source.set(DeviceState{9.f, 5, true});
    CHECK(std::get<0>(inspector.mirror().slots).value.get() == doctest::Approx(0.f)); // not yet drained

    source.drain_notifications();
    CHECK(std::get<0>(inspector.mirror().slots).value.get() == doctest::Approx(9.f));
    CHECK(std::get<1>(inspector.mirror().slots).value.get() == 5);
    CHECK(std::get<2>(inspector.mirror().slots).value.get() == true);
}

TEST_CASE("Inspector is a WidgetTree component") {
    prism::Shared<DeviceState> source{DeviceState{1.f, 1, false}};
    prism::inspector::Inspector<DeviceState> inspector(source);
    prism::WidgetTree tree(inspector);
    CHECK(tree.leaf_count() == 6); // 3 members x (name + value)
}

TEST_CASE("Inspector pushes local edits back to Shared<T>, preserving other fields") {
    prism::Shared<DeviceState> source{DeviceState{3.3f, 2, true}};
    prism::inspector::Inspector<DeviceState> inspector(source);

    std::get<1>(inspector.mirror().slots).value.set(99);

    DeviceState updated = source.get();
    CHECK(updated.voltage == doctest::Approx(3.3f));
    CHECK(updated.mode == 99);
    CHECK(updated.enabled == true);
}

TEST_CASE("WidgetTree drains Inspector's Shared<T> via tree.drain_shared()") {
    prism::Shared<DeviceState> source{DeviceState{0.f, 0, false}};
    prism::inspector::Inspector<DeviceState> inspector(source);
    prism::WidgetTree tree(inspector);

    source.set(DeviceState{7.f, 3, true});
    CHECK(std::get<0>(inspector.mirror().slots).value.get() == doctest::Approx(0.f)); // not yet drained

    tree.drain_shared();
    CHECK(std::get<0>(inspector.mirror().slots).value.get() == doctest::Approx(7.f));
    CHECK(std::get<1>(inspector.mirror().slots).value.get() == 3);
}

TEST_CASE("Local edit followed by remote drain settles without oscillating") {
    prism::Shared<DeviceState> source{DeviceState{0.f, 0, false}};
    prism::inspector::Inspector<DeviceState> inspector(source);
    prism::WidgetTree tree(inspector);
    tree.drain_shared(); // clear any initial pending state

    // Local edit: pushes {0, 5, false} to source, which also sets pending_ (echo).
    std::get<1>(inspector.mirror().slots).value.set(5);
    CHECK(source.get().mode == 5);

    // First drain consumes the echo — values are already correct, no observable change.
    tree.drain_shared();
    CHECK(std::get<1>(inspector.mirror().slots).value.get() == 5);

    // A second drain must be a true no-op (bounded, not oscillating forever).
    int calls = 0;
    auto conn = source.on_change().connect([&](const DeviceState&) { ++calls; });
    tree.drain_shared();
    CHECK(calls == 0);
}
#endif // __cpp_impl_reflection
