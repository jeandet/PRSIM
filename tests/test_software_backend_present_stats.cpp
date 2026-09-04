#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/software_backend.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <chrono>
#include <thread>

TEST_CASE("present_stats is nullopt for unknown windows and before the first present") {
    prism::backends::SoftwareBackend backend{{}};
    backend.create_window({});  // WindowId 1
    CHECK(backend.present_stats(9999) == std::nullopt);
    CHECK(backend.present_stats(1) == std::nullopt);
}

TEST_CASE("present_stats counts presents and reports timing") {
    prism::backends::SoftwareBackend backend{{}};
    backend.create_window({});

    std::thread t([&] { backend.run([](const prism::app::WindowEvent&) {}); });
    backend.wait_ready();

    backend.submit(1, std::make_shared<prism::render::SceneSnapshot>());
    backend.wake();

    std::optional<prism::app::PresentStats> stats;
    for (int i = 0; i < 200 && !stats; ++i) {
        stats = backend.present_stats(1);
        if (!stats) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    backend.quit();
    t.join();

    REQUIRE(stats.has_value());
    CHECK(stats->present_count >= 1);
    CHECK(stats->last_present_ms >= 0.0);
    CHECK(stats->last_present_at.time_since_epoch().count() > 0);
}
