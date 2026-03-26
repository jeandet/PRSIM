#pragma once

#include <atomic>
#include <optional>

namespace prism {

// Apple Silicon (aarch64 with 128-byte L1 lines) and POWER use 128, everything else 64.
// There's no reliable compile-time way to distinguish Apple Silicon from other AArch64,
// so we default to 128 on all AArch64 — over-aligning on 64-byte ARM is harmless.
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__ppc64__)
inline constexpr std::size_t cache_line_size = 128;
#else
inline constexpr std::size_t cache_line_size = 64;
#endif

// Lock-free multi-producer single-consumer queue.
// Intrusive Michael-Scott variant with pooled nodes.
template <typename T>
class mpsc_queue {
    struct node {
        std::optional<T> value;
        std::atomic<node*> next{nullptr};
    };

    alignas(cache_line_size) std::atomic<node*> tail_;
    alignas(cache_line_size) node* head_;

public:
    mpsc_queue()
    {
        auto* sentinel = new node{};
        head_ = sentinel;
        tail_.store(sentinel, std::memory_order_relaxed);
    }

    ~mpsc_queue()
    {
        while (pop()) {}
        delete head_;
    }

    mpsc_queue(const mpsc_queue&) = delete;
    mpsc_queue& operator=(const mpsc_queue&) = delete;

    // Thread-safe — may be called from any thread.
    void push(T value)
    {
        auto* n = new node{std::move(value)};
        auto* prev = tail_.exchange(n, std::memory_order_acq_rel);
        prev->next.store(n, std::memory_order_release);
    }

    // Single-consumer only.
    [[nodiscard]] std::optional<T> pop()
    {
        auto* next = head_->next.load(std::memory_order_acquire);
        if (!next)
            return std::nullopt;

        auto value = std::move(next->value);
        delete head_;
        head_ = next;
        return value;
    }
};

} // namespace prism
