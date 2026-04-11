#pragma once

#include <functional>
#include <unordered_set>
#include <vector>

namespace prism::core {

struct DeferredEmit {
    void* sender;
    std::function<void()> emit_fn;
};

struct TransactionState {
    int depth = 0;
    std::vector<DeferredEmit> queue;
};

inline TransactionState& current_transaction() {
    thread_local TransactionState state;
    return state;
}

inline bool transaction_active() {
    return current_transaction().depth > 0;
}

} // namespace prism::core

namespace prism {

struct TransactionGuard {
    TransactionGuard() { ++core::current_transaction().depth; }

    ~TransactionGuard() {
        auto& tx = core::current_transaction();
        if (--tx.depth == 0)
            flush(tx);
    }

    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;

private:
    static void flush(core::TransactionState& tx) {
        // Process the queue in waves.  Each wave coalesces the current queue
        // by sender, then fires callbacks while holding depth=1 so that
        // cascading recomputes (e.g. Derived<T> in a diamond graph) push new
        // entries back into the queue rather than firing immediately.  Repeat
        // until no new entries are produced.
        while (!tx.queue.empty()) {
            std::vector<core::DeferredEmit> coalesced;
            std::unordered_set<void*> seen;
            for (auto it = tx.queue.rbegin(); it != tx.queue.rend(); ++it) {
                if (seen.insert(it->sender).second)
                    coalesced.push_back(std::move(*it));
            }
            tx.queue.clear();
            // Keep depth=1 while firing so cascades queue, not fire.
            tx.depth = 1;
            for (auto it = coalesced.rbegin(); it != coalesced.rend(); ++it)
                it->emit_fn();
            tx.depth = 0;
        }
    }
};

template <typename F>
void transaction(F&& fn) {
    TransactionGuard tx;
    std::forward<F>(fn)();
}

} // namespace prism
