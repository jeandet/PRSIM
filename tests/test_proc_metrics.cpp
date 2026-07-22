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

TEST_CASE("poll_system_loop and poll_processes_loop are jthread-invocable with stop_token "
          "first, and honor jthread-managed cancellation") {
    // Mirrors Task 7's real call form: std::jthread(poll_system_loop, std::ref(app.sys_sample)).
    // std::jthread only auto-injects its stop_token when the callable takes it as the FIRST
    // parameter -- if either function ever regresses to a trailing stop_token, this test fails
    // to COMPILE (not just fail at runtime), because std::jthread's constructor requires the
    // remaining args alone to be invocable.
    prism::Shared<SystemSample> sys_sample;
    auto t0 = std::chrono::steady_clock::now();
    {
        std::jthread t(poll_system_loop, std::ref(sys_sample));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        t.request_stop();
    }
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));

    prism::Shared<std::vector<ProcessInfo>> proc_list;
    auto t1 = std::chrono::steady_clock::now();
    {
        std::jthread t(poll_processes_loop, std::ref(proc_list));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        t.request_stop();
    }
    CHECK(std::chrono::steady_clock::now() - t1 < std::chrono::seconds(3));
}
