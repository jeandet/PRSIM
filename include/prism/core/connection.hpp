#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace prism::core {

class Connection {
public:
    Connection() = default;

    Connection(std::shared_ptr<std::function<void()>> detach)
        : detach_(std::move(detach)) {}

    ~Connection() { disconnect(); }

    Connection(Connection&& o) noexcept : detach_(std::move(o.detach_)) {}
    Connection& operator=(Connection&& o) noexcept {
        if (this != &o) {
            disconnect();
            detach_ = std::move(o.detach_);
        }
        return *this;
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void disconnect() {
        if (auto d = std::move(detach_)) {
            if (*d) (*d)();
        }
    }

private:
    std::shared_ptr<std::function<void()>> detach_;
};

template <typename... Args>
class SenderHub {
public:
    using Callback = std::function<void(Args...)>;

    SenderHub() = default;
    SenderHub(const SenderHub&) = delete;
    SenderHub& operator=(const SenderHub&) = delete;

    SenderHub(SenderHub&& other) noexcept {
        std::lock_guard<std::mutex> lk(other.mutex_);
        receivers_ = std::move(other.receivers_);
        next_id_ = other.next_id_;
    }
    SenderHub& operator=(SenderHub&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lk(mutex_, other.mutex_);
            receivers_ = std::move(other.receivers_);
            next_id_ = other.next_id_;
        }
        return *this;
    }

    [[nodiscard]] Connection connect(Callback cb) {
        uint64_t id;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            id = next_id_++;
            receivers_.push_back({id, std::move(cb)});
        }
        auto detach = std::make_shared<std::function<void()>>(
            [this, id] { remove(id); }
        );
        return Connection{std::move(detach)};
    }

    void emit(Args... args) const {
        std::vector<Callback> snapshot;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            snapshot.reserve(receivers_.size());
            for (auto& e : receivers_) snapshot.push_back(e.cb);
        }
        for (auto& cb : snapshot) {
            if (cb) cb(args...);
        }
    }

private:
    struct Entry {
        uint64_t id;
        Callback cb;
    };

    mutable std::mutex mutex_;
    uint64_t next_id_ = 0;
    mutable std::vector<Entry> receivers_;

    void remove(uint64_t id) {
        std::lock_guard<std::mutex> lk(mutex_);
        std::erase_if(receivers_, [id](auto& e) { return e.id == id; });
    }
};

// Pipe adaptor: hub | prism::then(f) → Connection
template <typename F>
struct Then {
    F fn;
};

template <typename F>
auto then(F&& fn) {
    return Then<std::decay_t<F>>{std::forward<F>(fn)};
}

template <typename... Args, typename F>
[[nodiscard]] Connection operator|(SenderHub<Args...>& hub, Then<F> adaptor) {
    return hub.connect(std::move(adaptor.fn));
}

} // namespace prism::core
