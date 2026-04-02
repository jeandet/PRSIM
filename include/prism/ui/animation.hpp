#pragma once

#include <prism/core/types.hpp>
#include <prism/core/field.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <utility>
#include <variant>
#include <vector>

namespace prism::ui {
using namespace prism::core;


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

inline int lerp(int a, int b, EasedProgress t) {
    return a + static_cast<int>((b - a) * t.raw() + 0.5f);
}

inline double lerp(double a, double b, EasedProgress t) {
    return a + (b - a) * static_cast<double>(t.raw());
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

template <Lerpable T>
class Animation {
public:
    Animation() = default;

    Animation(AnimationClock& clock, Field<T>& field, T target, TimingConfig config)
        : clock_(&clock), field_(&field), from_(field.get()),
          to_(std::move(target)), config_(std::move(config)) {
        start_ = AnimationClock::clock::now();
        clock_id_ = clock_->add([this](AnimationClock::time_point now) {
            return tick(now);
        });
    }

    ~Animation() {
        if (clock_ && clock_id_)
            clock_->remove(clock_id_);
    }

    Animation(const Animation&) = delete;
    Animation& operator=(const Animation&) = delete;

    Animation(Animation&& o) noexcept
        : clock_(o.clock_), field_(o.field_), from_(std::move(o.from_)),
          to_(std::move(o.to_)), start_(o.start_), config_(std::move(o.config_)),
          clock_id_(o.clock_id_) {
        o.clock_ = nullptr;
        o.clock_id_ = 0;
        if (clock_ && clock_id_) {
            clock_->remove(clock_id_);
            clock_id_ = clock_->add([this](AnimationClock::time_point now) {
                return tick(now);
            });
        }
    }

    Animation& operator=(Animation&& o) noexcept {
        if (this != &o) {
            if (clock_ && clock_id_)
                clock_->remove(clock_id_);
            // Remove source's registration (it captures source's this)
            if (o.clock_ && o.clock_id_)
                o.clock_->remove(o.clock_id_);
            clock_ = o.clock_;
            field_ = o.field_;
            from_ = std::move(o.from_);
            to_ = std::move(o.to_);
            start_ = o.start_;
            config_ = std::move(o.config_);
            clock_id_ = 0;
            o.clock_ = nullptr;
            o.clock_id_ = 0;
            if (clock_) {
                clock_id_ = clock_->add([this](AnimationClock::time_point now) {
                    return tick(now);
                });
            }
        }
        return *this;
    }

private:
    AnimationClock* clock_ = nullptr;
    Field<T>* field_ = nullptr;
    T from_{};
    T to_{};
    AnimationClock::time_point start_{};
    TimingConfig config_{AnimationConfig{}};
    uint64_t clock_id_ = 0;

    bool tick(AnimationClock::time_point now) {
        auto elapsed = now - start_;
        return std::visit([&](auto& cfg) { return tick_with(cfg, elapsed); }, config_);
    }

    bool tick_with(const AnimationConfig& cfg, std::chrono::nanoseconds elapsed) {
        float t = std::chrono::duration<float>(elapsed).count()
                / std::chrono::duration<float>(cfg.duration).count();
        if (t >= 1.f) {
            field_->set(to_);
            return false;
        }
        auto eased = cfg.easing(Progress{std::clamp(t, 0.f, 1.f)});
        field_->set(lerp(from_, to_, eased));
        return true;
    }

    bool tick_with(const SpringConfig& cfg, std::chrono::nanoseconds elapsed) {
        auto [progress, settled] = cfg.spring.evaluate(elapsed);
        auto eased = EasedProgress{progress.raw()};
        if (settled) {
            field_->set(to_);
            return false;
        }
        field_->set(lerp(from_, to_, eased));
        return true;
    }
};

template <Lerpable T>
[[nodiscard]] Animation<T> animate(AnimationClock& clock, Field<T>& field,
                                    T target, TimingConfig config) {
    return Animation<T>(clock, field, std::move(target), std::move(config));
}

template <Lerpable T>
class TransitionGuard {
public:
    TransitionGuard() = default;

    TransitionGuard(AnimationClock& clock, Field<T>& field, TimingConfig config)
        : clock_(&clock), field_(&field), config_(std::move(config)),
          current_animated_(field.get()), target_(field.get()) {
        subscription_ = field_->on_change().connect([this](const T& new_val) {
            on_field_changed(new_val);
        });
    }

    ~TransitionGuard() {
        subscription_.disconnect();
        if (clock_ && clock_id_)
            clock_->remove(clock_id_);
    }

    TransitionGuard(const TransitionGuard&) = delete;
    TransitionGuard& operator=(const TransitionGuard&) = delete;

    TransitionGuard(TransitionGuard&& o) noexcept
        : clock_(o.clock_), field_(o.field_), config_(std::move(o.config_)),
          clock_id_(o.clock_id_), subscription_(std::move(o.subscription_)),
          current_animated_(std::move(o.current_animated_)),
          target_(std::move(o.target_)), from_(std::move(o.from_)),
          start_(o.start_), animating_(o.animating_) {
        o.clock_ = nullptr;
        o.clock_id_ = 0;
        if (clock_ && clock_id_) {
            clock_->remove(clock_id_);
            clock_id_ = clock_->add([this](AnimationClock::time_point now) {
                return tick(now);
            });
        }
    }

    TransitionGuard& operator=(TransitionGuard&& o) noexcept {
        if (this != &o) {
            subscription_.disconnect();
            if (clock_ && clock_id_)
                clock_->remove(clock_id_);
            // Remove source's registration (it captures source's this)
            if (o.clock_ && o.clock_id_)
                o.clock_->remove(o.clock_id_);
            clock_ = o.clock_;
            field_ = o.field_;
            config_ = std::move(o.config_);
            current_animated_ = std::move(o.current_animated_);
            target_ = std::move(o.target_);
            from_ = std::move(o.from_);
            start_ = o.start_;
            animating_ = o.animating_;
            subscription_ = std::move(o.subscription_);
            clock_id_ = 0;
            o.clock_ = nullptr;
            o.clock_id_ = 0;
            if (clock_) {
                clock_id_ = clock_->add([this](AnimationClock::time_point now) {
                    return tick(now);
                });
            }
        }
        return *this;
    }

private:
    AnimationClock* clock_ = nullptr;
    Field<T>* field_ = nullptr;
    TimingConfig config_{AnimationConfig{}};
    uint64_t clock_id_ = 0;
    Connection subscription_;
    T current_animated_{};
    T target_{};
    T from_{};
    AnimationClock::time_point start_{};
    bool animating_ = false;

    void on_field_changed(const T& new_val) {
        if (animating_) return;

        from_ = current_animated_;
        target_ = new_val;
        start_ = AnimationClock::clock::now();

        if (!clock_id_) {
            clock_id_ = clock_->add([this](AnimationClock::time_point now) {
                return tick(now);
            });
        }
    }

    bool tick(AnimationClock::time_point now) {
        auto elapsed = now - start_;
        bool keep_going = std::visit([&](auto& cfg) {
            return tick_with(cfg, elapsed);
        }, config_);
        if (!keep_going)
            clock_id_ = 0;
        return keep_going;
    }

    bool tick_with(const AnimationConfig& cfg, std::chrono::nanoseconds elapsed) {
        float t = std::chrono::duration<float>(elapsed).count()
                / std::chrono::duration<float>(cfg.duration).count();
        // Small tolerance for wall-clock drift between now() and tick time
        if (t >= 0.999f) {
            animating_ = true;
            field_->set(target_);
            current_animated_ = target_;
            animating_ = false;
            return false;
        }
        auto eased = cfg.easing(Progress{std::clamp(t, 0.f, 1.f)});
        current_animated_ = lerp(from_, target_, eased);
        animating_ = true;
        field_->set(current_animated_);
        animating_ = false;
        return true;
    }

    bool tick_with(const SpringConfig& cfg, std::chrono::nanoseconds elapsed) {
        auto [progress, settled] = cfg.spring.evaluate(elapsed);
        auto eased = EasedProgress{progress.raw()};
        if (settled) {
            animating_ = true;
            field_->set(target_);
            current_animated_ = target_;
            animating_ = false;
            return false;
        }
        current_animated_ = lerp(from_, target_, eased);
        animating_ = true;
        field_->set(current_animated_);
        animating_ = false;
        return true;
    }
};

template <Lerpable T>
[[nodiscard]] TransitionGuard<T> transition(AnimationClock& clock, Field<T>& field,
                                             TimingConfig config) {
    return TransitionGuard<T>(clock, field, std::move(config));
}

} // namespace prism::ui
