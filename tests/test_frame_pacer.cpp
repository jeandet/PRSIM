#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/frame_pacer.hpp>

using namespace prism::backends;
using namespace std::chrono;

namespace {
constexpr auto kInterval = nanoseconds(16'000'000); // 16 ms, clean math
const steady_clock::time_point t0 = steady_clock::time_point{} + seconds(1000);
}

TEST_CASE("FramePacer first frame is due immediately")
{
    FramePacer p(kInterval);
    CHECK(p.frame_due(t0));
    CHECK(p.ms_until_next_frame(t0) == milliseconds(0));
}

TEST_CASE("FramePacer cadence after a present")
{
    FramePacer p(kInterval);
    p.mark_presented(t0);

    CHECK_FALSE(p.frame_due(t0 + kInterval / 2));
    CHECK(p.ms_until_next_frame(t0 + kInterval / 2) == kInterval / 2);
    CHECK(p.frame_due(t0 + kInterval));
    CHECK(p.ms_until_next_frame(t0 + kInterval) == milliseconds(0));
}

TEST_CASE("FramePacer keeps cadence phase across on-time presents")
{
    FramePacer p(kInterval);
    p.mark_presented(t0);
    p.mark_presented(t0 + kInterval); // exactly on the deadline

    // Phase kept: next deadline is t0 + 2 intervals, not now + interval.
    CHECK_FALSE(p.frame_due(t0 + kInterval + milliseconds(1)));
    CHECK(p.ms_until_next_frame(t0 + kInterval + milliseconds(1)) == kInterval - milliseconds(1));
    CHECK(p.frame_due(t0 + 2 * kInterval));
}

TEST_CASE("FramePacer clamps after a stall instead of burst-catching-up")
{
    FramePacer p(kInterval);
    p.mark_presented(t0);

    const auto stalled = t0 + 10 * kInterval; // missed 9 frames
    CHECK(p.frame_due(stalled));
    p.mark_presented(stalled);

    // Phase reset to now + interval: no immediate second present.
    CHECK_FALSE(p.frame_due(stalled + kInterval / 2));
    CHECK(p.frame_due(stalled + kInterval));
}

TEST_CASE("FramePacer rounds sub-millisecond remainders up to 1 ms")
{
    FramePacer p(kInterval);
    p.mark_presented(t0);
    // 66.667 µs before the deadline: SDL_WaitEventTimeout takes integer ms,
    // 0 would busy-spin, so it must round up.
    CHECK(p.ms_until_next_frame(t0 + kInterval - microseconds(67)) == milliseconds(1));
}

TEST_CASE("resolve_frame_hz precedence and fallback")
{
    CHECK(resolve_frame_hz(144.f, 165.f) == 144.0); // explicit target wins
    CHECK(resolve_frame_hz(0.f, 165.f) == 165.0);   // display rate next
    CHECK(resolve_frame_hz(0.f, 0.f) == 60.0);      // both invalid: 60 Hz
    CHECK(resolve_frame_hz(0.f, -1.f) == 60.0);
}

TEST_CASE("frame_interval_from_hz")
{
    CHECK(frame_interval_from_hz(60.0) == duration_cast<nanoseconds>(duration<double>(1.0 / 60.0)));
}
