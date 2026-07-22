# System Monitor Example — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a live Linux system monitor example app ("fancy htop") that composes Plot,
Table, Tree, TabBar, and resizable split panes over real `/proc` data fed by two background
threads — a realistic, multi-widget stress test for PRISM, not another single-widget demo.

**Architecture:** Two independent `std::jthread` pollers read `/proc` and push plain-data
snapshots into two `Shared<T>` cells. The UI thread ingests each snapshot (via `.observe()`,
fired during drain) into its own reactive state — three bounded `History` ring buffers
feeding `Plot` panels, and a sorted `List<ProcessRow>`/`FlatProcessTreeSource` feeding a
`TabBar` with a flat `Table` and a parent/child `Tree`. A perpetual `AnimationClock`
oscillator drives a small heartbeat indicator, visually proving the UI never stalls. Building
this already surfaced a real core-framework bug (Task 1) — the animation tick loop never
drained `Shared<T>`, so a background-thread-only feed would never repaint.

**Tech Stack:** C++26 (P2996 reflection), Meson, doctest, `/proc` filesystem (Linux-only),
`std::jthread`/`std::stop_token`.

## Global Constraints

- `cpp_std=c++26` (root `meson.build`) — `std::jthread`/`std::stop_token` are available with
  no extra Meson dependency; no `Threads::Threads`/`-pthread` wiring needed on this toolchain.
- Every real window uses `.decoration = prism::DecorationMode::Custom` explicitly — relying
  on the default has caused a chromeless, uncloseable window before.
- Every example/test `.cpp`/`.hpp` file that touches `prism::` types carries the standard
  boilerplate block at the top (copy verbatim, do not modify):
  ```cpp
  namespace prism::core {} namespace prism::render {} namespace prism::input {}
  namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
  namespace prism {
  using namespace core; using namespace render; using namespace input;
  using namespace ui; using namespace app; using namespace plot;
  }
  ```
- doctest test files start with `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` +
  `#include <doctest.h>`, then `TEST_CASE("description")` blocks.
- Read-only app — no kill/renice, no destructive `/proc` writes.
- TDD per project workflow rules: write the failing test first, confirm it fails, then
  implement. Pure-logic functions (parsing, sorting, tree-building) are unit-testable against
  fixture strings — no live-system dependency in tests. The two `/proc`-reading poller loops
  are the one deliberate exception (Task 6): they are thin imperative shells around already-
  tested pure functions, reading real OS state that cannot be fixture-driven — verified by
  running the real app (see Task 7/8 completion), not by a unit test.
- Run the full test suite (`meson test -C builddir`) before any commit that isn't itself a
  single failing-test-first step; read the actual pass/fail count.

---

## File Structure

| File | Responsibility |
|---|---|
| `include/prism/app/model_app.hpp` (modify) | Core fix: drain `Shared<T>` from the animation tick path |
| `tests/test_model_app.cpp` (modify) | Regression test for the fix |
| `examples/proc_metrics.hpp` (create) | `SystemSample`/`ProcessInfo` data types, pure parsers, `History`, `sort_by`, the two poller loops |
| `tests/test_proc_metrics.cpp` (create) | Fixture tests for all pure functions in `proc_metrics.hpp` |
| `examples/process_tree_source.hpp` (create) | `build_process_tree_index` (pure) + `FlatProcessTreeSource` (`TreeStorage`-conforming adapter) |
| `tests/test_process_tree_source.cpp` (create) | Hierarchy-building tests, including orphan handling |
| `examples/model_system_monitor.cpp` (create) | `SystemMonitor` model, `view()`, `main()` (interactive + headless-SVG branches) |
| `examples/meson.build` (modify) | Register the new executable + SVG `custom_target` |
| `tests/meson.build` (modify) | Register `test_proc_metrics`, `test_process_tree_source` |

---

### Task 1: Fix `Shared<T>` drain on the animation tick path

**Files:**
- Modify: `include/prism/app/model_app.hpp:115-129`
- Test: `tests/test_model_app.cpp` (append)

**Interfaces:**
- Consumes: `WindowRegistry::for_each(Fn&&)` (`include/prism/app/window_registry.hpp:49-51`,
  already exists, iterates **all** entries unconditionally), `WidgetTree::drain_shared()`
  (`include/prism/app/widget_tree.hpp:655-658`, already exists).
- Produces: every later task relies on this — a `Shared<T>` set from a background thread must
  become visible on the UI thread purely via the animation tick loop, with zero input events.

The bug: `schedule_tick`'s per-frame continuation calls `anim_clock.tick(...)` and
`publish_dirty()` but never `drain_shared()` — that only happens from the input-dispatch path
(`model_app.hpp:184`, `entry->tree->drain_shared()`), which never fires without mouse/keyboard
activity. A background-thread-only feed is invisible to the UI until fixed.

- [ ] **Step 1: Write the failing regression test**

Open `tests/test_model_app.cpp`. Add three includes to the top `#include` block (after the
existing ones, before the blank line that precedes the boilerplate namespace block):

```cpp
#include <atomic>
#include <chrono>
#include <thread>
```

Then append this `TEST_CASE` at the end of the file:

```cpp
TEST_CASE("model_app drains Shared<T> via the animation tick path with zero input events") {
    struct TickModel {
        prism::Shared<int> data{0};
        void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(data); }
    };

    TickModel model;
    std::atomic<bool> drained{false};
    int observed = -1;
    auto conn = model.data.on_change().connect([&](const int& v) {
        observed = v;
        drained.store(true, std::memory_order_release);
    });

    // Fires WindowClose only after `drained` flips true (or a 2s safety deadline), so the
    // loop cannot exit before the tick path has had a real chance to drain — no fixed-count
    // event replay that could race the animation clock's own scheduling.
    struct WaitForDrainBackend final : public prism::BackendBase {
        prism::HeadlessWindow window_{0, {}};
        std::atomic<bool>& drained_ref;
        explicit WaitForDrainBackend(std::atomic<bool>& d) : drained_ref(d) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!drained_ref.load(std::memory_order_acquire)
                   && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    auto backend = prism::Backend{std::make_unique<WaitForDrainBackend>(drained)};
    auto& window = backend.create_window({.width = 200, .height = 200});

    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        // A single, self-terminating tick (returns false immediately) is enough to
        // exercise drain_shared() from the tick path. Deliberately NOT a perpetual
        // source: stdexec::run_loop::run()'s shutdown drain phase
        // (`while (execute_all() || task_count > 0);`) only terminates once the queue
        // stops refilling — a tick source that keeps re-scheduling itself forever
        // livelocks that phase, since schedule_tick unconditionally re-enqueues itself
        // while anim_clock.active() stays true. Confirmed by reading
        // subprojects/stdexec/include/stdexec/__detail/__run_loop.hpp directly. Task 1b
        // fixes this generally (AnimationClock::clear() called from WindowClose); this
        // test predates that fix and sidesteps the issue by never staying active past
        // the one tick it needs.
        ctx.clock().add([](prism::AnimationClock::time_point) { return false; });

        std::thread writer([&model] { model.data.set(7); });
        writer.join();
    });

    REQUIRE(drained.load());
    CHECK(observed == 7);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir model_app -v`
Expected: FAIL — `REQUIRE(drained.load())` fails (stays `false`), because `schedule_tick`
never calls `drain_shared()`. The test takes ~2 seconds to fail (the safety deadline), not a
hang — if it hangs instead, stop and re-check `WaitForDrainBackend`'s deadline logic before
proceeding.

