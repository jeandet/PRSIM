#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/perf_lab/lab_model.hpp"

#include <prism/ui/table.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace perf_lab;

TEST_CASE("RingBuffer preserves insertion order until full") {
    RingBuffer r(4);
    r.push(1.0); r.push(2.0); r.push(3.0);
    CHECK(r.size() == 3);
    CHECK(r[0] == 1.0);
    CHECK(r[2] == 3.0);
}

TEST_CASE("RingBuffer evicts the oldest sample on wrap") {
    RingBuffer r(3);
    r.push(1.0); r.push(2.0); r.push(3.0); r.push(4.0);
    CHECK(r.size() == 3);
    CHECK(r[0] == 2.0);
    CHECK(r[1] == 3.0);
    CHECK(r[2] == 4.0);
}

TEST_CASE("RingPlotSource maps index to the ring's oldest-first order") {
    RingBuffer r(3);
    r.push(10.0); r.push(20.0); r.push(30.0); r.push(40.0);
    RingPlotSource src{&r};
    CHECK(src.size() == 3);
    CHECK(src.x(0) == 0.0);
    CHECK(src.y(0) == 20.0);
    CHECK(src.y(2) == 40.0);
}

TEST_CASE("TelemetryGenerator is deterministic per seed and bounded") {
    TelemetryGenerator a(7), b(7), c(8);
    for (int i = 0; i < 100; ++i) {
        const double t = i * 0.001;
        CHECK(a.next(t) == b.next(t));
    }
    TelemetryGenerator d(7), e(8);
    bool any_diff = false;
    for (int i = 0; i < 100; ++i) {
        const double v = d.next(i * 0.001);
        CHECK(std::fabs(v) < 2.0);
        if (v != e.next(i * 0.001)) any_diff = true;
    }
    CHECK(any_diff);
}

TEST_CASE("LabTable satisfies ColumnStorage and formats cells") {
    static_assert(prism::ui::ColumnStorage<LabTable>);
    LabTable t(10);
    CHECK(t.column_count() == 5);
    CHECK(t.row_count() == 10);
    CHECK(t.header(0) == "id");
    CHECK(t.header(4) == "status");
    CHECK(t.cell_text(0, 0) == "0");
    CHECK(t.cell_text(3, 1) == "sensor_3");
    CHECK(t.cell_text(0, 4) == "OK");
    CHECK(t.cell_text(1, 4) == "WARN");
}

TEST_CASE("LabTable::update_slice wraps around the end") {
    LabTable t(10);
    t.update_slice(8, 4, 3.0); // rows 8, 9, 0, 1
    CHECK(t.cell_text(9, 2) == "3.00");
    CHECK(t.cell_text(0, 2) == "3.00");
    CHECK(t.cell_text(2, 2) != "3.00");
    // status: int(|3.0| * 2) % 3 = 6 % 3 = 0 -> "OK"
    CHECK(t.cell_text(0, 4) == "OK");
}

TEST_CASE("parse_lab_args defaults, flags, positional svg, and errors") {
    auto def = parse_lab_args({});
    REQUIRE(def.has_value());
    CHECK(def->rows == 100'000);
    CHECK(def->points == 1'000'000);
    CHECK(def->rate_hz == 1000.0);
    CHECK(def->headless_seconds == 0);
    CHECK(def->svg_path.empty());

    auto full = parse_lab_args({"--rows", "500", "--points", "2000", "--rate", "250", "--headless", "5"});
    REQUIRE(full.has_value());
    CHECK(full->rows == 500);
    CHECK(full->points == 2000);
    CHECK(full->rate_hz == 250.0);
    CHECK(full->headless_seconds == 5);

    auto svg = parse_lab_args({"out.svg"});
    REQUIRE(svg.has_value());
    CHECK(svg->svg_path == "out.svg");

    CHECK_FALSE(parse_lab_args({"--rows"}).has_value());
    CHECK_FALSE(parse_lab_args({"--rows", "abc"}).has_value());
    CHECK_FALSE(parse_lab_args({"--rows", "0"}).has_value());
    CHECK_FALSE(parse_lab_args({"--bogus", "1"}).has_value());
    // Above INT_MAX would narrow implementation-definedly into headless_seconds (int).
    CHECK_FALSE(parse_lab_args({"--headless", "999999999999"}).has_value());
    CHECK_FALSE(parse_lab_args({"a.svg", "b.svg"}).has_value());
    // The showcase SVG convention reads argv[1] as the output path, so a positional
    // capture path is only valid as the sole argument.
    CHECK_FALSE(parse_lab_args({"--rows", "10", "out.svg"}).has_value());
}

TEST_CASE("summarize_build_times computes nearest-rank percentiles") {
    auto s = summarize_build_times({5.0, 1.0, 3.0, 2.0, 4.0});
    CHECK(s.samples == 5);
    CHECK(s.min_ms == 1.0);
    CHECK(s.median_ms == 3.0);
    CHECK(s.max_ms == 5.0);
    CHECK(s.p95_ms == 4.0); // floor(0.95 * 4) = 3 -> sorted[3]

    auto empty = summarize_build_times({});
    CHECK(empty.samples == 0);
    CHECK(empty.max_ms == 0.0);
}

TEST_CASE("rate_per_second and format_bytes") {
    CHECK(rate_per_second(120, 2.0) == 60.0);
    CHECK(rate_per_second(120, 0.0) == 0.0);
    CHECK(format_bytes(512) == "512 B");
    CHECK(format_bytes(2048) == "2.0 KB");
    CHECK(format_bytes(19'300'000) == "18.4 MB");
}
