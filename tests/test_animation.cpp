#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/animation.hpp>
#include <prism/render/draw_list.hpp>
#include <prism/core/field.hpp>
#include <string>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;

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
    auto [p_soft, _s1] = soft.evaluate(150ms);
    auto [p_stiff, _s2] = stiff.evaluate(150ms);
    CHECK(p_stiff.raw() > p_soft.raw());
}

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
