#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/transaction.hpp>

#include <functional>
#include <vector>

namespace prism::core {

template <typename T, typename Derived>
struct ObservableValue {
    T value{};

    ObservableValue() = default;
    ObservableValue(T init) : value(std::move(init)) {}

    const T& get() const { return value; }
    operator const T&() const { return value; }

    void set(T new_value) {
        if (value == new_value) return;
        value = std::move(new_value);
        if (transaction_active()) {
            current_transaction().queue.push_back({
                static_cast<void*>(&changed_),
                [this] { changed_.emit(value); }
            });
        } else {
            changed_.emit(value);
        }
    }

    void observe(std::function<void(const T&)> cb) {
        observers_.push_back(changed_.connect(std::move(cb)));
    }

    SenderHub<const T&>& on_change() { return changed_; }

private:
    SenderHub<const T&> changed_;
    std::vector<Connection> observers_;
};

template <typename T>
struct Field : ObservableValue<T, Field<T>> {
    static constexpr bool is_prism_field = true;
    using ObservableValue<T, Field<T>>::ObservableValue;
};

} // namespace prism::core
