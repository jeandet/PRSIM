#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/software_backend.hpp>

#include <chrono>

TEST_CASE("SoftwareBackend::request_window times out rather than hanging forever "
          "if nothing ever drains the queue") {
    // No run() thread is started, so the request is never drained — this proves the
    // bounded wait_for(2s) fires rather than blocking indefinitely.
    prism::backends::SoftwareBackend backend{{}};
    auto start = std::chrono::steady_clock::now();
    auto* win = backend.request_window({});
    auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(win == nullptr);
    CHECK(elapsed < std::chrono::seconds(5)); // bounded, not infinite
}