- [ ] **Step 3: Fix the tick loop**

In `include/prism/app/model_app.hpp`, change lines 115-129 from:

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
                publish_dirty();
                if (anim_clock.active())
                    schedule_tick();
            })
        );
    };
```

to:

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
                registry.for_each([&](WindowId, WindowRegistry::Entry& entry) {
                    entry.tree->drain_shared();
                });
                publish_dirty();
                if (anim_clock.active())
                    schedule_tick();
            })
        );
    };
```

(Matches the existing input-dispatch path's own ordering at lines 184-189: drain before
publish, so any `Shared<T>`-driven state changes are folded into dirty-marking before
`publish_dirty()` checks `any_dirty()`.)

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir model_app -v`
Expected: PASS, and fast (well under the 2s deadline — the very first tick should drain it).

- [ ] **Step 5: Run the full suite and commit**

Run: `meson test -C builddir` — read the actual pass/fail summary line, confirm 0 failures.

```bash
git add include/prism/app/model_app.hpp tests/test_model_app.cpp
git commit -m "fix(app): drain Shared<T> from the animation tick path, not just input dispatch

A Shared<T> updated purely by a background thread, with no mouse/keyboard
activity, never repainted -- schedule_tick drove anim_clock.tick() and
publish_dirty() every frame but never drain_shared(). Found while building
the system monitor example (a background-thread-fed live app)."
```

---

### Task 1b: `AnimationClock::clear()` + drop all animations on primary `WindowClose`

**Files:**
- Modify: `include/prism/ui/animation.hpp`
- Modify: `include/prism/app/model_app.hpp:138-140`
- Test: `tests/test_model_app.cpp` (append)

**Interfaces:**
- Produces: `AnimationClock::clear()`. Task 8's perpetual heartbeat depends on this landing
  first — without it, closing the real app with the heartbeat still running livelocks on
  shutdown (see Task 1's investigation above for the root cause).

Found while building Task 1's regression test: a tick source that never stops re-scheduling
itself (`anim_clock.active()` permanently true) livelocks `stdexec::run_loop::run()`'s
shutdown drain phase (`while (execute_all() || task_count > 0);` never sees an empty queue,
since `schedule_tick`'s continuation keeps refilling it). `AnimationClock` has no way to force
all registered tick sources to stop. This generalizes beyond our heartbeat — *any* long-running
or perpetual animation still in flight when the primary window closes hits the same hang.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_model_app.cpp`:

```cpp
TEST_CASE("model_app shuts down cleanly with a perpetual animation still active") {
    struct EmptyModel {
        void view(prism::WidgetTree::ViewBuilder&) {}
    };
    EmptyModel model;

    struct ImmediateCloseBackend final : public prism::BackendBase {
        prism::HeadlessWindow window_{0, {}};
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    auto backend = prism::Backend{std::make_unique<ImmediateCloseBackend>()};
    auto& window = backend.create_window({.width = 200, .height = 200});

    // Perpetual tick source, deliberately never returning false -- this is exactly the
    // shape that livelocked stdexec::run_loop's shutdown drain before the fix. If
    // model_app returns at all, the fix works; if this test hangs, it doesn't.
    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        ctx.clock().add([](prism::AnimationClock::time_point) { return true; });
    });

    CHECK(true); // reaching this line at all is the assertion -- see comment above
}
```

- [ ] **Step 2: Run test to verify it fails (hangs)**

Run: `timeout 10 meson test -C builddir model_app -v; echo "exit: $?"`
Expected: the `timeout` wrapper kills the test after 10s (exit code 124) — this test cannot
"fail" cleanly before the fix, it hangs. That hang **is** the expected RED state; do not
increase the timeout to make it "pass" some other way.

- [ ] **Step 3: Add `AnimationClock::clear()`**

In `include/prism/ui/animation.hpp`, inside `class AnimationClock` (the same class documented
in the Task 1 investigation, alongside `add`/`remove`/`tick`/`active`), add:

```cpp
    void clear() { animations_.clear(); }
```

- [ ] **Step 4: Call it from the primary `WindowClose` handler**

In `include/prism/app/model_app.hpp`, change (lines 138-140):

```cpp
                    if (std::holds_alternative<WindowClose>(ev)) {
                        if (wid == primary_id) {
                            loop.finish();
```

to:

```cpp
                    if (std::holds_alternative<WindowClose>(ev)) {
                        if (wid == primary_id) {
                            anim_clock.clear();
                            loop.finish();
```

(`anim_clock` is already in scope here — it's captured by the same `[&, ev, wid]` lambda that
already calls `loop.finish()` two lines below.)

- [ ] **Step 5: Run test to verify it passes**

Run: `timeout 10 meson test -C builddir model_app -v; echo "exit: $?"`
Expected: PASS, completes in well under 10s (no timeout kill, exit code 0).

- [ ] **Step 6: Run the full suite and commit**

Run: `meson test -C builddir` — confirm 0 failures, and confirm no test in the suite takes
anywhere near its timeout (a lingering livelock elsewhere would show up as a slow test).

```bash
git add include/prism/ui/animation.hpp include/prism/app/model_app.hpp tests/test_model_app.cpp
git commit -m "fix(animation): stop all tick sources on primary WindowClose

A perpetual (or merely still-active) AnimationClock tick source keeps
schedule_tick re-enqueuing itself forever, which livelocks
stdexec::run_loop::run()'s shutdown drain phase -- it only returns once
the queue empties, and a self-rescheduling tick never lets that happen.
Found while testing the Task 1 drain fix with a perpetual tick source."
```

---

### Task 2: `proc_metrics.hpp` — system sample parsing (pure, fixture-tested)

**Files:**
- Create: `examples/proc_metrics.hpp`
- Create: `tests/test_proc_metrics.cpp`
- Modify: `tests/meson.build`

**Interfaces:**
- Produces: `SystemSample`, `StatTotals`, `NetTotals`, `SystemTotals`, `SystemSampleResult`,
  `parse_stat_totals(string_view) -> StatTotals`, `parse_net_totals(string_view) -> NetTotals`,
  `parse_meminfo(string_view) -> MemInfo`, `cpu_percent_from_totals(StatTotals, StatTotals) -> float`,
  `parse_system_sample(stat, meminfo, net_dev, prev, dt_seconds) -> SystemSampleResult`,
  `History` (used by Task 5).

- [ ] **Step 1: Write the failing tests**

