#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <prism/core/reflect_annotations.hpp>
#include <prism/core/shared.hpp>

#if __cpp_impl_reflection
#define PRISM_SKIP_MEMBER [[=prism::core::skip]]
#define PRISM_LABEL_MEMBER(s) [[=prism::core::label<s>]]
#else
#define PRISM_SKIP_MEMBER
#define PRISM_LABEL_MEMBER(s)
#endif

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
    PRISM_SKIP_MEMBER int ppid = 0;
    std::string name;
    PRISM_SKIP_MEMBER char state = '?';
    PRISM_LABEL_MEMBER("CPU %") float cpu_percent = 0.f;
    PRISM_LABEL_MEMBER("Mem %") float mem_percent = 0.f;
    PRISM_SKIP_MEMBER long rss_kb = 0;
    PRISM_SKIP_MEMBER long total_jiffies = 0;
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
        // sleep_for is not interruptible by stop_token, so a jthread destructor's
        // request_stop()+join() can linger up to this sleep's duration on shutdown --
        // not a hang, just a bounded delay (same caveat applies to poll_processes_loop
        // below, with its own longer sleep).
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
