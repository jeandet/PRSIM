#include <prism/core/mpsc_queue.hpp>

#include <cassert>
#include <thread>
#include <vector>

void test_single_thread()
{
    prism::mpsc_queue<int> q;

    q.push(1);
    q.push(2);
    q.push(3);

    assert(q.pop() == 1);
    assert(q.pop() == 2);
    assert(q.pop() == 3);
    assert(!q.pop().has_value());
}

void test_multi_producer()
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

    assert(count == num_threads * per_thread);
}

int main()
{
    test_single_thread();
    test_multi_producer();
}
