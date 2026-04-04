# Transaction API Design

## Problem

`Field<T>::set()` fires `on_change` callbacks synchronously on every call. When mutating multiple fields together, observers see intermediate inconsistent states and do redundant work. A transaction API defers and coalesces notifications so that N field mutations produce at most one callback per field, fired atomically at commit.

## Non-goals

- Repaint coalescing (already handled — dirty flags coalesce per frame)
- Rollback / undo on failure
- Cross-thread transactions
- Batching arbitrary `SenderHub` usage (only `ObservableValue`-based types)

## Design decisions

| Decision | Choice | Rationale |
|---|---|---|
| What to defer | All `SenderHub` callbacks (user + dirty-marking) | Dirty-marking connects through the same `field.on_change()` SenderHub, so deferring `emit()` defers both user callbacks and `set_dirty()` calls. This is correct: no intermediate dirty marks during a transaction. |
| Coalescing | Each field's callback fires at most once with final value | Matches "atomic update" mental model |
| Nesting | Flat — inner transactions absorbed into outer | Composable; library code can use transactions freely |
| Rollback | None | Keeps scope minimal; values update immediately, only notifications defer |
| Scope mechanism | Lambda + RAII guard | Lambda for 90% case, guard for complex control flow |
| Transaction state | Thread-local | App-thread concern, zero synchronization overhead |
| Intercept point | `ObservableValue::set()` | No type erasure needed; scoped to Field/State |

## Architecture

### Thread-local transaction state

```cpp
namespace prism::core {

struct DeferredEmit {
    void* sender;                    // address of SenderHub, for dedup
    std::function<void()> emit_fn;   // reads final value at flush time
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
```

### ObservableValue::set() modification

One branch added — if transaction is active, enqueue instead of emit:

```cpp
void set(T new_value) {
    if (value == new_value) return;
    value = std::move(new_value);
    if (transaction_active()) {
        auto& tx = current_transaction();
        tx.queue.push_back({
            static_cast<void*>(&changed_),
            [this] { changed_.emit(value); }
        });
    } else {
        changed_.emit(value);
    }
}
```

The lambda captures `this` and reads `value` at flush time (not enqueue time). Multiple sets to the same field enqueue multiple entries, but dedup keeps only the last one.

### TransactionGuard and lambda API

```cpp
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
        // Deduplicate: walk reverse, keep first occurrence per sender
        std::vector<core::DeferredEmit> coalesced;
        std::unordered_set<void*> seen;
        for (auto it = tx.queue.rbegin(); it != tx.queue.rend(); ++it) {
            if (seen.insert(it->sender).second)
                coalesced.push_back(std::move(*it));
        }
        tx.queue.clear();
        // Fire in original order (coalesced was built in reverse)
        for (auto it = coalesced.rbegin(); it != coalesced.rend(); ++it)
            it->emit_fn();
    }
};

template <typename F>
void transaction(F&& fn) {
    TransactionGuard tx;
    std::forward<F>(fn)();
}

} // namespace prism
```

Flat nesting: inner guard increments depth to 2, its destructor decrements to 1 — no flush. Only the outermost destructor (depth → 0) calls `flush()`.

## File placement

- `include/prism/core/transaction.hpp` — TransactionState, TransactionGuard, transaction()
- Modify `include/prism/core/field.hpp` — add transaction check to ObservableValue::set()
- `include/prism/prism.hpp` — include transaction.hpp in master header
- `tests/test_transaction.cpp` — new test file

## Test plan

1. **Single field, single set** — callback fires once at transaction end, not during
2. **Single field, multiple sets** — callback fires once with final value (coalescing)
3. **Multiple fields** — all callbacks fire at commit, each once
4. **No change** — set to same value inside transaction, no callback
5. **Nested transactions** — callbacks only fire when outermost commits
6. **No transaction** — existing behavior unchanged (immediate callbacks)
7. **Empty transaction** — open and close with no sets, no crash
8. **Guard and lambda** — both APIs produce identical behavior
