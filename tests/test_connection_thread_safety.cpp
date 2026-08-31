#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/connection.hpp>
#include <thread>
#include <atomic>
#include <vector>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

// P0 semantic delta: snapshot-then-invoke.
// Today connect() during emit CAN fire same pass (loop sees grown vector).
// After P0 it MUST NOT fire same pass.

TEST_CASE("connect during emit does not fire same pass (snapshot semantics)") {
    prism::SenderHub<> hub;
    int late_calls = 0;
    std::vector<prism::Connection> keep;
    auto c1 = hub.connect([&] {
        // Connect a new receiver mid-emit — keep it alive.
        keep.push_back(hub.connect([&] { ++late_calls; }));
    });
    hub.emit();
    CHECK(late_calls == 0); // new receiver not seen this emit
    hub.emit();
    CHECK(late_calls == 1); // seen next emit
}

TEST_CASE("disconnect during emit still fires current pass, not next") {
    prism::SenderHub<> hub;
    int b_calls = 0;
    prism::Connection cb;
    auto ca = hub.connect([&] { cb.disconnect(); });
    cb = hub.connect([&] { ++b_calls; });
    hub.emit();
    // B was already snapshotted before A disconnected it → fires this pass
    CHECK(b_calls == 1);
    hub.emit();
    CHECK(b_calls == 1); // not next pass
}

TEST_CASE("nested emit: disconnect in outer hides from inner (snapshot)") {
    prism::SenderHub<> hub;
    int inner_calls = 0;
    prism::Connection b;
    b = hub.connect([&] { ++inner_calls; });
    bool first = true;
    auto a = hub.connect([&] {
        if (!first) return;
        first = false;
        b.disconnect();
        hub.emit(); // nested emit: snapshot taken after disconnect
    });
    hub.emit(); // outer emit: a fires, disconnects b, then inner emit (a no-ops)
    // With snapshot-then-invoke: inner snapshot excludes b → inner_calls == 1 (only outer's pass)
    // With deferred-remove (old): inner still sees b → inner_calls == 2
    CHECK(inner_calls == 1);
}

// Thread-safety hammer: concurrent emit/connect/disconnect must not crash
// and must be TSan-clean. Run with -Db_sanitize=thread.
TEST_CASE("concurrent emit/connect/disconnect hammer is TSan-clean") {
    prism::SenderHub<int> hub;
    std::atomic<bool> stop{false};
    std::atomic<int> emits{0};

    // Keep some long-lived connections alive
    auto keep = hub.connect([](int) {});

    std::vector<std::thread> workers;
    // 2 emitters
    for (int i = 0; i < 2; ++i) {
        workers.emplace_back([&] {
            while (!stop.load()) {
                hub.emit(1);
                ++emits;
            }
        });
    }
    // 2 connectors/disconnectors
    for (int i = 0; i < 2; ++i) {
        workers.emplace_back([&] {
            while (!stop.load()) {
                auto c = hub.connect([](int) {});
                // c disconnects on destruction
            }
        });
    }
    // 1 thread that disconnects from GC-like context
    workers.emplace_back([&] {
        while (!stop.load()) {
            auto c = hub.connect([](int) {});
            c.disconnect();
        }
    });

    // Let it hammer for ~50ms
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop.store(true);
    for (auto& t : workers) t.join();

    // If we got here without crash/data race, TSan will have reported.
    CHECK(emits.load() > 0);
}