Create `tests/test_proc_metrics.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/proc_metrics.hpp"

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

TEST_CASE("parse_stat_totals reads the aggregate cpu line, skipping per-core lines") {
    std::string stat =
        "cpu  100 0 50 850 0 0 0 0 0 0\n"
        "cpu0 50 0 25 425 0 0 0 0 0 0\n"
        "cpu1 50 0 25 425 0 0 0 0 0 0\n"
        "intr 12345 0 0\n";
    StatTotals t = parse_stat_totals(stat);
    CHECK(t.total == 1000);
    CHECK(t.idle == 850);
}

TEST_CASE("cpu_percent_from_totals computes delta-based percentage") {
    StatTotals prev{.total = 1000, .idle = 850};
    StatTotals cur{.total = 2000, .idle = 1700};
    CHECK(cpu_percent_from_totals(prev, cur) == doctest::Approx(15.0f));
}

TEST_CASE("cpu_percent_from_totals returns 0 when there is no prior sample") {
    StatTotals prev{.total = 0, .idle = 0};
    StatTotals cur{.total = 0, .idle = 0};
    CHECK(cpu_percent_from_totals(prev, cur) == doctest::Approx(0.0f));
}

TEST_CASE("parse_meminfo reads MemTotal and MemAvailable in kB") {
    std::string meminfo =
        "MemTotal:       16384000 kB\n"
        "MemFree:         2048000 kB\n"
        "MemAvailable:    8192000 kB\n"
        "Buffers:          512000 kB\n";
    MemInfo m = parse_meminfo(meminfo);
    CHECK(m.total_kb == doctest::Approx(16384000.0));
    CHECK(m.available_kb == doctest::Approx(8192000.0));
}

TEST_CASE("parse_net_totals sums non-loopback interfaces, skipping lo") {
    std::string net_dev =
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "    lo:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0\n"
        "  eth0: 1048576    1000    0    0    0     0          0         0   524288     500    0    0    0     0       0          0\n";
    NetTotals n = parse_net_totals(net_dev);
    CHECK(n.rx_bytes == doctest::Approx(1048576.0));
    CHECK(n.tx_bytes == doctest::Approx(524288.0));
}

TEST_CASE("parse_system_sample combines stat/meminfo/net_dev into one sample") {
    std::string stat_prev = "cpu  100 0 50 850 0 0 0 0 0 0\n";
    std::string stat_cur = "cpu  200 0 100 1700 0 0 0 0 0 0\n";
    std::string meminfo =
        "MemTotal:       16384000 kB\nMemAvailable:    8192000 kB\n";
    std::string net_prev =
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "  eth0: 1048576    1000    0    0    0     0          0         0   524288     500    0    0    0     0       0          0\n";
    std::string net_cur =
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "  eth0: 2097152    2000    0    0    0     0          0         0  1048576    1000    0    0    0     0       0          0\n";

    SystemTotals prev;
    prev.cpu = parse_stat_totals(stat_prev);
    prev.net = parse_net_totals(net_prev);

    auto result = parse_system_sample(stat_cur, meminfo, net_cur, prev, 1.0);

    CHECK(result.sample.cpu_percent == doctest::Approx(15.0f));
    CHECK(result.sample.mem_total_mb == doctest::Approx(16000.0));
    CHECK(result.sample.mem_used_mb == doctest::Approx(8000.0));
    CHECK(result.sample.net_rx_kbps == doctest::Approx(1024.0));
    CHECK(result.sample.net_tx_kbps == doctest::Approx(512.0));
}

TEST_CASE("History caps at max_points, dropping the oldest") {
    History h;
    for (int i = 0; i < static_cast<int>(History::max_points) + 10; ++i)
        h.push(static_cast<float>(i));
    CHECK(h.values.size() == History::max_points);
    CHECK(h.values.front() == doctest::Approx(10.0f));
    CHECK(h.values.back() == doctest::Approx(129.0f));
}
```

- [ ] **Step 2: Create the (empty-bodied) header so the test fails on assertions, not compilation**

Create `examples/proc_metrics.hpp`:

```cpp
#pragma once

#include <cstdlib>
#include <deque>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

struct SystemSample {
    float cpu_percent = 0.f;
    double mem_used_mb = 0.0;
    double mem_total_mb = 0.0;
    double net_rx_kbps = 0.0;
    double net_tx_kbps = 0.0;
};

struct StatTotals {
    long total = 0;
    long idle = 0;
};

struct NetTotals {
    double rx_bytes = 0.0;
    double tx_bytes = 0.0;
};

struct SystemTotals {
    StatTotals cpu{};
    NetTotals net{};
};

struct SystemSampleResult {
    SystemSample sample;
    SystemTotals totals;
};

struct MemInfo {
    double total_kb = 0.0;
    double available_kb = 0.0;
};

struct History {
    static constexpr size_t max_points = 120;
    std::deque<float> values;

    void push(float v) {
        values.push_back(v);
        if (values.size() > max_points) values.pop_front();
    }
};

inline StatTotals parse_stat_totals(std::string_view) { return {}; }
inline float cpu_percent_from_totals(StatTotals, StatTotals) { return 0.f; }
inline MemInfo parse_meminfo(std::string_view) { return {}; }
inline NetTotals parse_net_totals(std::string_view) { return {}; }
inline SystemSampleResult parse_system_sample(std::string_view, std::string_view,
                                               std::string_view, const SystemTotals&, double) {
    return {};
}
```

Add to `tests/meson.build`'s `headless_tests` dict (alongside the existing `'file_tree_source'`
entry): `'proc_metrics' : files('test_proc_metrics.cpp'),`

- [ ] **Step 3: Run tests to verify they fail on assertions**

Run: `meson setup builddir --reconfigure && meson test -C builddir proc_metrics -v`
Expected: builds successfully, then FAILs on the `CHECK`/`REQUIRE` assertions (stub functions
return default-constructed `{}`).

- [ ] **Step 4: Implement the real parsing logic**

Replace the five stub function bodies in `examples/proc_metrics.hpp` with:

```cpp
inline StatTotals parse_stat_totals(std::string_view stat_text) {
    std::istringstream in{std::string(stat_text)};
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("cpu", 0) != 0) continue;
        if (line.size() <= 3 || !std::isspace(static_cast<unsigned char>(line[3]))) continue;
        std::istringstream fields{line.substr(3)};
        std::vector<long> values;
        long v;
        while (fields >> v) values.push_back(v);
        StatTotals totals;
        for (long value : values) totals.total += value;
        if (values.size() > 4) totals.idle = values[3] + values[4];
        else if (values.size() > 3) totals.idle = values[3];
        return totals;
    }
    return {};
}

inline float cpu_percent_from_totals(StatTotals prev, StatTotals cur) {
    long dt = cur.total - prev.total;
    if (dt <= 0) return 0.f;
    long du = (cur.total - cur.idle) - (prev.total - prev.idle);
    return 100.f * static_cast<float>(du) / static_cast<float>(dt);
}

inline MemInfo parse_meminfo(std::string_view meminfo_text) {
    std::istringstream in{std::string(meminfo_text)};
    std::string key, unit;
    double value = 0.0;
    MemInfo info;
    while (in >> key >> value >> unit) {
        if (key == "MemTotal:") info.total_kb = value;
        else if (key == "MemAvailable:") info.available_kb = value;
    }
    return info;
}

inline NetTotals parse_net_totals(std::string_view net_dev_text) {
    std::istringstream in{std::string(net_dev_text)};
    std::string line;
    NetTotals totals;
    std::getline(in, line); // header line 1
    std::getline(in, line); // header line 2
    while (std::getline(in, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        name.erase(0, name.find_first_not_of(" \t"));
        if (name == "lo") continue;
        std::istringstream fields{line.substr(colon + 1)};
        std::vector<double> values;
        double v;
        while (fields >> v) values.push_back(v);
        if (values.size() > 8) {
            totals.rx_bytes += values[0];
            totals.tx_bytes += values[8];
        }
    }
    return totals;
}

inline SystemSampleResult parse_system_sample(std::string_view stat_text,
                                               std::string_view meminfo_text,
                                               std::string_view net_dev_text,
                                               const SystemTotals& prev,
                                               double dt_seconds) {
    SystemTotals cur;
    cur.cpu = parse_stat_totals(stat_text);
    cur.net = parse_net_totals(net_dev_text);
    MemInfo mem = parse_meminfo(meminfo_text);

    SystemSample sample;
    sample.cpu_percent = cpu_percent_from_totals(prev.cpu, cur.cpu);
    sample.mem_total_mb = mem.total_kb / 1024.0;
    sample.mem_used_mb = (mem.total_kb - mem.available_kb) / 1024.0;
    if (dt_seconds > 0.0) {
        sample.net_rx_kbps = (cur.net.rx_bytes - prev.net.rx_bytes) / 1024.0 / dt_seconds;
        sample.net_tx_kbps = (cur.net.tx_bytes - prev.net.tx_bytes) / 1024.0 / dt_seconds;
    }
    return {sample, cur};
}
```

