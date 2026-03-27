#pragma once

#include <prism/core/connection.hpp>

#include <functional>
#include <vector>

namespace prism {

template <typename T>
struct Field {
    const char* label = "";
    T value{};

    Field() = default;
    Field(const char* lbl) : label(lbl) {}
    Field(const char* lbl, T init) : label(lbl), value(std::move(init)) {}

    const T& get() const { return value; }
    operator const T&() const { return value; }

    void set(T new_value) {
        if (value == new_value) return;
        value = std::move(new_value);
        changed_.emit(value);
    }

    void observe(std::function<void(const T&)> cb) {
        observers_.push_back(changed_.connect(std::move(cb)));
    }

    SenderHub<const T&>& on_change() { return changed_; }

private:
    SenderHub<const T&> changed_;
    std::vector<Connection> observers_;
};

} // namespace prism
