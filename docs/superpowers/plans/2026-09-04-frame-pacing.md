# Frame Pacing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `SoftwareBackend` a vsync-style present cadence (frame clock + dirty-snapshot dedup) so the render path is no longer event-rate-bound.

**Architecture:** A pure, SDL-free `FramePacer` helper owns the frame-deadline math (unit-tested headlessly). `SoftwareBackend::run()` waits with `SDL_WaitEventTimeout` against that clock and presents only windows whose snapshot changed since the last present; input dispatch stays per-event-batch and unpaced. Spec: `docs/superpowers/specs/2026-09-04-frame-pacing-design.md`.

**Tech Stack:** C++26, SDL3, Meson, doctest.

## Global Constraints

- Build: `ninja -C builddir` — one build/test invocation at a time, foreground, never backgrounded (AGENTS.md).
- Full suite: `meson test -C builddir` — must be green after every task.
- Core headers stay SDL-free; SDL types never leak past the backend layer. `frame_pacer.hpp` must not include SDL.
- Tests use doctest: `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` + `#include <doctest.h>`.
- Headless tests register in the `headless_tests` map in `tests/meson.build`.
- Commit explicitly listed files only; never `git add .`.
- Current behavior reference: run loop at `src/backends/software_backend.cpp:187-361`; renderer creation at `src/backends/sdl_window.cpp:99`; `RenderConfig` at `include/prism/app/window.hpp:32-34`.

---

### Task 1: `FramePacer` helper + unit tests

**Files:**
- Create: `include/prism/backends/frame_pacer.hpp`
- Test: `tests/test_frame_pacer.cpp`
- Modify: `tests/meson.build` (add one line to `headless_tests`, after the `record_reuse` entry at line 65)

**Interfaces:**
- Consumes: nothing (new, self-contained).
- Produces (Task 3 relies on these exact names):
  - `prism::backends::FramePacer` — ctor `explicit FramePacer(std::chrono::nanoseconds frame_interval)`; `using clock = std::chrono::steady_clock; using time_point = clock::time_point;`
  - `bool FramePacer::frame_due(time_point now) const`
  - `std::chrono::milliseconds FramePacer::ms_until_next_frame(time_point now) const`
  - `void FramePacer::mark_presented(time_point now)`
  - `double prism::backends::resolve_frame_hz(float target_fps, float display_hz)`
  - `std::chrono::nanoseconds prism::backends::frame_interval_from_hz(double hz)`

- [ ] **Step 1: Write the failing test**

Create `tests/test_frame_pacer.cpp`:

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C builddir tests/test_frame_pacer && ./builddir/tests/test_frame_pacer`
Expected: FAIL — compile error, `prism/backends/frame_pacer.hpp` does not exist.

- [ ] **Step 3: Write the implementation**

Create `include/prism/backends/frame_pacer.hpp`:

```cpp
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
```

- [ ] **Step 4: Register the test and verify it passes**

In `tests/meson.build`, add after the `'record_reuse'` line (line 65):

```meson
  'frame_pacer' : files('test_frame_pacer.cpp'),
```

Run: `ninja -C builddir tests/test_frame_pacer && ./builddir/tests/test_frame_pacer`
Expected: PASS, all 6 test cases.

- [ ] **Step 5: Commit**

```bash
git add include/prism/backends/frame_pacer.hpp tests/test_frame_pacer.cpp tests/meson.build
git commit -m "feat(backends): add FramePacer present-cadence helper"
```

---

### Task 2: `RenderConfig` pacing fields + `SdlWindow::enable_vsync`

**Files:**
- Modify: `include/prism/app/window.hpp:32-34` (`RenderConfig`)
- Modify: `include/prism/backends/sdl_window.hpp` (method declaration, after `render_snapshot` at line 62)
- Modify: `src/backends/sdl_window.cpp` (new method, after `create_sdl_window` at lines 91-104)

**Interfaces:**
- Consumes: nothing from Task 1 (compiles independently).
- Produces (Task 3 relies on these):
  - `RenderConfig::frame_pacing` (`bool`, default `true`), `RenderConfig::target_fps` (`float`, default `0`)
  - `void SdlWindow::enable_vsync()` — public method.

- [ ] **Step 1: Add the config fields**

Replace in `include/prism/app/window.hpp`:

```cpp
struct RenderConfig {
    const char* font_path = nullptr;
};
```

with:

```cpp
struct RenderConfig {
    const char* font_path = nullptr;
    // Present pacing (SoftwareBackend): presents happen on frame boundaries instead
    // of after every event batch. false = legacy event-rate-bound behavior.
    bool frame_pacing = true;
    // 0 = follow the display's refresh rate; >0 = fixed cap in frames per second.
    float target_fps = 0;
};
```

- [ ] **Step 2: Add `enable_vsync` to `SdlWindow`**

In `include/prism/backends/sdl_window.hpp`, after the `render_snapshot` declaration (line 62), add:

```cpp
    // Best-effort vsync (tear guard on top of the backend's frame clock).
    void enable_vsync();