`History` already has its real (non-stub) implementation from Step 2 — no change needed there.

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson test -C builddir proc_metrics -v`
Expected: PASS, all `TEST_CASE`s green.

- [ ] **Step 6: Run the full suite and commit**

Run: `meson test -C builddir` — confirm 0 failures.

```bash
git add examples/proc_metrics.hpp tests/test_proc_metrics.cpp tests/meson.build
git commit -m "feat(examples): pure /proc system-sample parsing for the system monitor example"
```

---

### Task 3: `proc_metrics.hpp` — process parsing, sort, `SortKey` (pure, fixture-tested)

**Files:**
- Modify: `examples/proc_metrics.hpp`
- Modify: `tests/test_proc_metrics.cpp`

**Interfaces:**
- Consumes: nothing from Task 2 directly (independent data), but lands in the same file.
- Produces: `ProcessInfo`, `SortKey`, `parse_process_stat(string_view) -> ProcessInfo`,
  `parse_status_vmrss_kb(string_view) -> long`,
  `parse_process_entry(stat_text, status_text, prev_total_jiffies, dt_seconds, total_mem_kb) -> ProcessInfo`,
  `sort_by(vector<ProcessInfo>, SortKey) -> vector<ProcessInfo>`. Task 4 (`process_tree_source.hpp`)
  and Task 5/7 (the app) consume `ProcessInfo`/`SortKey`/`sort_by` directly.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_proc_metrics.cpp`:

```cpp
TEST_CASE("parse_process_stat extracts pid/ppid/state/name, handling parens in the name") {
    // comm field is "(my proc (2))" -- deliberately contains parens and a space, which is
    // why the parser must match the LAST ')' rather than the first.
    std::string stat_line =
        "1234 (my proc (2)) S 1 1234 1234 0 -1 4194304 100 0 0 0 500 200 0 0 20 0 1 0 12345 123456789 1234";
    ProcessInfo info = parse_process_stat(stat_line);
    CHECK(info.pid == 1234);
    CHECK(info.ppid == 1);
    CHECK(info.state == 'S');
    CHECK(info.name == "my proc (2)");
    CHECK(info.total_jiffies == 700); // utime(500) + stime(200)
}

TEST_CASE("parse_status_vmrss_kb reads the VmRSS line") {
    std::string status =
        "Name:\tmy proc\nState:\tS (sleeping)\nVmRSS:\t   4096 kB\nThreads:\t1\n";
    CHECK(parse_status_vmrss_kb(status) == 4096);
}

TEST_CASE("parse_process_entry computes mem_percent and cpu_percent from prev jiffies") {
    std::string stat_line =
        "1234 (worker) S 1 1234 1234 0 -1 4194304 100 0 0 0 500 200 0 0 20 0 1 0 12345 123456789 1234";
    std::string status = "Name:\tworker\nVmRSS:\t   8192 kB\n";

    // First sample seen (prev_total_jiffies < 0): cpu_percent must be 0, not a bogus delta.
    ProcessInfo first = parse_process_entry(stat_line, status, -1, 1.0, 16384000.0);
    CHECK(first.cpu_percent == doctest::Approx(0.0f));
    CHECK(first.mem_percent == doctest::Approx(0.05f)); // 8192 / 16384000 * 100

    // Second sample: total_jiffies went from 400 to 700 (delta 300 ticks) over 1s @ 100 ticks/s.
    ProcessInfo second = parse_process_entry(stat_line, status, 400, 1.0, 16384000.0);
    CHECK(second.cpu_percent == doctest::Approx(300.0f)); // (300/100)/1.0*100
}

TEST_CASE("sort_by orders by each key without mutating the input") {
    std::vector<ProcessInfo> input;
    input.push_back(ProcessInfo{.pid = 3, .name = "c", .cpu_percent = 10.f, .mem_percent = 5.f});
    input.push_back(ProcessInfo{.pid = 1, .name = "a", .cpu_percent = 30.f, .mem_percent = 1.f});
    input.push_back(ProcessInfo{.pid = 2, .name = "b", .cpu_percent = 20.f, .mem_percent = 9.f});

    auto by_cpu = sort_by(input, SortKey::CpuPercent);
    CHECK(by_cpu[0].pid == 1);
    CHECK(by_cpu[1].pid == 2);
    CHECK(by_cpu[2].pid == 3);

    auto by_pid = sort_by(input, SortKey::Pid);
    CHECK(by_pid[0].pid == 1);
    CHECK(by_pid[1].pid == 2);
    CHECK(by_pid[2].pid == 3);

    // Original vector must be untouched -- sort_by takes and returns by value.
    CHECK(input[0].pid == 3);
}
```

- [ ] **Step 2: Add stub declarations**

In `examples/proc_metrics.hpp`, add after the `History` struct (before the `parse_stat_totals`
declarations added in Task 2):

```cpp
struct ProcessInfo {
    int pid = 0;
    int ppid = 0;
    std::string name;
    char state = '?';
    float cpu_percent = 0.f;
    float mem_percent = 0.f;
    long rss_kb = 0;
    long total_jiffies = 0;
};

enum class SortKey { CpuPercent, MemPercent, Pid, Name };

inline ProcessInfo parse_process_stat(std::string_view) { return {}; }
inline long parse_status_vmrss_kb(std::string_view) { return 0; }
inline ProcessInfo parse_process_entry(std::string_view, std::string_view, long, double, double) {
    return {};
}
inline std::vector<ProcessInfo> sort_by(std::vector<ProcessInfo> list, SortKey) { return list; }
```

- [ ] **Step 3: Run tests to verify they fail on assertions**

Run: `meson test -C builddir proc_metrics -v`
Expected: builds, FAILs on the new process-parsing/sort assertions.

- [ ] **Step 4: Implement the real logic**

Replace the four stub bodies with:

