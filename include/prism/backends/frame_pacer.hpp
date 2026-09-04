#pragma once

#include <chrono>

namespace prism::backends {

// Pure present-cadence logic for SoftwareBackend's run loop, kept SDL-free so it
// can be unit-tested headlessly (tests/test_frame_pacer.cpp). Single-threaded:
// owned and driven by the render thread only.
class FramePacer {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    explicit FramePacer(std::chrono::nanoseconds frame_interval)
        : frame_interval_(frame_interval) {}

    // The zero deadline means the very first frame is due immediately.
    bool frame_due(time_point now) const { return now >= next_frame_; }

    // 0 when a frame is due; otherwise the remaining time rounded UP to whole ms —
    // SDL_WaitEventTimeout takes integer ms and 0 would busy-spin on a sub-ms remainder.
    std::chrono::milliseconds ms_until_next_frame(time_point now) const {
        if (frame_due(now)) return std::chrono::milliseconds{0};
        return std::chrono::ceil<std::chrono::milliseconds>(next_frame_ - now);
    }

    // Advance the deadline by one interval, keeping cadence phase. If the loop
    // stalled a full interval or more past the deadline, reset phase to
    // now + interval instead of burst-presenting the missed frames.
    void mark_presented(time_point now) {
        if (now >= next_frame_ + frame_interval_)
            next_frame_ = now + frame_interval_;
        else
            next_frame_ += frame_interval_;
    }

private:
    std::chrono::nanoseconds frame_interval_;
    time_point next_frame_{};
};

// target_fps > 0 wins; otherwise display_hz; 60 Hz fallback when neither is valid.
inline double resolve_frame_hz(float target_fps, float display_hz) {
    if (target_fps > 0) return target_fps;
    if (display_hz > 0) return display_hz;
    return 60.0;
}

inline std::chrono::nanoseconds frame_interval_from_hz(double hz) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(1.0 / hz));
}

} // namespace prism::backends
