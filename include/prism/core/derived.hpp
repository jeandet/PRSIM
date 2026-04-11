#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/transaction.hpp>

#include <functional>
#include <vector>

namespace prism::core {

template <typename T>
struct Derived {
    template <typename Fn, typename... Sources>
    Derived(Fn&& compute, Sources&... sources)
        : compute_(std::forward<Fn>(compute))
        , value_(compute_())
    {
        (subscribe(sources), ...);
    }

    const T& get() const { return value_; }
    operator const T&() const { return value_; }

    SenderHub<const T&>& on_change() { return changed_; }

    void observe(std::function<void(const T&)> cb) {
        observers_.push_back(changed_.connect(std::move(cb)));
    }

private:
    std::function<T()> compute_;
    T value_;
    SenderHub<const T&> changed_;
    std::vector<Connection> connections_;
    std::vector<Connection> observers_;

    void recompute() {
        T new_value = compute_();
        if (value_ == new_value) return;
        value_ = std::move(new_value);
        emit_or_defer(static_cast<void*>(&changed_), [this] { changed_.emit(value_); });
    }

    template <typename Source>
    void subscribe(Source& source) {
        connections_.push_back(
            source.on_change().connect([this](const auto&) { recompute(); })
        );
    }
};

} // namespace prism::core
