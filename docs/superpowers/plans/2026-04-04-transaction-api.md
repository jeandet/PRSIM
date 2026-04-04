# Transaction API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a transaction API that defers and coalesces `Field<T>`/`State<T>` notifications so that N field mutations produce at most one callback per field, fired at commit.

**Architecture:** Thread-local transaction state with depth counter for flat nesting. `ObservableValue::set()` checks a flag and enqueues deferred emits instead of calling `SenderHub::emit()` directly. `TransactionGuard` (RAII) and `transaction()` (lambda) provide the public API. At commit, the queue is deduplicated by sender address (last value wins) and flushed in original order.

**Tech Stack:** C++20, doctest, Meson

---

## File Map

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `include/prism/core/transaction.hpp` | TransactionState, TransactionGuard, transaction() |
| Modify | `include/prism/core/field.hpp` | Add transaction check to ObservableValue::set() |
| Modify | `include/prism/prism.hpp` | Include transaction.hpp in master header |
| Create | `tests/test_transaction.cpp` | All transaction tests |
| Modify | `tests/meson.build` | Register test_transaction |

---

### Task 1: Create transaction.hpp with TransactionState and helpers

**Files:**
- Create: `include/prism/core/transaction.hpp`

- [ ] **Step 1: Create the transaction header**

```cpp
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
        std::vector<core::DeferredEmit> coalesced;
        std::unordered_set<void*> seen;
        for (auto it = tx.queue.rbegin(); it != tx.queue.rend(); ++it) {
            if (seen.insert(it->sender).second)
                coalesced.push_back(std::move(*it));
        }
        tx.queue.clear();
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

- [ ] **Step 2: Commit**

```bash
git add include/prism/core/transaction.hpp
git commit -m "feat: add transaction.hpp with TransactionGuard and transaction()"
```

---

### Task 2: Modify ObservableValue::set() to defer during transactions

**Files:**
- Modify: `include/prism/core/field.hpp`

- [ ] **Step 1: Write the failing test for deferred notification**

Create `tests/test_transaction.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/field.hpp>
#include <prism/core/transaction.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

