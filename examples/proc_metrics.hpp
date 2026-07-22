#pragma once

#include <algorithm>
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
