# Animation System — Design Spec

## Overview

A general-purpose animation engine for PRISM. Provides smooth property interpolation via easing functions and spring physics, driven by a wall-clock timer that integrates with the stdexec run_loop. Zero CPU when no animations are active.

## Motivation

PRISM's current visual state transitions (hover, press, focus) are instant. The scrollbar `show_ticks` countdown is frame-count-based and never actually used for visibility. An animation system provides smooth transitions for any `Field<T>` value — opacity fades, position slides, color transitions — while maintaining the event-driven idle architecture.

## Design Decisions

- **Hybrid API**: implicit transitions (any `set()` interpolates) + explicit one-shot animations
- **External to Field<T>**: animation state lives in `Animation<T>` / `TransitionGuard<T>`, not inside `Field<T>`
- **Wall-clock timing**: `std::chrono::steady_clock`, not frame counts
- **Timer-based wake**: a ~60Hz tick runs on the run_loop while animations are active, stops when the last one completes
- **Lerpable concept**: any type with a `lerp(T, T, EasedProgress)` overload is animatable
- **Flag-based recursion guard**: `TransitionGuard` uses a `bool animating_` flag to avoid re-triggering itself when calling `field.set()` — no changes to `Field<T>`

## Strong Types

Two new types in `types.hpp`:

```cpp
using Progress = Scalar<struct ProgressTag>;        // [0,1] normalized elapsed time
using EasedProgress = Scalar<struct EasedProgressTag>;  // [0,1] after easing curve
```

`Progress` represents raw linear time progression. `EasedProgress` is the output of an easing function, used as the interpolation parameter.

## Lerpable Concept

```cpp
template <typename T>
concept Lerpable = requires(T a, T b, EasedProgress t) {
    { lerp(a, b, t) } -> std::convertible_to<T>;
};
```

### Built-in lerp Overloads

```cpp
float lerp(float a, float b, EasedProgress t);
Color lerp(Color a, Color b, EasedProgress t);  // per-channel uint8_t interpolation

template <typename Tag>
Scalar<Tag> lerp(Scalar<Tag> a, Scalar<Tag> b, EasedProgress t);
```

Users can make custom types animatable by providing a `lerp` overload.

## Easing Functions

Pure functions `Progress -> EasedProgress` in `namespace prism::ease`:

```cpp
namespace prism::ease {
    EasedProgress linear(Progress t);
    EasedProgress in_quad(Progress t);
    EasedProgress out_quad(Progress t);
    EasedProgress in_out_quad(Progress t);
    EasedProgress in_cubic(Progress t);
    EasedProgress out_cubic(Progress t);
    EasedProgress in_out_cubic(Progress t);
}
```

All functions satisfy: `f(Progress{0}) == EasedProgress{0}` and `f(Progress{1}) == EasedProgress{1}`.

## Spring Model

Critically damped spring for natural-feeling animations:

```cpp
struct Spring {
    float stiffness = 100.f;
    float damping = 10.f;
    float mass = 1.f;

    // Returns progress and whether the spring has settled
    std::pair<Progress, bool> evaluate(std::chrono::nanoseconds elapsed) const;
};
```

`evaluate()` solves the damped harmonic oscillator equation for the given elapsed time. Returns `Progress` (displacement from start toward target) and a settled flag (true when displacement and velocity are both below threshold).

## Timing Configuration

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
```

`AnimationConfig` for fixed-duration eased animations. `SpringConfig` for physics-based animations with variable duration.

## AnimationClock

Global clock that drives all active animations. Owned by model_app alongside WidgetTree.

```cpp
class AnimationClock {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    // Register a tick function. Returns an ID for deregistration.
    // tick_fn receives current time, returns true if still animating.
    uint64_t add(std::function<bool(time_point now)> tick_fn);

    // Deregister an animation
    void remove(uint64_t id);

    // Advance all active animations. Removes completed ones.
    void tick(time_point now);

    // Are any animations running?
    [[nodiscard]] bool active() const;

private:
    uint64_t next_id_ = 1;
    std::vector<std::pair<uint64_t, std::function<bool(time_point)>>> animations_;
};
```

### Timer Integration in model_app

- `AnimationClock` is created alongside `WidgetTree`
- After processing any event, if `clock.active()` and no tick timer is scheduled, schedule a recurring ~16ms tick on the run_loop via `stdexec::schedule`
- Each tick: `clock.tick(steady_clock::now())`, then check `tree.any_dirty()` and publish if needed
- When `clock.active()` returns false after a tick, stop scheduling further ticks
- Net effect: zero CPU when idle, smooth 60Hz updates during animation

### AppContext Integration

`AppContext` exposes the clock:

```cpp
class AppContext {
public:
    AnimationClock& clock() { return *clock_; }
    // ... existing scheduler() ...
};
```

## Animation<T>

Explicit one-shot animation binding a `Field<T>` to an interpolation:

```cpp
template <Lerpable T>
class Animation {
public:
    Animation(AnimationClock& clock, Field<T>& field, T target, TimingConfig config);
    ~Animation();  // deregisters from clock

    // Move-only
    Animation(Animation&&) noexcept;
    Animation& operator=(Animation&&) noexcept;

private:
    uint64_t clock_id_ = 0;
    AnimationClock* clock_ = nullptr;
    Field<T>* field_ = nullptr;
    T from_;
    T to_;
    AnimationClock::time_point start_;
    TimingConfig config_;
};
```

On construction, captures `from_ = field.get()`, `to_ = target`, `start_ = now`, and registers a tick function with the clock. Each tick computes progress from elapsed time, applies easing (or spring), lerps between `from_` and `to_`, and calls `field.set()`. When progress reaches 1.0 (or spring settles), the tick function returns false and the animation is removed.

Destruction deregisters from the clock — if the `Animation` goes out of scope, the animation stops.

## TransitionGuard<T>

Implicit transition — any `set()` on the field smoothly interpolates:

```cpp
template <Lerpable T>
class TransitionGuard {
public:
    TransitionGuard(AnimationClock& clock, Field<T>& field, TimingConfig config);
    ~TransitionGuard();  // disconnects subscription, deregisters from clock