```cpp
inline ProcessInfo parse_process_stat(std::string_view stat_text) {
    ProcessInfo info;
    std::string text(stat_text);
    auto open = text.find('(');
    auto close = text.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open) return info;

    info.pid = std::atoi(text.substr(0, open).c_str());
    info.name = text.substr(open + 1, close - open - 1);

    std::istringstream rest{text.substr(close + 1)};
    std::vector<std::string> fields;
    std::string tok;
    while (rest >> tok) fields.push_back(tok);
    if (fields.size() < 13) return info;

    info.state = fields[0].empty() ? '?' : fields[0][0];
    info.ppid = std::atoi(fields[1].c_str());
    info.total_jiffies = std::atol(fields[11].c_str()) + std::atol(fields[12].c_str());
    return info;
}

inline long parse_status_vmrss_kb(std::string_view status_text) {
    std::istringstream in{std::string(status_text)};
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream fields{line.substr(6)};
            long kb = 0;
            fields >> kb;
            return kb;
        }
    }
    return 0;
}

inline ProcessInfo parse_process_entry(std::string_view stat_text, std::string_view status_text,
                                        long prev_total_jiffies, double dt_seconds,
                                        double total_mem_kb) {
    ProcessInfo info = parse_process_stat(stat_text);
    info.rss_kb = parse_status_vmrss_kb(status_text);
    if (total_mem_kb > 0.0)
        info.mem_percent = static_cast<float>(100.0 * static_cast<double>(info.rss_kb) / total_mem_kb);

    constexpr double clk_tck = 100.0; // sysconf(_SC_CLK_TCK) on virtually all Linux systems
    if (dt_seconds > 0.0 && prev_total_jiffies >= 0) {
        double delta_ticks = static_cast<double>(info.total_jiffies - prev_total_jiffies);
        float pct = static_cast<float>(100.0 * (delta_ticks / clk_tck) / dt_seconds);
        info.cpu_percent = pct < 0.f ? 0.f : pct;
    }
    return info;
}

inline std::vector<ProcessInfo> sort_by(std::vector<ProcessInfo> list, SortKey key) {
    std::sort(list.begin(), list.end(), [key](const ProcessInfo& a, const ProcessInfo& b) {
        switch (key) {
            case SortKey::CpuPercent: return a.cpu_percent > b.cpu_percent;
            case SortKey::MemPercent: return a.mem_percent > b.mem_percent;
            case SortKey::Pid:        return a.pid < b.pid;
            case SortKey::Name:       return a.name < b.name;
        }
        return false;
    });
    return list;
}
```

Add `#include <algorithm>` to the top include block of `examples/proc_metrics.hpp`.

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson test -C builddir proc_metrics -v`
Expected: PASS.

- [ ] **Step 6: Run the full suite and commit**

Run: `meson test -C builddir` — confirm 0 failures.

```bash
git add examples/proc_metrics.hpp tests/test_proc_metrics.cpp
git commit -m "feat(examples): pure process-list parsing, sort_by, and SortKey"
```

---

### Task 4: `process_tree_source.hpp` — hierarchy builder (pure, tested) + `TreeSource` adapter

**Files:**
- Create: `examples/process_tree_source.hpp`
- Create: `tests/test_process_tree_source.cpp`
- Modify: `tests/meson.build`

**Interfaces:**
- Consumes: `ProcessInfo` (Task 3) — this file `#include`s `proc_metrics.hpp`.
- Produces: `ProcessTreeIndex`, `build_process_tree_index(const vector<ProcessInfo>&) -> ProcessTreeIndex`,
  `FlatProcessTreeSource` (methods: `update(vector<ProcessInfo>)`, `root_count()`, `root_at(size_t)`,
  `child_count(uint64_t)`, `child_at(uint64_t, size_t)`, `label(uint64_t) -> std::string`,
  `has_children(uint64_t)` — satisfies `prism::TreeStorage` structurally, no prism include needed).
  Task 5/7 (the app) wraps it via `prism::wrap_tree_storage(source)`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_process_tree_source.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/process_tree_source.hpp"

TEST_CASE("build_process_tree_index groups children under their parent pid") {
    std::vector<ProcessInfo> procs;
    procs.push_back(ProcessInfo{.pid = 1, .ppid = 0, .name = "init"});
    procs.push_back(ProcessInfo{.pid = 2, .ppid = 1, .name = "shell"});
    procs.push_back(ProcessInfo{.pid = 3, .ppid = 1, .name = "editor"});
    procs.push_back(ProcessInfo{.pid = 4, .ppid = 2, .name = "grep"});

    ProcessTreeIndex idx = build_process_tree_index(procs);

    REQUIRE(idx.roots.size() == 1);
    CHECK(idx.roots[0] == 1);
    REQUIRE(idx.children_by_ppid.at(1).size() == 2);
    CHECK(idx.children_by_ppid.at(2) == std::vector<int>{4});
}

TEST_CASE("build_process_tree_index reparents a process whose ppid is not in the list to root") {
    std::vector<ProcessInfo> procs;
    procs.push_back(ProcessInfo{.pid = 1, .ppid = 0, .name = "init"});
    procs.push_back(ProcessInfo{.pid = 5, .ppid = 999, .name = "orphan"}); // 999 not present

    ProcessTreeIndex idx = build_process_tree_index(procs);

    CHECK(idx.roots.size() == 2);
    CHECK(std::find(idx.roots.begin(), idx.roots.end(), 5) != idx.roots.end());
}

TEST_CASE("build_process_tree_index treats a self-parented pid as a root, not a self-loop") {
    std::vector<ProcessInfo> procs;
    procs.push_back(ProcessInfo{.pid = 1, .ppid = 1, .name = "weird-init"});

    ProcessTreeIndex idx = build_process_tree_index(procs);

    CHECK(idx.roots.size() == 1);
    CHECK(idx.roots[0] == 1);
    CHECK(idx.children_by_ppid.empty());
}

TEST_CASE("FlatProcessTreeSource exposes the index through the TreeStorage-shaped methods") {
    FlatProcessTreeSource source;
    std::vector<ProcessInfo> procs;
    procs.push_back(ProcessInfo{.pid = 1, .ppid = 0, .name = "init", .cpu_percent = 0.5f});
    procs.push_back(ProcessInfo{.pid = 2, .ppid = 1, .name = "shell", .cpu_percent = 1.5f});
    source.update(procs);

    REQUIRE(source.root_count() == 1);
    uint64_t root = source.root_at(0);
    CHECK(root == 1);
    REQUIRE(source.child_count(root) == 1);
    CHECK(source.child_at(root, 0) == 2);
    CHECK(source.has_children(root));
    CHECK_FALSE(source.has_children(2));
    CHECK(source.label(root).find("init") != std::string::npos);
}
```

- [ ] **Step 2: Add stub implementations**

Create `examples/process_tree_source.hpp`:

```cpp
#pragma once

#include "proc_metrics.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

struct ProcessTreeIndex {
    std::vector<int> roots;
    std::unordered_map<int, std::vector<int>> children_by_ppid;
};

inline ProcessTreeIndex build_process_tree_index(const std::vector<ProcessInfo>&) { return {}; }

class FlatProcessTreeSource {
public:
    void update(std::vector<ProcessInfo> processes) { processes_ = std::move(processes); }

    size_t root_count() const { return 0; }
    uint64_t root_at(size_t) const { return 0; }
    size_t child_count(uint64_t) const { return 0; }
    uint64_t child_at(uint64_t, size_t) const { return 0; }
    std::string label(uint64_t) const { return ""; }
    bool has_children(uint64_t) const { return false; }

private:
    std::vector<ProcessInfo> processes_;
};
```

Add to `tests/meson.build`'s `headless_tests` dict: `'process_tree_source' : files('test_process_tree_source.cpp'),`

- [ ] **Step 3: Run tests to verify they fail**

Run: `meson setup builddir --reconfigure && meson test -C builddir process_tree_source -v`
Expected: builds, FAILs on assertions.

- [ ] **Step 4: Implement the real logic**

Replace the stub `build_process_tree_index` and `FlatProcessTreeSource` with:

```cpp
inline ProcessTreeIndex build_process_tree_index(const std::vector<ProcessInfo>& processes) {
    ProcessTreeIndex index;
    std::set<int> pids;
    for (const auto& p : processes) pids.insert(p.pid);
    for (const auto& p : processes) {
        if (p.ppid != p.pid && pids.contains(p.ppid))
            index.children_by_ppid[p.ppid].push_back(p.pid);
        else
            index.roots.push_back(p.pid);
    }
    return index;
}

