#pragma once

#include <prism/core/connection.hpp>

#include <cstddef>
#include <vector>

namespace prism {

template <typename T>
class List {
public:
    using value_type = T;
    void push_back(T item) {
        items_.push_back(std::move(item));
        inserted_.emit(items_.size() - 1, items_.back());
    }

    void erase(size_t index) {
        items_.erase(items_.begin() + static_cast<ptrdiff_t>(index));
        removed_.emit(index);
    }

    void set(size_t index, T value) {
        items_[index] = std::move(value);
        updated_.emit(index, items_[index]);
    }

    [[nodiscard]] const T& operator[](size_t i) const { return items_[i]; }
    [[nodiscard]] size_t size() const { return items_.size(); }
    [[nodiscard]] bool empty() const { return items_.empty(); }

    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }

    SenderHub<size_t, const T&>& on_insert() { return inserted_; }
    SenderHub<size_t>& on_remove() { return removed_; }
    SenderHub<size_t, const T&>& on_update() { return updated_; }

private:
    std::vector<T> items_;
    SenderHub<size_t, const T&> inserted_;
    SenderHub<size_t> removed_;
    SenderHub<size_t, const T&> updated_;
};

} // namespace prism