    // Move-only
    TransitionGuard(TransitionGuard&&) noexcept;
    TransitionGuard& operator=(TransitionGuard&&) noexcept;

private:
    AnimationClock* clock_ = nullptr;
    Field<T>* field_ = nullptr;
    TimingConfig config_;
    uint64_t clock_id_ = 0;
    Connection subscription_;
    T current_animated_;  // current interpolated value
    T target_;
    AnimationClock::time_point start_;
    bool animating_ = false;  // recursion guard
};
```

### Recursion Guard

`TransitionGuard` subscribes to `field.on_change()`. During animation, it calls `field.set()` with interpolated values. The `animating_` flag prevents the subscription callback from re-triggering:

```
on_change callback:
    if (animating_) return;       // we caused this, ignore
    from_ = current_animated_;    // start from where we are
    target_ = field.get();        // new user-provided target
    start_ = now;                 // reset timer
    register tick if not active

tick function:
    animating_ = true;
    field.set(lerp(from_, target_, progress));
    animating_ = false;
```

This is safe because everything runs on the single run_loop thread. No changes to `Field<T>` required.

### Rapid Retargeting

If the user calls `set()` while a transition is in flight, the guard captures `from_ = current_animated_` (the current interpolated position, not the field's snapped value) and starts a new interpolation toward the new target. This produces smooth retargeting with no visual discontinuity.

## User API

### Free Functions

```cpp
// Explicit one-shot animation
template <Lerpable T>
[[nodiscard]]
Animation<T> animate(AnimationClock& clock, Field<T>& field, T target, TimingConfig config);

// Implicit transition — any set() on field interpolates while guard is alive
template <Lerpable T>
[[nodiscard]]
TransitionGuard<T> transition(AnimationClock& clock, Field<T>& field, TimingConfig config);
```

Both return `[[nodiscard]]` RAII objects. Dropping the return value stops the animation.

### Usage Examples

```cpp
struct Panel {
    Field<float> opacity{1.0f};
    Field<Y> panel_y{Y{-200}};

    void setup(AppContext& ctx) {
        // Implicit: opacity always transitions smoothly
        opacity_guard_ = prism::transition(ctx.clock(), opacity,
            AnimationConfig{.duration = 200ms, .easing = ease::out_quad});

        // Later, any set() is smooth:
        opacity.set(0.0f);  // fades out over 200ms
        opacity.set(1.0f);  // fades back in, retargets smoothly
    }

    void show(AppContext& ctx) {
        // Explicit: slide panel into view
        slide_anim_ = prism::animate(ctx.clock(), panel_y, Y{0},
            SpringConfig{.spring = {.stiffness = 200, .damping = 15}});
    }

private:
    TransitionGuard<float> opacity_guard_;
    Animation<Y> slide_anim_;
};
```

## What Changes

| File | Change |
|---|---|
| `include/prism/core/types.hpp` | `Progress`, `EasedProgress` strong types |
| `include/prism/core/animation.hpp` | **New.** `Lerpable` concept, `lerp` overloads, easing functions, `Spring`, `AnimationConfig`, `SpringConfig`, `TimingConfig`, `AnimationClock`, `Animation<T>`, `TransitionGuard<T>`, `transition()`, `animate()` |
| `include/prism/core/model_app.hpp` | Own `AnimationClock`, wire tick timer, expose via `AppContext` |
| `include/prism/core/draw_list.hpp` | `lerp(Color, Color, EasedProgress)` overload |
| `tests/test_animation.cpp` | **New.** All animation tests |
| `tests/meson.build` | Add `test_animation.cpp` |

## What Stays Unchanged

- `Field<T>`, `State<T>`, `Connection`, `SenderHub` — untouched
- All existing `Delegate<T>` specializations — untouched
- `WidgetTree`, `WidgetNode` — untouched
- Layout, hit testing, scroll, virtual list — untouched
- All backends — untouched

## Testing Strategy

All headless, no SDL:

1. **Easing functions** — known values at `Progress{0}`, `Progress{0.5}`, `Progress{1}`
2. **lerp overloads** — float, Color (per-channel), `Scalar<Tag>`
3. **Spring convergence** — `evaluate()` reaches settled state within reasonable time
4. **AnimationClock lifecycle** — `add`/`remove`/`tick`, `active()` transitions to false when empty
5. **animate()** — field reaches target value after duration elapses, intermediate ticks produce interpolated values
6. **transition()** — `set()` triggers smooth interpolation, field value progresses over ticks
7. **Rapid retargeting** — calling `set()` mid-transition starts from current interpolated position
8. **Recursion guard** — animation's own `set()` does not re-trigger the transition callback
9. **RAII cleanup** — dropping `Animation`/`TransitionGuard` deregisters from clock, clock becomes inactive
10. **Timer self-stop** — `clock.active()` becomes false after last animation completes

## Out of Scope

- Animation groups (coordinated multi-field animations)
- Keyframe sequences
- Layout animations (size/position transitions on add/remove)
- Delegate-level implicit transitions (auto-animate hover/press visual states)
- Scrollbar fade migration (replacing `show_ticks` with animation system)
- Cubic-bezier custom curves
- Animation pause/resume
