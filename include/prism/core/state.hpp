#pragma once

#include <prism/core/connection.hpp>

namespace prism {

template <typename T>
struct State {
    T value{};

    State() = default;
    explicit State(T init) : value(std::move(init)) {}

    const T& get() const { return value; }
    operator const T&() const { return value; }

    void set(T new_value) {
        if (value == new_value) return;
        value = std::move(new_value);
        changed_.emit(value);
    }

    SenderHub<const T&>& on_change() { return changed_; }

private:
    SenderHub<const T&> changed_;
};

} // namespace prism