class FlatProcessTreeSource {
public:
    void update(std::vector<ProcessInfo> processes) {
        processes_ = std::move(processes);
        index_ = build_process_tree_index(processes_);
    }

    size_t root_count() const { return index_.roots.size(); }
    uint64_t root_at(size_t i) const { return static_cast<uint64_t>(index_.roots[i]); }

    size_t child_count(uint64_t id) const {
        auto it = index_.children_by_ppid.find(static_cast<int>(id));
        return it != index_.children_by_ppid.end() ? it->second.size() : 0;
    }

    uint64_t child_at(uint64_t id, size_t i) const {
        return static_cast<uint64_t>(index_.children_by_ppid.at(static_cast<int>(id))[i]);
    }

    std::string label(uint64_t id) const {
        const ProcessInfo* p = find(static_cast<int>(id));
        if (!p) return "?";
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s (%d) %.1f%%", p->name.c_str(), p->pid, p->cpu_percent);
        return buf;
    }

    bool has_children(uint64_t id) const { return child_count(id) > 0; }

private:
    const ProcessInfo* find(int pid) const {
        for (const auto& p : processes_) if (p.pid == pid) return &p;
        return nullptr;
    }

    std::vector<ProcessInfo> processes_;
    ProcessTreeIndex index_;
};
```

Add `#include <cstdio>` to the top include block for `std::snprintf`.

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson test -C builddir process_tree_source -v`
Expected: PASS.

- [ ] **Step 6: Run the full suite and commit**

Run: `meson test -C builddir` — confirm 0 failures.

```bash
git add examples/process_tree_source.hpp tests/test_process_tree_source.cpp tests/meson.build
git commit -m "feat(examples): ppid-hierarchy builder and TreeStorage adapter for process list"
```

---

### Task 5: `model_system_monitor.cpp` — static UI skeleton + SVG snapshot

**Files:**
- Create: `examples/model_system_monitor.cpp`
- Modify: `examples/meson.build`

**Interfaces:**
- Consumes: `SystemSample`, `ProcessInfo`, `SortKey`, `sort_by`, `History` (Task 2/3),
  `FlatProcessTreeSource` (Task 4).
- Produces: `SystemMonitor` model struct, `ProcessRow`, `seed_demo_data()`. Task 6 adds the
  pollers; Task 7 wires them into this file's `main()`; Task 8 adds the heartbeat animation.

This task proves the widget composition (Plot × 3, resizable split, Dropdown, `TabBar` with
`Table`/`Tree`, a static heartbeat shape) builds and renders correctly, with hardcoded demo
data — before adding any threading risk.

- [ ] **Step 1: Write the model, seeded with static data, and a headless-SVG entry point**

Create `examples/model_system_monitor.cpp`:

```cpp
#include <prism/prism.hpp>
#include "proc_metrics.hpp"
#include "process_tree_source.hpp"
#include "showcase/showcase_common.hpp"

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

#if __cpp_impl_reflection

struct ProcessRow {
    prism::Field<int> pid{0};
    prism::Field<std::string> name{""};
    prism::Field<float> cpu_percent{0.f};
    prism::Field<float> mem_percent{0.f};
};

struct SystemMonitor {
    // Background-thread ingest points. Never placed via vb.widget() -- they are read only
    // through .observe(), which fires during drain via the void drain() opt-in below. A
    // harmless "not placed by view()" debug-build warning is expected for these two fields.
    prism::Shared<SystemSample> sys_sample{};
    prism::Shared<std::vector<ProcessInfo>> proc_list{};

    History cpu_history;
    History mem_history;
    History net_history;

    prism::plot::PlotModel cpu_plot;
    prism::plot::PlotModel mem_plot;
    prism::plot::PlotModel net_plot;

    prism::Field<SortKey> sort_key{SortKey::CpuPercent};
    prism::List<ProcessRow> table_rows;
    FlatProcessTreeSource tree_source;
    prism::TreeController tree_ctrl{prism::wrap_tree_storage(tree_source)};
    prism::Field<prism::TabBar<>> tabs;

    prism::Field<float> heartbeat_phase{0.f};

    static void rebuild_plot(prism::plot::PlotModel& plot, const History& h) {
        std::vector<double> xs(h.values.size());
        std::vector<double> ys(h.values.begin(), h.values.end());
        for (size_t i = 0; i < xs.size(); ++i) xs[i] = static_cast<double>(i);
        auto colors = prism::plot::default_series_colors(prism::default_theme());
        plot.clear_series();
        plot.add_series(prism::plot::XYData{std::move(xs), std::move(ys)},
                        prism::plot::SeriesStyle{colors[0], 2.f});
        plot.notify();
    }

    void ingest_system(const SystemSample& s) {
        cpu_history.push(s.cpu_percent);
        mem_history.push(static_cast<float>(s.mem_used_mb));
        net_history.push(static_cast<float>(s.net_rx_kbps));
        rebuild_plot(cpu_plot, cpu_history);
        rebuild_plot(mem_plot, mem_history);
        rebuild_plot(net_plot, net_history);
    }

    void ingest_processes(const std::vector<ProcessInfo>& processes) {
        auto sorted = sort_by(processes, sort_key.get());

        while (table_rows.size() > sorted.size())
            table_rows.erase(table_rows.size() - 1);
        for (size_t i = 0; i < sorted.size(); ++i) {
            ProcessRow row{.pid = {sorted[i].pid}, .name = {sorted[i].name},
                           .cpu_percent = {sorted[i].cpu_percent},
                           .mem_percent = {sorted[i].mem_percent}};
            if (i < table_rows.size()) table_rows.set(i, row);
            else table_rows.push_back(row);
        }

        tree_source.update(processes);
        tree_ctrl.refresh();
    }

    void seed_demo_data() {
        ingest_system(SystemSample{.cpu_percent = 12.5f, .mem_used_mb = 4096.0,
                                    .mem_total_mb = 16384.0, .net_rx_kbps = 128.0,
                                    .net_tx_kbps = 32.0});
        std::vector<ProcessInfo> demo;
        demo.push_back(ProcessInfo{.pid = 1, .ppid = 0, .name = "init",
                                    .cpu_percent = 0.1f, .mem_percent = 0.5f});
        demo.push_back(ProcessInfo{.pid = 42, .ppid = 1, .name = "prism_demo",
                                    .cpu_percent = 3.2f, .mem_percent = 1.8f});
        ingest_processes(demo);
    }

