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
