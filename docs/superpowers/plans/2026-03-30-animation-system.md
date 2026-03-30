# Animation System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a general-purpose animation engine to PRISM — easing functions, spring physics, `AnimationClock`, `Animation<T>`, and `TransitionGuard<T>`.

**Architecture:** External animation slots bind `Field<T>` to interpolation configs. A lightweight `AnimationClock` drives all active animations via a ~60Hz timer on the stdexec run_loop, self-stopping when idle. `Lerpable` concept makes any type with a `lerp` overload animatable.

**Tech Stack:** C++26, stdexec, doctest, Meson

**Spec:** `docs/superpowers/specs/2026-03-30-animation-system-design.md`

---

### Task 1: Strong Types — Progress and EasedProgress

**Files:**
- Modify: `include/prism/core/types.hpp:184` (after Rect)
- Modify: `tests/test_types.cpp`

- [ ] **Step 1: Write failing tests for Progress and EasedProgress**

Add to `tests/test_types.cpp`:

```cpp
TEST_CASE("Progress strong type") {
    auto p = Progress{0.5f};
    CHECK(p.raw() == doctest::Approx(0.5f));

    auto p0 = Progress{0.f};
    auto p1 = Progress{1.f};
    CHECK(p0 < p1);
    CHECK(p0 == Progress{0.f});
}

TEST_CASE("EasedProgress strong type") {
    auto e = EasedProgress{0.75f};
    CHECK(e.raw() == doctest::Approx(0.75f));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson test -C builddir test_types --print-errorlogs`
Expected: FAIL — `Progress` and `EasedProgress` not defined

- [ ] **Step 3: Add Progress and EasedProgress types**

In `include/prism/core/types.hpp`, after the `Rect` definition (line 184), add:

```cpp
// Animation progress types
struct ProgressTag {};
struct EasedProgressTag {};

using Progress = Scalar<ProgressTag>;
using EasedProgress = Scalar<EasedProgressTag>;
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson test -C builddir test_types --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/types.hpp tests/test_types.cpp
git commit -m "feat: add Progress and EasedProgress strong types"
```

---

### Task 2: Easing Functions

**Files:**
- Create: `include/prism/core/animation.hpp`
- Create: `tests/test_animation.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write failing tests for easing functions**

Create `tests/test_animation.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <prism/core/animation.hpp>

using namespace prism;

