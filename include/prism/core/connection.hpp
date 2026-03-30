#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace prism {

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

    [[nodiscard]] Connection connect(Callback cb) {
        auto id = next_id_++;
        receivers_.push_back({id, std::move(cb)});
        auto detach = std::make_shared<std::function<void()>>(
            [this, id] { remove(id); }
        );
        return Connection{std::move(detach)};
    }

    void emit(Args... args) const {
        ++emit_depth_;
        for (size_t i = 0; i < receivers_.size(); ++i) {
            if (receivers_[i].cb) receivers_[i].cb(args...);
        }
        if (--emit_depth_ == 0 && !pending_removes_.empty()) {
            for (auto id : pending_removes_)
                std::erase_if(receivers_, [id](auto& e) { return e.id == id; });
            pending_removes_.clear();
        }
    }

private:
    struct Entry {
        uint64_t id;
        Callback cb;
    };

    uint64_t next_id_ = 0;
    mutable std::vector<Entry> receivers_;
    mutable std::vector<uint64_t> pending_removes_;
    mutable int emit_depth_ = 0;

    void remove(uint64_t id) {
        if (emit_depth_ > 0)
            pending_removes_.push_back(id);
        else
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

} // namespace prism
