#pragma once

#include <prism/core/atomic_cell.hpp>
#include <prism/core/connection.hpp>

#include <atomic>
#include <functional>
#include <vector>

namespace prism::core {

template <typename T>
struct Shared {
    Shared() : cell_(T{}) {}
    Shared(T init) : cell_(std::move(init)) {}

    T get() const {
        auto ptr = cell_.load();
        return ptr ? *ptr : T{};
    }

    void set(T new_value) {
        cell_.store(std::move(new_value));
        pending_.store(true, std::memory_order_release);
    }

    void drain_notifications() {
        if (!pending_.exchange(false, std::memory_order_acquire))
            return;
        changed_.emit(get());
    }

    SenderHub<const T&>& on_change() { return changed_; }

    void observe(std::function<void(const T&)> cb) {
        observers_.push_back(changed_.connect(std::move(cb)));
    }

private:
    atomic_cell<T> cell_;
    std::atomic<bool> pending_{false};
    SenderHub<const T&> changed_;
    std::vector<Connection> observers_;
};

} // namespace prism::core