    void drain() {
        sys_sample.drain_notifications();
        proc_list.drain_notifications();
    }

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode& node) {
        auto& t = *node.theme;
        float size = 8.f + 4.f * std::sin(heartbeat_phase.get());
        dl.rounded_rect(
            prism::Rect{bounds.origin, prism::Size{prism::Width{size}, prism::Height{size}}},
            t.accent_hover, size * 0.5f);
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.canvas(cpu_plot)
                .depends_on(cpu_plot.x_range).depends_on(cpu_plot.y_range)
                .depends_on(cpu_plot.view).depends_on(cpu_plot.cursor)
                .depends_on(cpu_plot.revision);
            vb.canvas(mem_plot)
                .depends_on(mem_plot.x_range).depends_on(mem_plot.y_range)
                .depends_on(mem_plot.view).depends_on(mem_plot.cursor)
                .depends_on(mem_plot.revision);
            vb.canvas(net_plot)
                .depends_on(net_plot.x_range).depends_on(net_plot.y_range)
                .depends_on(net_plot.view).depends_on(net_plot.cursor)
                .depends_on(net_plot.revision);
            vb.handle();
            vb.widget(sort_key);
            vb.tabs(tabs, [&] {
                vb.tab("Table", [&](prism::WidgetTree::ViewBuilder& tvb) {
                    tvb.table(table_rows).headers({"PID", "Name", "CPU %", "Mem %"});
                });
                vb.tab("Tree", [&](prism::WidgetTree::ViewBuilder& tvb) {
                    tvb.tree(tree_ctrl);
                });
            });
            vb.canvas(*this).depends_on(heartbeat_phase);
        });
    }
};

int main(int argc, char* argv[]) {
    SystemMonitor app;

    if (argc >= 2) {
        app.seed_demo_data();
        return showcase(argc, argv, app, 900, 700);
    }

    std::cerr << "This build only supports headless SVG snapshot mode so far "
                 "(pass an output path) -- interactive mode lands in Task 7.\n";
    return 1;
}

#else
int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    std::ofstream(argv[1]) << "<svg xmlns=\"http://www.w3.org/2000/svg\"/>\n";
    return 0;
}
#endif
```

- [ ] **Step 2: Register the executable and SVG snapshot in Meson**

In `examples/meson.build`, replace:

```meson
executable('model_tree_browser', 'model_tree_browser.cpp',
  dependencies : [prism_dep],
)

subdir('showcase')
```

with:

```meson
executable('model_tree_browser', 'model_tree_browser.cpp',
  dependencies : [prism_dep],
)

model_system_monitor_exe = executable('model_system_monitor', 'model_system_monitor.cpp',
  dependencies : [prism_dep],
)

custom_target('svg_system_monitor',
  output : 'system_monitor.svg',
  command : [model_system_monitor_exe, '@OUTPUT@'],
  depends : model_system_monitor_exe,
  build_by_default : true,
)

subdir('showcase')
```

- [ ] **Step 3: Build and verify the snapshot is produced**

Run: `meson setup builddir --reconfigure && ninja -C builddir`
Expected: builds cleanly; `builddir/examples/system_monitor.svg` exists and is non-trivial
(open it or `wc -l` it — should contain real geometry, not just the reflection-fallback's
empty `<svg .../>`).

- [ ] **Step 4: Run the full suite and commit**

Run: `meson test -C builddir` — confirm 0 failures (no new tests in this task, but confirm
nothing else broke).

```bash
git add examples/model_system_monitor.cpp examples/meson.build
git commit -m "feat(examples): static system-monitor UI skeleton (plots, split, table/tree tabs)"
```

---

### Task 6: `proc_metrics.hpp` — real `/proc` pollers (impure, dogfood-verified)

**Files:**
- Modify: `examples/proc_metrics.hpp`

**Interfaces:**
- Consumes: `parse_system_sample` (Task 2), `parse_process_entry`/`parse_meminfo` (Task 2/3).
- Produces: `read_system_sample(prev, dt) -> SystemSampleResult`,
  `read_process_list(prev, dt) -> vector<ProcessInfo>`,
  `poll_system_loop(stop_token, Shared<SystemSample>&)`,
  `poll_processes_loop(stop_token, Shared<vector<ProcessInfo>>&)`. Task 7 consumes the two
  `poll_*_loop` functions directly.

No unit test for this task: these functions read real `/proc` files and enumerate real PIDs —
there is nothing fixture-driven left to assert once you're past the already-tested `parse_*`
functions they call. Correctness here is verified by Task 7/8's live dogfooding run (does the
real app show real, sane CPU/mem/net/process numbols that move over time).

- [ ] **Step 1: Add the impure I/O layer**

Append to `examples/proc_metrics.hpp` (after the `sort_by` function from Task 3):

```cpp
inline std::string read_whole_file(const char* path) {
    std::ifstream f(path);
    std::stringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

inline SystemSampleResult read_system_sample(const SystemTotals& prev, double dt_seconds) {
    return parse_system_sample(read_whole_file("/proc/stat"), read_whole_file("/proc/meminfo"),
                                read_whole_file("/proc/net/dev"), prev, dt_seconds);
}

inline std::vector<ProcessInfo> read_process_list(const std::vector<ProcessInfo>& prev,
                                                   double dt_seconds) {
    std::unordered_map<int, long> prev_jiffies;
    for (const auto& p : prev) prev_jiffies[p.pid] = p.total_jiffies;

    double total_mem_kb = parse_meminfo(read_whole_file("/proc/meminfo")).total_kb;

    std::vector<ProcessInfo> result;
    for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
        if (!entry.is_directory()) continue;
        const std::string name = entry.path().filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(),
                                          [](unsigned char c) { return std::isdigit(c); }))
            continue;

        std::ifstream stat_file(entry.path() / "stat");
        std::ifstream status_file(entry.path() / "status");
        if (!stat_file || !status_file) continue; // process exited mid-scan

        std::stringstream stat_buf, status_buf;
        stat_buf << stat_file.rdbuf();
        status_buf << status_file.rdbuf();

        int pid = std::atoi(name.c_str());
        auto it = prev_jiffies.find(pid);
        long prev_ticks = it != prev_jiffies.end() ? it->second : -1;

        result.push_back(parse_process_entry(stat_buf.str(), status_buf.str(), prev_ticks,
                                              dt_seconds, total_mem_kb));
    }
    return result;
}

inline void poll_system_loop(std::stop_token stop, prism::Shared<SystemSample>& out) {
    SystemTotals prev = read_system_sample({}, 0.0).totals; // prime the delta baseline
    auto last = std::chrono::steady_clock::now();
    while (!stop.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        auto result = read_system_sample(prev, dt);
        prev = result.totals;
        out.set(result.sample);
    }
}

inline void poll_processes_loop(std::stop_token stop,
                                 prism::Shared<std::vector<ProcessInfo>>& out) {
    std::vector<ProcessInfo> prev;
    auto last = std::chrono::steady_clock::now();
    while (!stop.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        auto current = read_process_list(prev, dt);
        prev = current;
        out.set(std::move(current));
    }
}
```

**Parameter order matters here**: `std::jthread`'s automatic stop-token injection only
activates when the callable is invocable as `(stop_token, Args...)` — token *first*. A trailing
`std::stop_token` parameter (the original draft of this brief had it last) is never detected or
supplied by `std::jthread`'s constructor; the call site in Task 7
(`std::jthread(poll_system_loop, std::ref(app.sys_sample))`) needs the token-first shape to
compile and actually receive the jthread-managed token, unchanged on the call side.

Add these includes to the top of `examples/proc_metrics.hpp`: `#include <chrono>`,
`#include <cctype>`, `#include <filesystem>`, `#include <fstream>`, `#include <thread>`,
`#include <unordered_map>`, and `#include <prism/core/shared.hpp>` (for `prism::Shared<T>`
in the two `poll_*_loop` signatures).