```

In `src/backends/sdl_window.cpp`, after `create_sdl_window()` (ends line 104), add:

```cpp
void SdlWindow::enable_vsync() {
    // Best-effort: renderers without vsync support just fail the call; the frame
    // clock (SoftwareBackend's FramePacer) still paces presents regardless.
    if (renderer_) SDL_SetRenderVSync(renderer_, 1);
}
```

- [ ] **Step 3: Build and run the suite**

Run: `ninja -C builddir && meson test -C builddir`
Expected: everything green (fields are defaults-only, method unused so far; all existing `RenderConfig{}` / `{{}}` aggregate inits remain valid).

- [ ] **Step 4: Commit**

```bash
git add include/prism/app/window.hpp include/prism/backends/sdl_window.hpp src/backends/sdl_window.cpp
git commit -m "feat(backends): add frame_pacing/target_fps config and renderer vsync"
```

---

### Task 3: Paced, dirty-tracked presents in `SoftwareBackend::run()`

**Files:**
- Modify: `include/prism/backends/software_backend.hpp` (includes + 3 members)
- Modify: `src/backends/software_backend.cpp` (pacer setup, EXPOSED case, wait/present restructure, close-drain cleanup, vsync for late windows)

**Interfaces:**
- Consumes (from Tasks 1-2): `FramePacer`, `resolve_frame_hz`, `frame_interval_from_hz`, `RenderConfig::frame_pacing`, `RenderConfig::target_fps`, `SdlWindow::enable_vsync()`.
- Produces: no new API. Behavior change: presents coalesce to frame boundaries; unchanged snapshots are never re-presented.

- [ ] **Step 1: Add members to `software_backend.hpp`**

Add includes at the top:

```cpp
#include <prism/backends/frame_pacer.hpp>

#include <optional>
#include <unordered_set>
```

Add members after `WindowId pressed_window_ = 0;` (line 52):

```cpp
    // Frame pacing — see doc/design/render-backend.md "Present Cadence".
    // nullopt pacer_ = unpaced (RenderConfig::frame_pacing == false).
    std::optional<FramePacer> pacer_;
    // Last snapshot actually presented per window; a window presents only when the
    // slot's shared_ptr differs (or force_redraw_ contains it). Guarded by windows_mutex_.
    std::unordered_map<WindowId, std::shared_ptr<const SceneSnapshot>> last_presented_;
    std::unordered_set<WindowId> force_redraw_;
```

- [ ] **Step 2: Resolve the pacer and enable vsync at `run()` start**

In `src/backends/software_backend.cpp`, after the `ensure_created`/`SDL_StartTextInput` loop (ends line 152) and before `ready_.store(true, ...)` (line 154), insert:

```cpp
    // Present pacing: resolve the frame interval once — RenderConfig::target_fps,
    // else the display's actual refresh rate (60 Hz fallback) — and ask SDL for
    // vsync as a best-effort tear guard. Input dispatch below stays per-event-batch;
    // only presents wait for the frame clock.
    if (render_config_.frame_pacing) {
        float display_hz = 0;
        {
            std::lock_guard<std::mutex> lk(windows_mutex_);
            if (!windows_.empty()) {
                if (auto display = SDL_GetDisplayForWindow(windows_.begin()->second->sdl_window()))
                    if (const auto* mode = SDL_GetCurrentDisplayMode(display))
                        display_hz = mode->refresh_rate;
            }
            for (auto& [id, win] : windows_) win->enable_vsync();
        }
        pacer_.emplace(frame_interval_from_hz(resolve_frame_hz(render_config_.target_fps, display_hz)));
    }
