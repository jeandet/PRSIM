#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/exec.hpp>
#include <prism/core/on.hpp>
#include <prism/core/field.hpp>

TEST_CASE("just | then | sync_wait") {
    auto s = stdexec::just(42)
           | stdexec::then([](int x) { return x * 2; });

    auto [result] = stdexec::sync_wait(std::move(s)).value();
    CHECK(result == 84);
}

TEST_CASE("run_loop schedules work on caller thread") {
    stdexec::run_loop loop;
    auto sched = loop.get_scheduler();

    auto caller_id = std::this_thread::get_id();
    std::thread::id work_id;

    auto work = stdexec::schedule(sched)
              | stdexec::then([&] {
                    work_id = std::this_thread::get_id();
                    loop.finish();
                });

    exec::start_detached(std::move(work));
    loop.run();

    CHECK(work_id == caller_id);
}

TEST_CASE("run_loop receives work from another thread") {
    stdexec::run_loop loop;
    auto sched = loop.get_scheduler();

    int value = 0;

    std::thread producer([&] {
        for (int i = 1; i <= 3; ++i) {
            exec::start_detached(
                stdexec::schedule(sched)
                | stdexec::then([&, i] { value += i; })
            );
        }
        exec::start_detached(
            stdexec::schedule(sched)
            | stdexec::then([&] { loop.finish(); })
        );
    });

    loop.run();
    producer.join();

    CHECK(value == 6);
}

TEST_CASE("SenderHub | prism::on(sched) | prism::then(f) runs on scheduler thread") {
    stdexec::run_loop loop;
    auto sched = loop.get_scheduler();

    prism::SenderHub<int> hub;
    auto caller_id = std::this_thread::get_id();
    std::thread::id cb_id;
    int received = -1;

    auto conn = hub
              | prism::on(sched)
              | prism::then([&](int v) {
                    received = v;
                    cb_id = std::this_thread::get_id();
                    loop.finish();
                });

    std::thread emitter([&] { hub.emit(42); });

    loop.run();
    emitter.join();

    CHECK(received == 42);
    CHECK(cb_id == caller_id);
}

TEST_CASE("Field on_change | prism::on(sched) | prism::then(f)") {
    stdexec::run_loop loop;
    auto sched = loop.get_scheduler();

    prism::Field<int> f{0};
    int received = -1;

    auto conn = f.on_change()
              | prism::on(sched)
              | prism::then([&](const int& v) {
                    received = v;
                    loop.finish();
                });

    std::thread mutator([&] { f.set(99); });

    loop.run();
    mutator.join();

    CHECK(received == 99);
}
