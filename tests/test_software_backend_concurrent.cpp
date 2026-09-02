#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/software_backend.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <thread>
#include <vector>
#include <atomic>

TEST_CASE("windows_mutex concurrent submit/sdl_id lookup no race") {
    prism::backends::SoftwareBackend backend{{}};
    // Pre-create one window so snapshots map has entry
    backend.create_window({});

    std::atomic<bool> stop{false};
    std::atomic<int> errors{0};

    auto worker = [&] {
        for (int i = 0; i < 200; ++i) {
            try {
                auto snap = std::make_shared<prism::render::SceneSnapshot>();
                backend.submit(1, snap);
                backend.submit(2, snap);
            } catch (...) { errors++; }
        }
    };

    std::vector<std::thread> th;
    for (int i = 0; i < 4; ++i) th.emplace_back(worker);
    for (auto& t : th) t.join();

    CHECK(errors == 0);
}

TEST_CASE("windows_mutex concurrent request_window does not deadlock") {
    // request_window pushes to mpsc queue then waits 2s; concurrent submit shouldn't block it
    prism::backends::SoftwareBackend backend{{}};
    std::atomic<bool> done{false};
    std::thread t([&]{
        // No run() draining, so request will time out after 2s
        auto* w = backend.request_window({});
        (void)w;
        done = true;
    });
    // Meanwhile hammer submit
    for (int i = 0; i < 100; ++i) {
        auto snap = std::make_shared<prism::render::SceneSnapshot>();
        backend.submit(9999, snap);
    }
    t.join();
    CHECK(done.load());
}
