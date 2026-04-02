#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/exec.hpp>

namespace prism::core {

// Scheduler adaptor: hub | prism::on(sched) | prism::then(f)
// Wraps the downstream callback to execute on the given scheduler.
template <typename Scheduler>
struct On {
    Scheduler sched;
};

template <typename Scheduler>
auto on(Scheduler sched) {
    return On<Scheduler>{sched};
}

// hub | prism::on(sched) → ScheduledHub that wraps connect()
template <typename Scheduler, typename... Args>
struct ScheduledHub {
    SenderHub<Args...>& hub;
    Scheduler sched;
};

template <typename... Args, typename Scheduler>
ScheduledHub<Scheduler, Args...> operator|(SenderHub<Args...>& hub, On<Scheduler> adaptor) {
    return {hub, adaptor.sched};
}

// ScheduledHub | prism::then(f) → Connection (callback runs on scheduler)
template <typename Scheduler, typename... Args, typename F>
[[nodiscard]] Connection operator|(ScheduledHub<Scheduler, Args...> sh, Then<F> adaptor) {
    return sh.hub.connect([sched = sh.sched, fn = std::move(adaptor.fn)](Args... args) {
        exec::start_detached(
            stdexec::schedule(sched)
            | stdexec::then([fn, args...]() { fn(args...); })
        );
    });
}

} // namespace prism::core
