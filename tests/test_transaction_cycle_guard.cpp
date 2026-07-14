// Isolated from test_transaction.cpp: this file forces NDEBUG before including
// transaction.hpp to simulate a `buildtype=release` build (b_ndebug=if-release
// in meson.build), where assert() compiles away. This is the only way to
// exercise release-build semantics deterministically inside a debug test
// binary; see the bug this guards against below.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#define NDEBUG
#include <prism/core/transaction.hpp>
#undef NDEBUG

#include <prism/core/field.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

TEST_CASE("transaction flush terminates on a non-converging cascade even under NDEBUG") {
    // Two fields that escalate each other forever (a -> b -> a -> ..., each
    // wave with a strictly larger value) never satisfy the flush loop's
    // `!queue.empty()` exit condition on their own. The wave-count cap is the
    // only thing that can stop this. Previously the cap's increment lived
    // inside assert(), which is elided under NDEBUG, so the loop never
    // terminated (verified experimentally: hung until killed by a 5s
    // `timeout` in manual testing). If this regresses, meson's default
    // per-test timeout will catch it as a TIMEOUT rather than hanging CI
    // forever.
    prism::Field<int> a{0};
    prism::Field<int> b{0};

    auto ca = a.on_change().connect([&](const int& v) { b.set(v + 1); });
    auto cb = b.on_change().connect([&](const int& v) { a.set(v + 1); });

    prism::transaction([&] { a.set(1); });

    // Reaching this line at all proves the loop terminated instead of
    // hanging. The cap (64 waves) means the cascade gets cut short rather
    // than running forever, so neither field converges to a final steady
    // value — that's the documented, deliberate trade-off of the safety net.
    CHECK(a.get() > 0);
    CHECK(b.get() > 0);
}
