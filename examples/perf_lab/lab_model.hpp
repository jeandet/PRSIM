#pragma once

// Perf lab pure core — telemetry generation, workload data structures, CLI parsing,
// and stats math. No PRISM dependencies (same pattern as
// examples/model_system_monitor/proc_metrics.hpp); unit-tested by tests/test_perf_lab.cpp.

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace perf_lab {

// Fixed-capacity, oldest-first ring of doubles. Backs the plot's PlotSource.
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity) : data_(capacity) {}

    void push(double v) {
        data_[write_] = v;
        write_ = (write_ + 1) % data_.size();
        if (size_ < data_.size()) ++size_;
    }

    size_t size() const { return size_; }
    size_t capacity() const { return data_.size(); }

    // i = 0 is the oldest sample still held.
    double operator[](size_t i) const {
        return data_[(write_ + data_.size() - size_ + i) % data_.size()];
    }

private:
    std::vector<double> data_;
    size_t write_ = 0;
    size_t size_ = 0;
};

// PlotSource adapter over a RingBuffer: x = sample index, oldest = 0.
struct RingPlotSource {
    const RingBuffer* buf;
    size_t size() const { return buf->size(); }
    double x(size_t i) const { return static_cast<double>(i); }
    double y(size_t i) const { return (*buf)[i]; }
};

// Deterministic synthetic telemetry: sum of sines plus xorshift noise in [-0.1, 0.1).
class TelemetryGenerator {
public:
    explicit TelemetryGenerator(uint64_t seed = 42) : rng_(seed) {}

    double next(double t_seconds) {
        const double wave = std::sin(t_seconds * 2.0)
                          + 0.5 * std::sin(t_seconds * 7.3)
                          + 0.25 * std::sin(t_seconds * 13.1);
        return wave + noise() * 0.1;
    }

private:
    // xorshift64*, mapped to [-1, 1).
    double noise() {
        rng_ ^= rng_ >> 12;
        rng_ ^= rng_ << 25;
        rng_ ^= rng_ >> 27;
        const uint64_t bits = (rng_ * 0x2545F4914F6CDD1DULL) >> 11; // 53-bit mantissa
        return static_cast<double>(bits) * (1.0 / 9007199254740992.0) * 2.0 - 1.0;
    }

    uint64_t rng_;
};

// The synthetic table workload: `rows` rows x 5 columns, hand-written to satisfy
// prism::ui::ColumnStorage without -freflection (builds in every configuration).
struct LabTable {
    std::vector<int64_t> id;
    std::vector<std::string> name;
    std::vector<double> value;
    std::vector<double> rate;
    std::vector<int> status;

    explicit LabTable(size_t rows) {
        id.reserve(rows);
        name.reserve(rows);
        value.reserve(rows);
        rate.reserve(rows);
        status.reserve(rows);
        TelemetryGenerator gen(1234);
        for (size_t i = 0; i < rows; ++i) {
            id.push_back(static_cast<int64_t>(i));
            name.push_back("sensor_" + std::to_string(i));
            value.push_back(gen.next(static_cast<double>(i) * 0.001));
            rate.push_back(gen.next(static_cast<double>(i) * 0.002));
            status.push_back(static_cast<int>(i % 3));
        }
    }

    // Mutate a wrapping slice of rows — what makes the table visibly live under telemetry.
    void update_slice(size_t start, size_t count, double v) {
        const size_t n = row_count();
        for (size_t k = 0; k < count; ++k) {
            const size_t r = (start + k) % n;
            value[r] = v;
            rate[r] = v * 0.5;
            status[r] = static_cast<int>(std::fabs(v) * 2.0) % 3;
        }
    }

    size_t column_count() const { return kHeaders.size(); }
    size_t row_count() const { return id.size(); }

    std::string cell_text(size_t r, size_t c) const {
        char buf[32];
        switch (c) {
        case 0: return std::to_string(id[static_cast<size_t>(r)]);
        case 1: return name[r];
        case 2: std::snprintf(buf, sizeof buf, "%.2f", value[r]); return buf;
        case 3: std::snprintf(buf, sizeof buf, "%.2f", rate[r]); return buf;
        default: return std::string(kStatus[static_cast<size_t>(status[r])]);
        }
    }