```

- [ ] **Step 3: Vsync for windows created after `run()`**

In `drain_window_requests()` (`software_backend.cpp:95-97`), after `win->ensure_created();`, add:

```cpp
            if (pacer_) win->enable_vsync();
```

- [ ] **Step 4: Clean up pacing state on window close**

In `drain_close_requests()` (`software_backend.cpp:119-125`), inside the lock, after `snapshots_.erase(*id_opt);`, add:

```cpp
        last_presented_.erase(*id_opt);
        force_redraw_.erase(*id_opt);
```

- [ ] **Step 5: Handle `SDL_EVENT_WINDOW_EXPOSED`**

In the event switch in `run()`, add a case before `default:` (line 333):

```cpp
            case SDL_EVENT_WINDOW_EXPOSED:
                // De-occlusion/un-minimize: SDL needs a redraw even though the
                // snapshot pointer is unchanged.
                {
                    std::lock_guard<std::mutex> lk(windows_mutex_);
                    force_redraw_.insert(wid);
                }
                break;
```

- [ ] **Step 6: Restructure the wait/present loop**

Replace this exact code (`software_backend.cpp:187-191`):

```cpp
    while (running_.load(std::memory_order_relaxed)) {
        SDL_Event ev;
        if (!SDL_WaitEvent(&ev)) continue;

        do {
```

with:

```cpp
    // A window needs presenting when its snapshot slot differs from what was last
    // presented (or SDL asked for a redraw).
    auto any_dirty = [&] {
        std::lock_guard<std::mutex> lk(windows_mutex_);
        for (auto& [id, slot] : snapshots_)
            if (force_redraw_.contains(id) ||
                slot.snapshot.load(std::memory_order_acquire) != last_presented_[id])
                return true;
        return false;
    };

    while (running_.load(std::memory_order_relaxed)) {
        SDL_Event ev;
        // Paced + dirty: wait only until the next frame deadline (events still wake
        // us earlier). Otherwise block indefinitely — every submit() is paired with
        // wake(), and with nothing dirty there is no present to wait for.
        bool have_event;
        if (pacer_ && any_dirty()) {
            have_event = SDL_WaitEventTimeout(&ev, static_cast<int>(
                pacer_->ms_until_next_frame(FramePacer::clock::now()).count()));
        } else {
            have_event = SDL_WaitEvent(&ev);
            if (!have_event) continue;
        }

        if (have_event) {
            do {
```

Then re-indent the entire existing `do { ... } while` body (the event-dispatch switch) one level deeper to sit inside `if (have_event) {`, and replace its closing sequence plus the render block — replace this exact code (`software_backend.cpp:336-360`):

```cpp
        } while (SDL_PollEvent(&ev));

        flush_pending_motion(); // the queue's last event is very often a motion event itself

        if (!running_.load(std::memory_order_relaxed)) break;

        // Render any pending snapshots — collect under lock, render outside
        {
            std::vector<std::pair<WindowId, std::shared_ptr<const SceneSnapshot>>> to_render;
            {
                std::lock_guard<std::mutex> lk(windows_mutex_);
                for (auto& [id, snap_slot] : snapshots_) {
                    auto snap = snap_slot.snapshot.load(std::memory_order_acquire);
                    if (snap) to_render.emplace_back(id, std::move(snap));
                }
            }
            for (auto& [id, snap] : to_render) {
                SdlWindow* win = nullptr;
                {
                    std::lock_guard<std::mutex> lk(windows_mutex_);
                    if (auto it = windows_.find(id); it != windows_.end()) win = it->second.get();
                }
                if (win) win->render_snapshot(*snap, font_);
            }
        }
    }
```

with:

```cpp
            } while (SDL_PollEvent(&ev));

            flush_pending_motion(); // the queue's last event is very often a motion event itself
        }

        if (!running_.load(std::memory_order_relaxed)) break;

        // Present dirty snapshots — collect under lock, render outside. With pacing
        // on, only on frame boundaries; last_presented_ dedupes unchanged snapshots,
        // so a high-rate publish stream coalesces into one present per frame.
        const auto now = FramePacer::clock::now();
        if (!pacer_ || pacer_->frame_due(now)) {
            std::vector<std::pair<WindowId, std::shared_ptr<const SceneSnapshot>>> to_render;
            {
                std::lock_guard<std::mutex> lk(windows_mutex_);
                for (auto& [id, snap_slot] : snapshots_) {
                    auto snap = snap_slot.snapshot.load(std::memory_order_acquire);
                    if (!snap) continue;
                    if (!force_redraw_.contains(id) && snap == last_presented_[id]) continue;
                    to_render.emplace_back(id, snap);
                    last_presented_[id] = std::move(snap);
                }
                force_redraw_.clear();
            }
            bool presented_any = false;
            for (auto& [id, snap] : to_render) {
                SdlWindow* win = nullptr;
                {
                    std::lock_guard<std::mutex> lk(windows_mutex_);
                    if (auto it = windows_.find(id); it != windows_.end()) win = it->second.get();
                }
                if (win) {
                    win->render_snapshot(*snap, font_);
                    presented_any = true;
                }
            }
            if (presented_any && pacer_) pacer_->mark_presented(now);
        }
    }
```

- [ ] **Step 7: Build and run the full suite**

Run: `ninja -C builddir && meson test -C builddir`
Expected: all green, including the SDL-backed tests (`software_backend_*`, `sdl_window`, `model_app_chrome_cursor`, `app`).

If an SDL-backed test turns timing-flaky because presents now wait for a frame boundary, construct its backend unpaced — e.g. change `prism::backends::SoftwareBackend backend{{}};` (`tests/test_software_backend_chrome_cursor.cpp:47`) to `prism::backends::SoftwareBackend backend{{.frame_pacing = false}};`. Only do this for tests that actually fail; do not pre-emptively change them.

- [ ] **Step 8: Run the stall-latency benchmark**

Run: `./builddir/benchmarks/bench_stall_latency`
Expected: exits 0 (it uses a headless backend, so pacing must not affect it).

- [ ] **Step 9: Commit**

```bash
git add include/prism/backends/software_backend.hpp src/backends/software_backend.cpp
git commit -m "perf(backends): pace presents to frame boundaries, skip unchanged snapshots"
```

---

### Task 4: Document present cadence in `render-backend.md`

**Files:**
- Modify: `doc/design/render-backend.md` (new section after "Software Backend (Implemented)", i.e. after line 64)

**Interfaces:**
- Consumes: the behavior landed in Task 3.
- Produces: docs only.

- [ ] **Step 1: Add the section**

Insert after the "POC limitations" list (line 64), before `## Build System`:

```markdown
## Present Cadence

`SoftwareBackend` paces presents to the display's refresh rate (`RenderConfig::frame_pacing`, default on): the run loop waits on `SDL_WaitEventTimeout` against a `FramePacer` frame clock (`include/prism/backends/frame_pacer.hpp`) and presents at most one frame per window per refresh interval. `RenderConfig::target_fps` overrides the rate (0 = display rate, 60 Hz fallback). Input event dispatch to the app thread is *not* paced — only presents are — so input latency is independent of the frame clock. A window is presented only when its snapshot changed since the last present (`shared_ptr` comparison) or SDL requested a redraw (`SDL_EVENT_WINDOW_EXPOSED`); a high-rate publish stream coalesces into one present per frame. Renderer vsync is requested as a best-effort tear guard. `frame_pacing = false` restores event-rate-bound behavior.
```

- [ ] **Step 2: Commit**

```bash
git add doc/design/render-backend.md
git commit -m "docs: present cadence in render-backend.md"
```