TEST_CASE("transaction defers callback until commit") {
    prism::Field<int> f{0};
    int calls = 0;
    int last_value = -1;
    auto conn = f.on_change().connect([&](const int& v) {
        ++calls;
        last_value = v;
    });

    prism::transaction([&] {
        f.set(42);
        CHECK(calls == 0);
    });

    CHECK(calls == 1);
    CHECK(last_value == 42);
}
```

- [ ] **Step 2: Register the test in meson.build**

Add to the `headless_tests` dict in `tests/meson.build`:

```
  'transaction' : files('test_transaction.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test -C builddir transaction --print-errorlogs`
Expected: FAIL — `calls == 0` inside the lambda will fail because `set()` still emits immediately.

- [ ] **Step 4: Modify ObservableValue::set() to check transaction state**

In `include/prism/core/field.hpp`, add `#include <prism/core/transaction.hpp>` and change `set()`:

```cpp
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
```

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test -C builddir transaction --print-errorlogs`
Expected: PASS

- [ ] **Step 6: Run all existing field tests to verify no regression**

Run: `meson test -C builddir field state --print-errorlogs`
Expected: PASS — without a transaction active, behavior is identical.

- [ ] **Step 7: Commit**

```bash
git add include/prism/core/field.hpp tests/test_transaction.cpp tests/meson.build
git commit -m "feat: defer Field/State notifications during transactions"
```

---

### Task 3: Test coalescing — multiple sets to same field

**Files:**
- Modify: `tests/test_transaction.cpp`

- [ ] **Step 1: Add coalescing test**

Append to `tests/test_transaction.cpp`:

```cpp
TEST_CASE("transaction coalesces multiple sets to same field") {
    prism::Field<int> f{0};
    int calls = 0;
    int last_value = -1;
    auto conn = f.on_change().connect([&](const int& v) {
        ++calls;
        last_value = v;
    });

    prism::transaction([&] {
        f.set(1);
        f.set(2);
        f.set(3);
    });

    CHECK(calls == 1);
    CHECK(last_value == 3);
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `meson test -C builddir transaction --print-errorlogs`
Expected: PASS — dedup by sender address keeps only the last entry, which reads `value` (3) at flush time.

- [ ] **Step 3: Commit**

```bash
git add tests/test_transaction.cpp
git commit -m "test: transaction coalesces multiple sets to same field"
```

---

### Task 4: Test multiple fields in one transaction

**Files:**
- Modify: `tests/test_transaction.cpp`

- [ ] **Step 1: Add multi-field test**

Append to `tests/test_transaction.cpp`:

```cpp
TEST_CASE("transaction with multiple fields fires each callback once") {
    prism::Field<int> a{0};
    prism::Field<std::string> b{""};
    int a_calls = 0, b_calls = 0;
    auto c1 = a.on_change().connect([&](const int&) { ++a_calls; });
    auto c2 = b.on_change().connect([&](const std::string&) { ++b_calls; });

    prism::transaction([&] {
        a.set(1);
        b.set("hello");
        CHECK(a_calls == 0);
        CHECK(b_calls == 0);
    });

    CHECK(a_calls == 1);
    CHECK(b_calls == 1);
    CHECK(a.get() == 1);
    CHECK(b.get() == "hello");
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `meson test -C builddir transaction --print-errorlogs`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add tests/test_transaction.cpp
git commit -m "test: transaction with multiple fields fires each once"
```

---

### Task 5: Test no-change, empty transaction, and unchanged behavior without transaction

**Files:**
- Modify: `tests/test_transaction.cpp`

- [ ] **Step 1: Add three edge-case tests**

Append to `tests/test_transaction.cpp`:

```cpp
TEST_CASE("transaction: set to same value produces no callback") {
    prism::Field<int> f{5};
    int calls = 0;
    auto conn = f.on_change().connect([&](const int&) { ++calls; });

    prism::transaction([&] {
        f.set(5);
    });

    CHECK(calls == 0);
}

TEST_CASE("empty transaction does not crash") {
    prism::transaction([&] {
        // nothing
    });
}

TEST_CASE("without transaction, callbacks fire immediately") {
    prism::Field<int> f{0};
    int calls = 0;
    auto conn = f.on_change().connect([&](const int&) { ++calls; });

    f.set(1);
    CHECK(calls == 1);
    f.set(2);
    CHECK(calls == 2);
}
```

- [ ] **Step 2: Run test to verify all pass**

Run: `meson test -C builddir transaction --print-errorlogs`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add tests/test_transaction.cpp
git commit -m "test: edge cases — no-change, empty, and non-transactional"
```

---

### Task 6: Test nested transactions

**Files:**
- Modify: `tests/test_transaction.cpp`

- [ ] **Step 1: Add nested transaction test**

Append to `tests/test_transaction.cpp`:

```cpp
TEST_CASE("nested transactions: callbacks fire only at outermost commit") {
    prism::Field<int> f{0};
    int calls = 0;
    int last_value = -1;
    auto conn = f.on_change().connect([&](const int& v) {
        ++calls;
        last_value = v;
    });

    prism::transaction([&] {
        f.set(1);

        prism::transaction([&] {
            f.set(2);
            CHECK(calls == 0);
        });

        // inner transaction committed, but outer still active
        CHECK(calls == 0);
        f.set(3);
    });

    CHECK(calls == 1);
    CHECK(last_value == 3);
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `meson test -C builddir transaction --print-errorlogs`
Expected: PASS — inner TransactionGuard decrements depth to 1, no flush. Outer decrements to 0, flushes. Dedup keeps last entry for the field, emits with value 3.

- [ ] **Step 3: Commit**

```bash
git add tests/test_transaction.cpp
git commit -m "test: nested transactions defer to outermost commit"
```

---

### Task 7: Test TransactionGuard directly

**Files:**
- Modify: `tests/test_transaction.cpp`

- [ ] **Step 1: Add guard API test**

Append to `tests/test_transaction.cpp`:

```cpp
TEST_CASE("TransactionGuard works identically to transaction()") {
    prism::Field<int> f{0};
    int calls = 0;
    int last_value = -1;
    auto conn = f.on_change().connect([&](const int& v) {
        ++calls;
        last_value = v;
    });

    {
        prism::TransactionGuard tx;
        f.set(10);
        f.set(20);
        CHECK(calls == 0);
    }

    CHECK(calls == 1);
    CHECK(last_value == 20);
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `meson test -C builddir transaction --print-errorlogs`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add tests/test_transaction.cpp
git commit -m "test: TransactionGuard RAII API"
```

---

### Task 8: Add transaction.hpp to master include and State test

**Files:**
- Modify: `include/prism/prism.hpp`
- Modify: `tests/test_transaction.cpp`

- [ ] **Step 1: Add State transaction test**

Append to `tests/test_transaction.cpp`:

```cpp
#include <prism/core/state.hpp>

TEST_CASE("transaction works with State<T> too") {
    prism::core::State<int> s{0};
    int calls = 0;
    auto conn = s.on_change().connect([&](const int&) { ++calls; });

    prism::transaction([&] {
        s.set(1);
        s.set(2);
        CHECK(calls == 0);
    });

    CHECK(calls == 1);
    CHECK(s.get() == 2);
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `meson test -C builddir transaction --print-errorlogs`
Expected: PASS — State inherits ObservableValue, same set() path.

- [ ] **Step 3: Add transaction.hpp to master include**

In `include/prism/prism.hpp`, add after the `#include <prism/core/field.hpp>` line:

```cpp
#include <prism/core/transaction.hpp>
```

- [ ] **Step 4: Run full test suite to verify no regressions**

Run: `meson test -C builddir --print-errorlogs`
Expected: All tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/prism.hpp tests/test_transaction.cpp
git commit -m "feat: include transaction.hpp in master header, add State<T> test"
```
