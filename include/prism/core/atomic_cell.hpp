#pragma once

#include <atomic>
#include <memory>

namespace prism::core {

// Single-value atomic cell for publishing immutable snapshots.
// Multiple producers may store; a single consumer loads the latest.
// Intermediate values are silently dropped (by design).
template <typename T>
class atomic_cell {
    std::atomic<std::shared_ptr<const T>> ptr_{nullptr};

public:
    atomic_cell() = default;

    explicit atomic_cell(T value)
        : ptr_{std::make_shared<const T>(std::move(value))}
    {}

    void store(T value)
    {
        ptr_.store(std::make_shared<const T>(std::move(value)));
    }

    [[nodiscard]] std::shared_ptr<const T> load() const
    {
        return ptr_.load();
    }
};

} // namespace prism::core
