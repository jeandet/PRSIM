#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/mpsc_queue.hpp>

#include <functional>
#include <vector>

namespace prism::core {

// Cross-thread, ordered, lossless event stream. Contrast with Shared<T>: Shared<T> publishes
// a latest value and coalesces intermediate set()s away, Channel<T> delivers every send() in
// FIFO order. No get() -- there is no "current value", only a sequence of arrivals.
template <typename T>
class Channel {
public:
    // Thread-safe -- may be called from any thread.
    void send(T value) { queue_.push(std::move(value)); }

    // Consumer-thread only. Pops everything queued so far, in order, and emits each via
    // on_receive() -- unlike Shared<T>::drain_notifications(), never coalesces.
    void drain_notifications() {
        while (auto value = queue_.pop())
            received_.emit(*value);
    }

    SenderHub<const T&>& on_receive() { return received_; }

    void observe(std::function<void(const T&)> cb) {
        observers_.push_back(received_.connect(std::move(cb)));
    }

private:
    mpsc_queue<T> queue_;
    SenderHub<const T&> received_;
    std::vector<Connection> observers_;
};

} // namespace prism::core
