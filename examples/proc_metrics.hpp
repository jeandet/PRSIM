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
