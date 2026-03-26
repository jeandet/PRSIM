#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/mpsc_queue.hpp>

#include <thread>
#include <vector>

TEST_CASE("mpsc_queue single-thread push/pop")
{
    prism::mpsc_queue<int> q;

    q.push(1);
    q.push(2);
    q.push(3);

    CHECK(q.pop() == 1);
    CHECK(q.pop() == 2);
    CHECK(q.pop() == 3);
    CHECK_FALSE(q.pop().has_value());
}

TEST_CASE("mpsc_queue multi-producer")
{
    prism::mpsc_queue<int> q;
    constexpr int per_thread = 1000;
    constexpr int num_threads = 4;

    std::vector<std::jthread> producers;
    for (int t = 0; t < num_threads; ++t)
        producers.emplace_back([&q, t] {
            for (int i = 0; i < per_thread; ++i)
                q.push(t * per_thread + i);
        });

    for (auto& t : producers)
        t.join();

    int count = 0;
    while (q.pop())
        ++count;

    CHECK(count == num_threads * per_thread);
}