- [ ] **Step 2: Build (compile-only check, no new tests)**

Run: `meson setup builddir --reconfigure && ninja -C builddir`
Expected: builds cleanly — `poll_system_loop`/`poll_processes_loop` aren't called by anything
yet (Task 7 wires them), so this step is purely a compilation check.

- [ ] **Step 3: Run the full suite and commit**

Run: `meson test -C builddir` — confirm 0 failures (existing `proc_metrics` tests still pass
unchanged; nothing here is exercised by them).

```bash
git add examples/proc_metrics.hpp
git commit -m "feat(examples): real /proc pollers for system stats and process list

Thin imperative shells around the already-tested parse_* functions --
no fixture-driven test possible past that boundary; correctness is
verified by running the real app (next task)."
```

---

### Task 7: `model_system_monitor.cpp` — wire the real background threads

**Files:**
- Modify: `examples/model_system_monitor.cpp`

**Interfaces:**
- Consumes: `poll_system_loop`, `poll_processes_loop` (Task 6).
- Produces: the fully-live interactive app (`main()`'s no-args branch).

- [ ] **Step 1: Replace the placeholder `main()` with the real interactive branch**

In `examples/model_system_monitor.cpp`, replace:

```cpp
    std::cerr << "This build only supports headless SVG snapshot mode so far "
                 "(pass an output path) -- interactive mode lands in Task 7.\n";
    return 1;
}
```

with:

```cpp
    std::jthread sys_thread;
    std::jthread proc_thread;

    prism::model_app({.title = "PRISM System Monitor", .width = 900, .height = 700,
                       .decoration = prism::DecorationMode::Custom},
                      app, [&](prism::AppContext& ctx) {
        app.sys_sample.observe([&app](const SystemSample& s) { app.ingest_system(s); });
        app.proc_list.observe([&app](const std::vector<ProcessInfo>& p) {
            app.ingest_processes(p);
        });
        // Re-sort immediately on a Dropdown change using the last known snapshot, rather
        // than waiting up to 1.5s for the next background poll to land.
        app.sort_key.observe([&app](const SortKey&) {
            app.ingest_processes(app.proc_list.get());
        });

        sys_thread = std::jthread(poll_system_loop, std::ref(app.sys_sample));
        proc_thread = std::jthread(poll_processes_loop, std::ref(app.proc_list));
    });

    return 0;
}
```

Add `#include <thread>` to the top include block (for `std::jthread`).

- [ ] **Step 2: Build**

Run: `ninja -C builddir`
Expected: builds cleanly.

- [ ] **Step 3: Dogfood — run the real interactive app**

Run: `./builddir/examples/model_system_monitor`
Expected (manual verification — no GUI screenshot tooling is available in this environment):
a window titled "PRISM System Monitor" opens; within ~1-2 seconds the CPU/memory/network plots
start showing real, plausible numbers for this machine; the Table tab lists real running
processes sorted by CPU%; switching to the Tree tab shows a parent/child hierarchy rooted at
pid 1 (or wherever this system's process tree actually roots); dragging the split handle
resizes the plot region vs. the table/tree region; changing the sort Dropdown reorders the
table immediately. **You need to run this and confirm** — report back anything that looks
wrong (frozen plots, empty table, crash, garbled tree) so it can be fixed before Task 8.

- [ ] **Step 4: Run the full suite and commit**

Run: `meson test -C builddir` — confirm 0 failures.

```bash
git add examples/model_system_monitor.cpp
git commit -m "feat(examples): wire real background threads into the system monitor UI"
```

---

### Task 8: `model_system_monitor.cpp` — perpetual heartbeat animation

**Files:**
- Modify: `examples/model_system_monitor.cpp`

**Interfaces:**
- Consumes: `AppContext::clock()` / `AnimationClock::add()` (existing framework API) and
  `AnimationClock::clear()` (Task 1b — **must land before this task**; without it, closing
  the app while this perpetual heartbeat is running livelocks on shutdown, per Task 1's
  investigation notes above).
- Produces: the finished app — a continuously pulsing indicator that proves the two poller
  threads and the main-thread sort/tree-rebuild never stall the UI.

- [ ] **Step 1: Add the perpetual oscillator to `setup()`**

In `examples/model_system_monitor.cpp`, inside the `setup` lambda from Task 7, add as the
first statement (before the `.observe(...)` wiring):

```cpp
        ctx.clock().add([&app](prism::AnimationClock::time_point now) {
            double t = std::chrono::duration<double>(now.time_since_epoch()).count();
            app.heartbeat_phase.set(static_cast<float>(t * 4.0)); // ~4 rad/s
            return true; // never remove -- keeps schedule_tick perpetually re-scheduling,
                         // which is what lets Task 1's fix keep draining Shared<T> with
                         // zero mouse/keyboard input.
        });
```

- [ ] **Step 2: Build**

Run: `ninja -C builddir`
Expected: builds cleanly.

- [ ] **Step 3: Dogfood — confirm the heartbeat never stutters**

Run: `./builddir/examples/model_system_monitor`
Expected (manual verification): the small pulsing square in the corner animates smoothly and
continuously, including while the plots/table are updating from the background pollers and
while dragging the split handle or switching tabs. If it ever visibly stutters or freezes,
that's a real regression to chase down before calling this done — it's the whole point of
including it. **You need to run this and confirm.**

- [ ] **Step 4: Run the full suite one last time and commit**

Run: `meson test -C builddir` — read the actual pass/fail count, confirm 0 failures across the
whole suite (not just this feature's tests).

```bash
git add examples/model_system_monitor.cpp
git commit -m "feat(examples): perpetual heartbeat animation proves the UI never stalls

Completes the system monitor example: two background /proc pollers feed
Shared<T>, the main thread does real sort/tree-rebuild work each poll, and
a continuously-animating indicator gives visual proof neither ever blocks
the frame loop."
```

---

## Self-Review Notes

- **Spec coverage:** all seven design-doc sections map to tasks — framework fix (Task 1),
  data model/pollers (Tasks 2/3/6), threading story (Task 7/8), UI layout (Task 5), code style
  (small pure-function files throughout), testing (fixture tests in 2/3/4, dogfood steps in
  7/8). The "Deferred / follow-ups" section lists gaps intentionally *not* built here (Table
  header-click sort/selection hook, Plot incremental-append API, a generic flat-list tree
  helper) — no task should attempt them.
- **Deliberate deviation from the design doc:** per-core CPU (`per_core_percent`) was dropped
  entirely rather than "captured but not plotted" — capturing data that's never read anywhere
  is dead weight; if per-core ever becomes a real follow-up visualization, the parsing can be
  added back alongside it.
- **Placeholder scan:** no TBDs; Task 5/6's stub-then-implement steps are the standard TDD
  red/green shape, not placeholders.
- **Type consistency checked:** `ProcessInfo`/`SortKey`/`sort_by` (Task 3) match their usage in
  `process_tree_source.hpp` (Task 4) and `model_system_monitor.cpp` (Task 5/7);
  `poll_system_loop`/`poll_processes_loop` (Task 6) signatures match their `std::jthread`
  construction in Task 7 exactly (trailing `std::stop_token` parameter, auto-supplied by
  `jthread`).