TEST_CASE("ease::linear") {
    CHECK(ease::linear(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::linear(Progress{0.5f}).raw() == doctest::Approx(0.5f));
    CHECK(ease::linear(Progress{1.f}).raw() == doctest::Approx(1.f));
}

TEST_CASE("ease::in_quad") {
    CHECK(ease::in_quad(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::in_quad(Progress{0.5f}).raw() == doctest::Approx(0.25f));
    CHECK(ease::in_quad(Progress{1.f}).raw() == doctest::Approx(1.f));
}

TEST_CASE("ease::out_quad") {
    CHECK(ease::out_quad(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::out_quad(Progress{0.5f}).raw() == doctest::Approx(0.75f));
    CHECK(ease::out_quad(Progress{1.f}).raw() == doctest::Approx(1.f));
}

TEST_CASE("ease::in_out_quad") {
    CHECK(ease::in_out_quad(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::in_out_quad(Progress{0.5f}).raw() == doctest::Approx(0.5f));
    CHECK(ease::in_out_quad(Progress{1.f}).raw() == doctest::Approx(1.f));
}

TEST_CASE("ease::in_cubic") {
    CHECK(ease::in_cubic(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::in_cubic(Progress{0.5f}).raw() == doctest::Approx(0.125f));
    CHECK(ease::in_cubic(Progress{1.f}).raw() == doctest::Approx(1.f));
}

TEST_CASE("ease::out_cubic") {
    CHECK(ease::out_cubic(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::out_cubic(Progress{1.f}).raw() == doctest::Approx(1.f));
}

TEST_CASE("ease::in_out_cubic") {
    CHECK(ease::in_out_cubic(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::in_out_cubic(Progress{0.5f}).raw() == doctest::Approx(0.5f));
    CHECK(ease::in_out_cubic(Progress{1.f}).raw() == doctest::Approx(1.f));
}
```

- [ ] **Step 2: Add test_animation to meson.build**

In `tests/meson.build`, add to the `headless_tests` dict (after the `'virtual_list'` entry):

```
  'animation' : files('test_animation.cpp'),
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `meson test -C builddir test_animation --print-errorlogs`
Expected: FAIL — `animation.hpp` does not exist

- [ ] **Step 4: Implement easing functions**

Create `include/prism/core/animation.hpp`:

```cpp
#pragma once

#include <prism/core/types.hpp>

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

} // namespace prism
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson test -C builddir test_animation --print-errorlogs`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/animation.hpp tests/test_animation.cpp tests/meson.build
git commit -m "feat: easing functions — linear, quad, cubic"
```

---

### Task 3: Lerpable Concept and lerp Overloads

**Files:**
- Modify: `include/prism/core/animation.hpp`
- Modify: `include/prism/core/draw_list.hpp`
- Modify: `tests/test_animation.cpp`

- [ ] **Step 1: Write failing tests for lerp**

Append to `tests/test_animation.cpp`:

```cpp
#include <prism/core/draw_list.hpp>

TEST_CASE("lerp float") {
    CHECK(prism::lerp(0.f, 10.f, EasedProgress{0.f}) == doctest::Approx(0.f));
    CHECK(prism::lerp(0.f, 10.f, EasedProgress{0.5f}) == doctest::Approx(5.f));
    CHECK(prism::lerp(0.f, 10.f, EasedProgress{1.f}) == doctest::Approx(10.f));
}

TEST_CASE("lerp Scalar<Tag>") {
    auto a = X{0.f};
    auto b = X{100.f};
    auto mid = prism::lerp(a, b, EasedProgress{0.25f});
    CHECK(mid.raw() == doctest::Approx(25.f));
}

TEST_CASE("lerp Color") {
    auto black = Color::rgba(0, 0, 0, 255);
    auto white = Color::rgba(255, 255, 255, 255);
    auto mid = prism::lerp(black, white, EasedProgress{0.5f});
    CHECK(mid.r == 128);
    CHECK(mid.g == 128);
    CHECK(mid.b == 128);
    CHECK(mid.a == 255);
}

TEST_CASE("Lerpable concept") {
    static_assert(prism::Lerpable<float>);
    static_assert(prism::Lerpable<X>);
    static_assert(prism::Lerpable<Color>);
    static_assert(!prism::Lerpable<std::string>);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson test -C builddir test_animation --print-errorlogs`
Expected: FAIL — `lerp` and `Lerpable` not defined

- [ ] **Step 3: Add lerp overloads and Lerpable concept**

In `include/prism/core/animation.hpp`, after the closing `} // namespace ease` but before closing `} // namespace prism`, add:

```cpp
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
```

- [ ] **Step 4: Add Color lerp in draw_list.hpp**

In `include/prism/core/draw_list.hpp`, after the `Color` struct closing brace (line 21), add:

```cpp
inline Color lerp(Color a, Color b, EasedProgress t) {
    auto mix = [&](uint8_t x, uint8_t y) -> uint8_t {
        return static_cast<uint8_t>(x + (y - x) * t.raw() + 0.5f);
    };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}
```

Note: `draw_list.hpp` already includes `prism/core/types.hpp` (line 3), so `EasedProgress` is available.

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson test -C builddir test_animation --print-errorlogs`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/animation.hpp include/prism/core/draw_list.hpp tests/test_animation.cpp
git commit -m "feat: Lerpable concept, lerp overloads for float/Scalar/Color"
```

---

### Task 4: Spring Model

**Files:**
- Modify: `include/prism/core/animation.hpp`
- Modify: `tests/test_animation.cpp`

- [ ] **Step 1: Write failing tests for Spring**

Append to `tests/test_animation.cpp`:

```cpp
#include <chrono>
using namespace std::chrono_literals;

TEST_CASE("Spring starts at zero progress") {
    Spring spring{};
    auto [progress, settled] = spring.evaluate(0ns);
    CHECK(progress.raw() == doctest::Approx(0.f));
    CHECK_FALSE(settled);
}

TEST_CASE("Spring converges to 1.0") {
    Spring spring{.stiffness = 100.f, .damping = 10.f, .mass = 1.f};
    auto [progress, settled] = spring.evaluate(5s);
    CHECK(progress.raw() == doctest::Approx(1.f).epsilon(0.01f));
    CHECK(settled);
}

TEST_CASE("Spring intermediate progress") {
    Spring spring{.stiffness = 100.f, .damping = 10.f, .mass = 1.f};
    auto [p1, s1] = spring.evaluate(100ms);
    CHECK(p1.raw() > 0.f);
    CHECK(p1.raw() < 1.f);
    CHECK_FALSE(s1);
}

TEST_CASE("Stiffer spring converges faster") {
    Spring soft{.stiffness = 50.f, .damping = 10.f, .mass = 1.f};
    Spring stiff{.stiffness = 200.f, .damping = 20.f, .mass = 1.f};
    auto [p_soft, _s1] = soft.evaluate(500ms);
    auto [p_stiff, _s2] = stiff.evaluate(500ms);
    CHECK(p_stiff.raw() > p_soft.raw());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson test -C builddir test_animation --print-errorlogs`
Expected: FAIL — `Spring` not defined

- [ ] **Step 3: Implement Spring**

In `include/prism/core/animation.hpp`, add `#include <chrono>` to the includes, then after the easing functions namespace but before the lerp overloads, add:

```cpp
struct Spring {
    float stiffness = 100.f;
    float damping = 10.f;
    float mass = 1.f;

    std::pair<Progress, bool> evaluate(std::chrono::nanoseconds elapsed) const {
        // Critically damped spring: x'' = -(k/m)*x - (c/m)*x'
        // Solved analytically for displacement from target (0→1 transition)
        float t = std::chrono::duration<float>(elapsed).count();
        float omega = std::sqrt(stiffness / mass);        // natural frequency
        float zeta = damping / (2.f * std::sqrt(stiffness * mass)); // damping ratio

        float x; // displacement from target (starts at -1, converges to 0)
        if (zeta < 1.f) {
            // Underdamped
            float omega_d = omega * std::sqrt(1.f - zeta * zeta);
            float env = std::exp(-zeta * omega * t);
            x = -env * (std::cos(omega_d * t) + (zeta * omega / omega_d) * std::sin(omega_d * t));
        } else if (zeta > 1.f) {
            // Overdamped
            float s1 = -omega * (zeta + std::sqrt(zeta * zeta - 1.f));
            float s2 = -omega * (zeta - std::sqrt(zeta * zeta - 1.f));
            float c2 = (s1 + omega * zeta) / (s1 - s2);
            float c1 = -1.f - c2;
            x = c1 * std::exp(s1 * t) + c2 * std::exp(s2 * t);
        } else {
            // Critically damped
            float env = std::exp(-omega * t);
            x = -(1.f + omega * t) * env;
        }

        float progress = std::clamp(1.f + x, 0.f, 1.f);
        bool settled = std::abs(x) < 0.001f;
        return {Progress{progress}, settled};
    }
};
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson test -C builddir test_animation --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/animation.hpp tests/test_animation.cpp
git commit -m "feat: Spring damped harmonic oscillator model"
```

---

### Task 5: TimingConfig and AnimationClock

**Files:**
- Modify: `include/prism/core/animation.hpp`
- Modify: `tests/test_animation.cpp`

- [ ] **Step 1: Write failing tests for AnimationClock**

Append to `tests/test_animation.cpp`:

```cpp
TEST_CASE("AnimationClock starts inactive") {
    AnimationClock clock;
    CHECK_FALSE(clock.active());
}

TEST_CASE("AnimationClock becomes active on add") {
    AnimationClock clock;
    auto id = clock.add([](AnimationClock::time_point) { return true; });
    CHECK(clock.active());
    clock.remove(id);
    CHECK_FALSE(clock.active());
}

TEST_CASE("AnimationClock tick advances animations") {
    AnimationClock clock;
    int tick_count = 0;
    clock.add([&](AnimationClock::time_point) {
        ++tick_count;
        return tick_count < 3;
    });
    CHECK(clock.active());

    auto t = AnimationClock::clock::now();
    clock.tick(t);
    CHECK(tick_count == 1);
    CHECK(clock.active());

    clock.tick(t + 16ms);
    CHECK(tick_count == 2);
    CHECK(clock.active());

    clock.tick(t + 32ms);
    CHECK(tick_count == 3);
    CHECK_FALSE(clock.active());
}

TEST_CASE("AnimationClock remove during tick is safe") {
    AnimationClock clock;
    uint64_t id = 0;
    id = clock.add([&](AnimationClock::time_point) {
        clock.remove(id);
        return false;
    });
    auto t = AnimationClock::clock::now();
    clock.tick(t); // should not crash
    CHECK_FALSE(clock.active());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson test -C builddir test_animation --print-errorlogs`
Expected: FAIL — `AnimationClock`, `AnimationConfig`, etc. not defined

- [ ] **Step 3: Implement TimingConfig and AnimationClock**

In `include/prism/core/animation.hpp`, add `#include <functional>` and `#include <variant>` to includes, then after the `Spring` struct, add:

```cpp
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
        // Snapshot to allow safe removal during tick
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
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson test -C builddir test_animation --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/animation.hpp tests/test_animation.cpp
git commit -m "feat: AnimationClock and TimingConfig types"
```

---

### Task 6: Animation<T> — Explicit One-Shot

**Files:**
- Modify: `include/prism/core/animation.hpp`
- Modify: `tests/test_animation.cpp`

- [ ] **Step 1: Write failing tests for Animation<T>**

Append to `tests/test_animation.cpp`:

```cpp
#include <prism/core/field.hpp>

TEST_CASE("animate() reaches target after duration") {
    AnimationClock clock;
    Field<float> value{0.f};

    auto anim = prism::animate(clock, value, 100.f,
        AnimationConfig{.duration = 100ms, .easing = ease::linear});

    auto t0 = AnimationClock::clock::now();

    clock.tick(t0 + 50ms);
    CHECK(value.get() == doctest::Approx(50.f).epsilon(5.f));

    clock.tick(t0 + 100ms);
    CHECK(value.get() == doctest::Approx(100.f));
    CHECK_FALSE(clock.active());
}

TEST_CASE("animate() with Scalar type") {
    AnimationClock clock;
    Field<X> pos{X{0.f}};

    auto anim = prism::animate(clock, pos, X{200.f},
        AnimationConfig{.duration = 200ms, .easing = ease::linear});

    auto t0 = AnimationClock::clock::now();
    clock.tick(t0 + 100ms);
    CHECK(pos.get().raw() == doctest::Approx(100.f).epsilon(5.f));

    clock.tick(t0 + 200ms);
    CHECK(pos.get().raw() == doctest::Approx(200.f));
}

TEST_CASE("animate() with spring") {
    AnimationClock clock;
    Field<float> value{0.f};

    auto anim = prism::animate(clock, value, 1.f,
        SpringConfig{.spring = {.stiffness = 100.f, .damping = 10.f, .mass = 1.f}});

    auto t0 = AnimationClock::clock::now();
    clock.tick(t0 + 5s);
    CHECK(value.get() == doctest::Approx(1.f).epsilon(0.01f));
    CHECK_FALSE(clock.active());
}

TEST_CASE("animate() RAII — dropping stops animation") {
    AnimationClock clock;
    Field<float> value{0.f};

    {
        auto anim = prism::animate(clock, value, 100.f,
            AnimationConfig{.duration = 1s, .easing = ease::linear});
        CHECK(clock.active());
    }
    // anim destroyed
    CHECK_FALSE(clock.active());
    CHECK(value.get() < 100.f); // did not reach target
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson test -C builddir test_animation --print-errorlogs`
Expected: FAIL — `animate()` and `Animation` not defined

- [ ] **Step 3: Implement Animation<T> and animate()**

In `include/prism/core/animation.hpp`, add `#include <prism/core/field.hpp>` to includes, then after `AnimationClock`, add:

```cpp
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
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson test -C builddir test_animation --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/animation.hpp tests/test_animation.cpp
git commit -m "feat: Animation<T> and animate() one-shot API"
```

---

### Task 7: TransitionGuard<T> — Implicit Transitions

**Files:**
- Modify: `include/prism/core/animation.hpp`
- Modify: `tests/test_animation.cpp`

- [ ] **Step 1: Write failing tests for TransitionGuard**

Append to `tests/test_animation.cpp`:

```cpp
TEST_CASE("transition() interpolates on set()") {
    AnimationClock clock;
    Field<float> value{0.f};

    auto guard = prism::transition(clock, value,
        AnimationConfig{.duration = 100ms, .easing = ease::linear});

    auto t0 = AnimationClock::clock::now();

    value.set(100.f);
    CHECK(clock.active());

    clock.tick(t0 + 50ms);
    CHECK(value.get() == doctest::Approx(50.f).epsilon(5.f));

    clock.tick(t0 + 100ms);
    CHECK(value.get() == doctest::Approx(100.f));
    CHECK_FALSE(clock.active());
}

TEST_CASE("transition() rapid retarget") {
    AnimationClock clock;
    Field<float> value{0.f};

    auto guard = prism::transition(clock, value,
        AnimationConfig{.duration = 100ms, .easing = ease::linear});

    auto t0 = AnimationClock::clock::now();

    value.set(100.f);
    clock.tick(t0 + 50ms);
    float mid = value.get();
    CHECK(mid == doctest::Approx(50.f).epsilon(5.f));

    // Retarget to 0 mid-animation
    value.set(0.f);
    // The new animation starts from ~50, going to 0
    clock.tick(t0 + 50ms + 50ms);
    CHECK(value.get() == doctest::Approx(mid / 2.f).epsilon(5.f));

    clock.tick(t0 + 50ms + 100ms);
    CHECK(value.get() == doctest::Approx(0.f).epsilon(1.f));
}

TEST_CASE("transition() recursion guard — no infinite loop") {
    AnimationClock clock;
    Field<float> value{0.f};

    auto guard = prism::transition(clock, value,
        AnimationConfig{.duration = 100ms, .easing = ease::linear});

    value.set(100.f);
    auto t0 = AnimationClock::clock::now();

    // Multiple ticks should not cause re-entrant set→callback→set loops
    for (int i = 1; i <= 10; ++i) {
        clock.tick(t0 + std::chrono::milliseconds(i * 10));
    }
    CHECK(value.get() == doctest::Approx(100.f));
}

TEST_CASE("transition() RAII — dropping stops interception") {
    AnimationClock clock;
    Field<float> value{0.f};

    {
        auto guard = prism::transition(clock, value,
            AnimationConfig{.duration = 1s, .easing = ease::linear});
        value.set(100.f);
        CHECK(clock.active());
    }
    // guard destroyed — clock should be inactive
    CHECK_FALSE(clock.active());

    // Further set() should be instant (no transition)
    value.set(50.f);
    CHECK(value.get() == doctest::Approx(50.f));
    CHECK_FALSE(clock.active());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson test -C builddir test_animation --print-errorlogs`
Expected: FAIL — `transition()` and `TransitionGuard` not defined

- [ ] **Step 3: Implement TransitionGuard<T> and transition()**

In `include/prism/core/animation.hpp`, after `animate()`, add:

```cpp
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
            if (clock_ && clock_id_) {
                clock_->remove(clock_id_);
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

    // Returns true = keep going, false = done (same convention as Animation<T>)
    bool tick_with(const AnimationConfig& cfg, std::chrono::nanoseconds elapsed) {
        float t = std::chrono::duration<float>(elapsed).count()
                / std::chrono::duration<float>(cfg.duration).count();
        if (t >= 1.f) {
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
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson test -C builddir test_animation --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/animation.hpp tests/test_animation.cpp
git commit -m "feat: TransitionGuard<T> and transition() implicit API"
```

---

### Task 8: AnimationClock Integration in model_app

**Files:**
- Modify: `include/prism/core/model_app.hpp`

- [ ] **Step 1: Add AnimationClock to model_app and AppContext**

In `include/prism/core/model_app.hpp`:

Add include at the top:

```cpp
#include <prism/core/animation.hpp>
```

Modify `AppContext` class to hold the clock:

```cpp
class AppContext {
public:
    using scheduler_type = decltype(std::declval<stdexec::run_loop>().get_scheduler());

    explicit AppContext(scheduler_type s, AnimationClock& c) : sched_(s), clock_(&c) {}
    scheduler_type scheduler() const { return sched_; }
    AnimationClock& clock() { return *clock_; }

private:
    scheduler_type sched_;
    AnimationClock* clock_;
};
```

In the `model_app` template function, after `WidgetTree tree(model);` (line 32), add:

```cpp
    AnimationClock anim_clock;
    bool tick_scheduled = false;
```

Add a `schedule_tick` lambda after the `publish` lambda:

```cpp
    std::function<void()> schedule_tick;
    schedule_tick = [&] {
        if (!anim_clock.active() || tick_scheduled) return;
        tick_scheduled = true;
        exec::start_detached(
            stdexec::schedule(sched)
            | stdexec::then([&] {
                tick_scheduled = false;
                anim_clock.tick(AnimationClock::clock::now());
                if (tree.any_dirty())
                    publish();
                if (anim_clock.active())
                    schedule_tick();
            })
        );
    };
```

At the end of the event handler lambda (after `if (tree.any_dirty() || needs_publish) publish();`, line 107-108), add:

```cpp
                    schedule_tick();
```

Modify the `setup` call (line 117-120) to pass the clock:

```cpp
    if (setup) {
        auto ctx = AppContext(sched, anim_clock);
        setup(ctx);
        schedule_tick();
    }
```

- [ ] **Step 2: Verify the project builds**

Run: `meson compile -C builddir`
Expected: PASS — no compilation errors

- [ ] **Step 3: Run all existing tests to check for regressions**

Run: `meson test -C builddir --print-errorlogs`
Expected: All tests PASS

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/model_app.hpp
git commit -m "feat: integrate AnimationClock into model_app event loop"
```