    std::string_view header(size_t c) const { return kHeaders[c]; }

private:
    static constexpr std::array<std::string_view, 5> kHeaders = {"id", "name", "value", "rate", "status"};
    static constexpr std::array<std::string_view, 3> kStatus = {"OK", "WARN", "CRIT"};
};

struct LabConfig {
    size_t rows = 100'000;
    size_t points = 1'000'000;
    double rate_hz = 1000.0;
    int headless_seconds = 0;  // 0 = interactive
    std::string svg_path;      // positional arg: one-frame SVG capture (showcase convention)
};

// Parses args excluding argv[0]; nullopt on any malformed or unknown input.
// A positional SVG capture path is only valid as the sole argument, because the
// showcase() helper always reads argv[1] as the output path.
inline std::optional<LabConfig> parse_lab_args(const std::vector<std::string>& args) {
    LabConfig cfg;
    auto parse_size = [](const std::string& s) -> std::optional<size_t> {
        try {
            size_t pos;
            const auto v = std::stoull(s, &pos);
            if (pos != s.size() || v == 0) return std::nullopt;
            return v;
        } catch (...) { return std::nullopt; }
    };
    auto parse_positive_double = [](const std::string& s) -> std::optional<double> {
        try {
            size_t pos;
            const auto v = std::stod(s, &pos);
            if (pos != s.size() || v <= 0.0) return std::nullopt;
            return v;
        } catch (...) { return std::nullopt; }
    };
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == "--rows" || a == "--points" || a == "--headless") {
            if (i + 1 >= args.size()) return std::nullopt;
            const auto v = parse_size(args[++i]);
            if (!v) return std::nullopt;
            if (a == "--rows") cfg.rows = *v;
            else if (a == "--points") cfg.points = *v;
            else {
                if (*v > INT_MAX) return std::nullopt; // would narrow UB-ishly into int
                cfg.headless_seconds = static_cast<int>(*v);
            }
        } else if (a == "--rate") {
            if (i + 1 >= args.size()) return std::nullopt;
            const auto v = parse_positive_double(args[++i]);
            if (!v) return std::nullopt;
            cfg.rate_hz = *v;
        } else if (!a.empty() && a[0] == '-') {
            return std::nullopt;
        } else {
            if (!cfg.svg_path.empty() || args.size() != 1) return std::nullopt;
            cfg.svg_path = a;
        }
    }
    return cfg;
}

// Windowed rate from a monotonic counter delta over wall seconds; 0 when dt <= 0.
inline double rate_per_second(uint64_t count_delta, double dt_seconds) {
    return dt_seconds > 0.0 ? static_cast<double>(count_delta) / dt_seconds : 0.0;
}

struct BuildStatsSummary {
    size_t samples = 0;
    double min_ms = 0, median_ms = 0, p95_ms = 0, max_ms = 0;
};

// Nearest-rank percentiles over a sorted copy; empty input -> all zeros.
inline BuildStatsSummary summarize_build_times(std::vector<double> v) {
    BuildStatsSummary s;
    s.samples = v.size();
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    s.min_ms = v.front();
    s.max_ms = v.back();
    s.median_ms = v[(n - 1) / 2];
    s.p95_ms = v[std::min(n - 1, static_cast<size_t>(0.95 * static_cast<double>(n - 1)))];
    return s;
}

// Human-readable byte counts for the overlay/report ("18.4 MB").
inline std::string format_bytes(size_t bytes) {
    static constexpr const char* kUnits[] = {"B", "KB", "MB", "GB"};
    double v = static_cast<double>(bytes);
    size_t u = 0;
    while (v >= 1024.0 && u + 1 < 4) { v /= 1024.0; ++u; }
    char buf[32];
    if (u == 0) std::snprintf(buf, sizeof buf, "%zu B", bytes);
    else std::snprintf(buf, sizeof buf, "%.1f %s", v, kUnits[u]);
    return buf;
}

} // namespace perf_lab
