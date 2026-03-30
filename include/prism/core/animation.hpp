#pragma once

#include <prism/core/types.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <utility>
#include <variant>
#include <vector>

namespace prism {

namespace ease {

inline EasedProgress linear(Progress t) {
    return EasedProgress{t.raw()};
}

inline EasedProgress in_quad(Progress t) {
    float v = t.raw();
    return EasedProgress{v * v};
}

inline EasedProgress out_quad(Progress t) {
    float v = t.raw();
    return EasedProgress{v * (2.f - v)};
}

inline EasedProgress in_out_quad(Progress t) {
    float v = t.raw();
    return v < 0.5f
        ? EasedProgress{2.f * v * v}
        : EasedProgress{-1.f + (4.f - 2.f * v) * v};
}

inline EasedProgress in_cubic(Progress t) {
    float v = t.raw();
    return EasedProgress{v * v * v};
}

inline EasedProgress out_cubic(Progress t) {
    float v = t.raw() - 1.f;
    return EasedProgress{v * v * v + 1.f};
}

inline EasedProgress in_out_cubic(Progress t) {
    float v = t.raw();
    return v < 0.5f
        ? EasedProgress{4.f * v * v * v}
        : EasedProgress{(v - 1.f) * (2.f * v - 2.f) * (2.f * v - 2.f) + 1.f};
}

} // namespace ease

struct Spring {
    float stiffness = 100.f;
    float damping = 10.f;
    float mass = 1.f;

    std::pair<Progress, bool> evaluate(std::chrono::nanoseconds elapsed) const {
        float t = std::chrono::duration<float>(elapsed).count();
        float omega = std::sqrt(stiffness / mass);
        float zeta = damping / (2.f * std::sqrt(stiffness * mass));

        float x;
        if (zeta < 1.f) {
            float omega_d = omega * std::sqrt(1.f - zeta * zeta);
            float env = std::exp(-zeta * omega * t);
            x = -env * (std::cos(omega_d * t) + (zeta * omega / omega_d) * std::sin(omega_d * t));
        } else if (zeta > 1.f) {
            float s1 = -omega * (zeta + std::sqrt(zeta * zeta - 1.f));
            float s2 = -omega * (zeta - std::sqrt(zeta * zeta - 1.f));
            float c2 = (s1 + omega * zeta) / (s1 - s2);
            float c1 = -1.f - c2;
            x = c1 * std::exp(s1 * t) + c2 * std::exp(s2 * t);
        } else {
            float env = std::exp(-omega * t);
            x = -(1.f + omega * t) * env;
        }

        float progress = std::clamp(1.f + x, 0.f, 1.f);
        bool settled = std::abs(x) < 0.001f;
        return {Progress{progress}, settled};
    }
};

struct AnimationConfig {
    std::chrono::milliseconds duration{300};
    std::function<EasedProgress(Progress)> easing = ease::out_quad;
};

struct SpringConfig {
    Spring spring{};
    float settle_threshold = 0.001f;
};

using TimingConfig = std::variant<AnimationConfig, SpringConfig>;

class AnimationClock {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    uint64_t add(std::function<bool(time_point)> tick_fn) {
        auto id = next_id_++;
        animations_.push_back({id, std::move(tick_fn)});
        return id;
    }

    void remove(uint64_t id) {
        std::erase_if(animations_, [id](auto& e) { return e.first == id; });
    }

    void tick(time_point now) {
        auto snapshot = animations_;
        for (auto& [id, fn] : snapshot) {
            if (!fn(now)) {
                remove(id);
            }
        }
    }

    [[nodiscard]] bool active() const { return !animations_.empty(); }

private:
    uint64_t next_id_ = 1;
    std::vector<std::pair<uint64_t, std::function<bool(time_point)>>> animations_;
};

// lerp overloads
inline float lerp(float a, float b, EasedProgress t) {
    return a + (b - a) * t.raw();
}

template <typename Tag>
Scalar<Tag> lerp(Scalar<Tag> a, Scalar<Tag> b, EasedProgress t) {
    return Scalar<Tag>{a.raw() + (b.raw() - a.raw()) * t.raw()};
}

// Lerpable concept
template <typename T>
concept Lerpable = requires(T a, T b, EasedProgress t) {
    { lerp(a, b, t) } -> std::convertible_to<T>;
};

} // namespace prism
