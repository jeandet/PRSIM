#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/function.h>

#include <prism/core/field.hpp>
#include <prism/core/shared.hpp>
#include <prism/core/channel.hpp>
#include <prism/core/connection.hpp>
#include <prism/core/error_hub.hpp>
#include <prism/core/transaction.hpp>
#include <prism/app/model_app.hpp>
#include <prism/app/backend.hpp>
#include <prism/ui/delegate.hpp> // Slider<T>, Checkbox, Orientation — for BoundSliderValue/BoundCheckboxValue
#include <prism/widgets/plot.hpp>
#include <prism/ui/tree.hpp>

#include <prism/app/headless_window.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

namespace nb = nanobind;
using namespace prism::core;
using namespace prism::app;

// Defined near NB_MODULE below, alongside the rest of the prism.on_error() error hub —
// forward-declared here since every Python callback wrapper in this file uses it.
static void report_python_callback_error();

// Python-facing spelling of a scalar field type, for TypeError messages (validator
// rejects, derived recompute cast failures).
template <typename T>
static const char* py_type_name() {
    if constexpr (std::is_same_v<T, int>) return "int";
    else if constexpr (std::is_same_v<T, double>) return "float";
    else if constexpr (std::is_same_v<T, std::string>) return "str";
    else if constexpr (std::is_same_v<T, bool>) return "bool";
    else return "value";
}

// Global PostHandle for off-thread posting (set during prism.run, cleared after).
// Holds weak_ptrs to the mutation queue so post after model_app returns is not UAF.
static std::mutex g_handle_mutex;
static std::optional<AppContext::PostHandle> g_post_handle;
static std::atomic<bool> g_has_handle{false};
static std::atomic<bool> g_run_guard{false};
static std::atomic<bool> g_app_closed{false};
// simplify: g_app_closed is global, not per-generation. After run we clear it
// immediately so next test's pre-run dispatch is NoApp (direct) not Closed.
// This drops the post-close guarantee for late posts from the *old* Model
// after the next run starts — acceptable: production runs once, pytest
// reuses the same process sequentially. Proper per-generation tracking
// would keep Closed for old-gen handles until next gen installs.

// Test-only: makes the next run()/_run_headless() call throw immediately after
// acquiring g_run_guard, before touching the backend/model_app — a stand-in for
// model_app() itself throwing (no real path exists to trigger that from Python;
// every callback model_app calls synchronously is already exception-guarded).
// Exercises RunGuardReset below: a second run() after the forced failure must work.
static std::atomic<bool> g_fail_next_run{false};

// run()/_run_headless() must reset g_run_guard/g_has_handle/g_post_handle/g_app_closed
// on every exit path, including model_app() throwing — otherwise a failed run leaves
// g_run_guard stuck true and every subsequent run()/_run_headless() call raises
// "prism.run already running" forever. Constructed right after the CAS that claims
// g_run_guard, so its destructor always runs exactly once per successful claim,
// success or exception.
struct RunGuardReset {
    ~RunGuardReset() {
        {
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle.reset();
            g_app_closed.store(true, std::memory_order_release);
        }
        g_has_handle.store(false, std::memory_order_release);
        g_run_guard.store(false, std::memory_order_release);
        g_app_closed.store(false, std::memory_order_release);
    }
};

enum class PostResult { Posted, NoApp, Closed };

static void drain_queue_loop(const std::shared_ptr<mpsc_queue<std::function<void()>>>& q,
                             const std::shared_ptr<std::atomic<bool>>& sf,
                             const std::shared_ptr<std::function<void()>>& tp) {
    do {
        prism::app::detail_in_mutation_batch = true;
        while (auto f = q->pop()) {
            try {
                (*f)();
            } catch (...) {
                prism::core::report_unhandled_error(std::current_exception());
            }
        }
        prism::app::detail_in_mutation_batch = false;
        if (tp && *tp) {
            try {
                (*tp)();
            } catch (...) {
                prism::core::report_unhandled_error(std::current_exception());
            }
        }
        sf->store(false, std::memory_order_release);
        if (q->empty()) break;
        bool exp = false;
        if (!sf->compare_exchange_strong(exp, true, std::memory_order_acq_rel)) break;
    } while (true);
}

static PostResult try_post_via_handle_impl(std::function<void()> fn, bool allow_logic_thread) {
    if (!allow_logic_thread && prism::app::detail_is_logic_thread) return PostResult::NoApp;
    // Startup window: g_run_guard true but g_has_handle not yet set (setup hasn't run).
    // Don't fall back to direct unsync write — spin briefly for handle to appear.
    // 1000ms survives slow debug/CI runners; still NoApp/Closed check below handles overflow.
    // Only spin when this thread actually holds the GIL: that's the case where another
    // Python thread is mid-setup and releasing the GIL here lets it finish. The initial
    // view build (registry.add in model_app) calls this without the GIL (it runs under
    // _run_headless's own gil_scoped_release) — there setup can never complete regardless
    // of how long we spin, so skip straight to the direct read/dispatch below instead of
    // burning the full 1000ms.
    if (g_run_guard.load(std::memory_order_acquire) && !g_has_handle.load(std::memory_order_acquire) &&
        Py_IsInitialized() && PyGILState_Check()) {
        nb::gil_scoped_release release;
        for (int i = 0; i < 1000 && !g_has_handle.load(std::memory_order_acquire); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::optional<AppContext::PostHandle> hopt;
    {
        std::lock_guard<std::mutex> lk(g_handle_mutex);
        if (!g_has_handle.load(std::memory_order_acquire) || !g_post_handle) {
            return g_app_closed.load(std::memory_order_acquire) ? PostResult::Closed : PostResult::NoApp;
        }
        hopt = *g_post_handle;
    }
    auto& h = *hopt;
    auto q = h.queue.lock();
    if (!q) return PostResult::Closed;
    auto closed_flag = h.closed.lock();
    if (!closed_flag || closed_flag->load(std::memory_order_acquire)) {
        g_app_closed.store(true, std::memory_order_release);
        return PostResult::Closed;
    }
    auto sched_flag = h.scheduled.lock();
    if (!sched_flag) return PostResult::Closed;
    auto tail = h.drain_publish.lock();
    q->push(std::move(fn));
    bool expected = false;
    if (sched_flag->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        auto qq = q;
        auto sf = sched_flag;
        auto tp = tail;
        auto sch = h.sched;
        exec::start_detached(stdexec::schedule(sch) | stdexec::then([qq, sf, tp, sch] {
            // AppContext::post's drain is now wrapped via logic_wrapper; keep GIL here too
            // for the try_post path which bypasses AppContext::post. Once the interpreter
            // is finalized, nothing queued here may run — it may hold Python callbacks.
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire gil;
            drain_queue_loop(qq, sf, tp);
        }));
    }
    return PostResult::Posted;
}

static bool try_post_via_handle(std::function<void()> fn) {
    return try_post_via_handle_impl(std::move(fn), false) == PostResult::Posted;
}

static PostResult try_post_any_thread(std::function<void()> fn) {
    return try_post_via_handle_impl(std::move(fn), true);
}

static void ensure_idle_wake() {
    if (prism::app::detail_in_mutation_batch) return;
    (void)try_post_any_thread([] {});
}

#include <future>

template <typename T>
T dispatch_sync_read(std::function<T()> reader) {
    if (prism::app::detail_is_logic_thread) return reader();
    // Startup window: spin briefly like the write path, and same GIL-holding
    // condition — see try_post_via_handle_impl. SlotDerived::get() is called from the
    // initial widget tree build (registry.add in model_app), which runs on the calling
    // thread under _run_headless's own gil_scoped_release (no GIL held here); skip the
    // spin there and fall straight through to the direct read below.
    if (g_run_guard.load(std::memory_order_acquire) && !g_has_handle.load(std::memory_order_acquire) &&
        Py_IsInitialized() && PyGILState_Check()) {
        nb::gil_scoped_release release;
        for (int i = 0; i < 1000 && !g_has_handle.load(std::memory_order_acquire); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Pre-run or post-close: direct read (single-threaded / drained).
    // Shutdown narrow window: closed=true but a drain continuation may still
    // be queued; wait briefly for the queue to empty before unsync read.
    {
        std::shared_ptr<mpsc_queue<std::function<void()>>> qcopy;
        bool do_direct = false;
        {
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            if (!g_has_handle.load(std::memory_order_acquire) || !g_post_handle) return reader();
            if (g_app_closed.load(std::memory_order_acquire)) {
                qcopy = g_post_handle->queue.lock();
                do_direct = true;
            } else {
                auto closed_flag = g_post_handle->closed.lock();
                if (!closed_flag || closed_flag->load(std::memory_order_acquire)) {
                    qcopy = g_post_handle->queue.lock();
                    do_direct = true;
                }
            }
        }
        if (do_direct) {
            if (qcopy) {
                for (int i = 0; i < 50 && !qcopy->empty(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return reader();
        }
    }
    auto prom = std::make_shared<std::promise<T>>();
    auto fut = prom->get_future();
    PostResult pr = try_post_via_handle_impl([reader = std::move(reader), prom]() mutable {
        try { prom->set_value(reader()); }
        catch (...) { try { prom->set_exception(std::current_exception()); } catch (...) {} }
    }, false);
    if (pr != PostResult::Posted) return reader(); // fallback: NoApp/Closed handled above, but safety
    // Block caller (off logic thread) until logic thread runs reader — release GIL while
    // waiting, same guard as the spins above (this thread may not hold the GIL here either).
    {
        std::optional<nb::gil_scoped_release> rel;
        if (Py_IsInitialized() && PyGILState_Check()) rel.emplace();
        fut.wait();
    }
    return fut.get();
}

// Transaction buffering — Python thread-local coalescing into one logic-thread closure.
// Matches doc/design/python-sdk.md §2: `with prism.transaction():` enqueues single closure.
// Uses C++ thread_local(TransactionState) semantics but buffered per-Python-thread pre-dispatch.
inline thread_local int txn_depth = 0;
inline thread_local std::vector<std::function<void()>> txn_queue;
inline thread_local std::vector<size_t> txn_marks;
inline bool txn_active() { return txn_depth > 0; }

inline void txn_flush_batch() {
    if (txn_queue.empty()) return;
    auto batch = std::move(txn_queue);
    txn_queue.clear();
    txn_queue.shrink_to_fit();
    txn_marks.clear();
    if (prism::app::detail_is_logic_thread) {
        // GIL needed: field->set will emit copying handlers holding Python objects.
        nb::gil_scoped_acquire gil;
        prism::TransactionGuard g;
        for (auto& fn : batch) fn();
        // If inside outer batch drain, defer publish to outer tail; else wake directly via tail if available.
        if (prism::app::detail_in_mutation_batch) return;
        // Try direct tail if we have handle, else fallback to coalesced wake.
        {
            std::optional<AppContext::PostHandle> hopt;
            {
                std::lock_guard<std::mutex> lk(g_handle_mutex);
                if (g_has_handle.load(std::memory_order_acquire) && g_post_handle) hopt = *g_post_handle;
            }
            if (hopt) {
                auto tail = hopt->drain_publish.lock();
                auto closed_flag = hopt->closed.lock();
                if (tail && *tail && closed_flag && !closed_flag->load(std::memory_order_acquire)) {
                    (*tail)();
                    return;
                }
            }
        }
        ensure_idle_wake();
        return;
    }
    // Off-thread: distinguish no-app (direct) vs closed (drop) vs live (post)
    // NoApp takes priority after run has cleared handle — future txns are direct, not dropped.
    PostResult pr;
    {
        std::lock_guard<std::mutex> lk(g_handle_mutex);
        if (!g_has_handle.load(std::memory_order_acquire) || !g_post_handle) pr = PostResult::NoApp;
        else if (g_app_closed.load(std::memory_order_acquire)) pr = PostResult::Closed;
        else pr = PostResult::Posted; // will attempt post below
    }
    if (pr == PostResult::NoApp) {
        prism::TransactionGuard g;
        for (auto& fn : batch) fn();
        return;
    }
    if (pr == PostResult::Closed) return; // drop batch per spec
    (void)try_post_via_handle([batch = std::move(batch)]() mutable {
        prism::TransactionGuard g;
        for (auto& fn : batch) fn();
    });
}

template <typename T>
inline bool txn_buffer_or_dispatch(std::shared_ptr<void> keep, Field<T>* field, const T& v) {
    if (!txn_active()) return false;
    T copy = v;
    txn_queue.emplace_back([keep, field, copy = std::move(copy)]() mutable {
        field->set(std::move(copy));
    });
    return true;
}

// Helper to post or direct-set a Field. `keep` is the owning shared_ptr (standalone
// handle state, or a Bound* handle's SlotBase owner) that the posted closure must
// hold so `field` cannot be freed before the logic thread runs it.
template <typename T>
void field_set_dispatch(std::shared_ptr<void> keep, Field<T>* field, T v) {
    if (txn_buffer_or_dispatch(keep, field, v)) return;
    if (!prism::app::detail_is_logic_thread) {
        T copy = v;
        auto res = try_post_via_handle_impl([keep, field, copy = std::move(copy)]() mutable {
            field->set(std::move(copy));
        }, false);
        if (res == PostResult::Posted) return;
        if (res == PostResult::Closed) return; // post-close: no-op per spec (no direct fallback)
        // NoApp: fall through to direct (pre-run single-threaded)
    }
    if (prism::app::detail_is_logic_thread && Py_IsInitialized()) {
        nb::gil_scoped_acquire gil;
        field->set(std::move(v));
    } else {
        field->set(std::move(v));
    }
}

// apply_validator is defined near NB_MODULE (needs nb::dict/attr access on `self`);
// forward-declared here so field_add_dispatch below can call it.
template <typename T> T apply_validator(nb::object self, T v);

// Owns exactly one nb::object for a closure that may sit in the mutation queue and
// be destroyed with the GIL released (model_app.hpp:190) or after interpreter
// shutdown. Move-only (copied via shared_ptr<PyHolder> where a closure needs to be
// copy-constructible for std::function). The destructor re-acquires the GIL before
// releasing the Python reference if the interpreter is still alive; otherwise it
// leaks the reference (`.release()`, same idiom as g_observed_handles above) rather
// than decref'ing into a torn-down interpreter.
struct PyHolder {
    nb::object obj;
    PyHolder() = default;
    explicit PyHolder(nb::object o) : obj(std::move(o)) {}
    PyHolder(const PyHolder&) = delete;
    PyHolder& operator=(const PyHolder&) = delete;
    PyHolder(PyHolder&&) = default;
    PyHolder& operator=(PyHolder&&) = default;
    ~PyHolder() {
        // Check GIL-held first, not Py_IsInitialized(): see the SlotDerived
        // destructor's comment below for why Py_IsInitialized() alone is the
        // wrong signal (it can already read false during ordinary,
        // still-GIL-holding teardown at interpreter shutdown).
        if (!obj.ptr()) return;
        if (PyGILState_Check()) { obj.reset(); return; }
        if (!Py_IsInitialized()) { obj.release(); return; }
        nb::gil_scoped_acquire g;
        obj.reset();
    }
};

// Atomic `field.add(n)` for int/float fields: get()+set() must both happen on the
// logic thread (the only thread allowed to touch Field<T> directly) so concurrent
// add() calls from many threads never race on a read-modify-write — each becomes
// one closure that runs to completion before the next drains. `self` is the Python
// handle (BoundField<T>/FieldHandle<T>) so the field's validator, if any, can be
// applied to the *summed* value — the caller's thread never sees that value, so
// unlike set()/`.value =` it cannot validate before dispatch; a rejection here is
// reported via prism.on_error() instead of raising back to the calling thread.
// simplify: does not participate in prism.transaction() buffering (unlike
// field_set_dispatch) — add() always dispatches its own closure immediately.
//
// The posted closure must never capture `self`/an nb::object directly (queued
// closures are destroyed by run() with the GIL released — model_app.hpp:190). The
// common case (no validator installed) posts a closure holding only `keep`, the raw
// field pointer, and `n` — no Python objects at all. Only when a validator is
// present does the closure capture one, via a PyHolder behind a shared_ptr (so the
// closure itself stays trivially copyable without touching the GIL).
template <typename T>
void field_add_dispatch(nb::object self, std::shared_ptr<void> keep, Field<T>* field, T n) {
    nb::dict d = self.attr("__dict__");
    bool has_validator = d.contains("_prism_validator");
    std::function<void()> do_add;
    if (has_validator) {
        auto held_self = std::make_shared<PyHolder>(self);
        do_add = [held_self, field, n]() {
            T new_value = field->get() + n;
            try {
                new_value = apply_validator<T>(held_self->obj, new_value);
            } catch (...) {
                report_python_callback_error();
                return;
            }
            field->set(std::move(new_value));
        };
    } else {
        do_add = [field, n]() { field->set(field->get() + n); };
    }
    if (prism::app::detail_is_logic_thread) {
        do_add();
        return;
    }
    auto res = try_post_via_handle_impl([keep, do_add]() mutable { do_add(); }, false);
    if (res == PostResult::Posted) return;
    if (res == PostResult::Closed) return; // post-close: no-op per spec (no direct fallback)
    do_add(); // NoApp: pre-run, single-threaded
}

// Standalone-handle state (Field<T>/List<T> here, Shared<T>/Channel<T> below) is
// heap-allocated and tracked by a live count, exposed to Python as
// _standalone_state_alive_count() — a regression probe for the self-owning-hub leak
// fixed below: state must reach zero once every handle referencing it is gone and
// disconnected, not just have its Python wrapper object collected.
static std::atomic<int64_t> g_standalone_state_alive{0};

template <typename U, typename... Args>
std::shared_ptr<U> make_tracked_state(Args&&... args) {
    g_standalone_state_alive.fetch_add(1, std::memory_order_relaxed);
    return std::shared_ptr<U>(new U(std::forward<Args>(args)...), [](U* p) {
        delete p;
        g_standalone_state_alive.fetch_sub(1, std::memory_order_relaxed);
    });
}

// Wraps a Python observer callback for storage inside a C++ SenderHub::receivers_ entry
// without giving receivers_ its own strong reference to it. The callback's only strong
// reference is meant to live in the observing handle's __dict__["_prism_callbacks"]
// (appended by keep_connection() below) — a second, independent strong nb::callable copy
// held here would be a reference the cyclic GC can't see (plain C++ storage has no
// tp_traverse), which is exactly what used to pin a self-capturing observer's
// Model -> handle -> callback -> Model cycle forever, uncollectable, even once nothing
// else referenced the Model (Python's GC can only reclaim a cycle if every strong
// reference into it is visible via tp_traverse). A weakref doesn't contribute to the
// callback's refcount, so once the handle (and its callbacks list) becomes unreachable
// and is collected, this weakref starts resolving to None and operator() below silently
// no-ops — same observable behavior as an already-disconnected observer. Falls back to
// holding `cb` directly (old behavior) for the rare callable that can't be
// weak-referenced (some C-implemented callables) — such an observer keeps working, just
// without this fix's GC-visibility for that one case.
class WeakCallback {
public:
    explicit WeakCallback(nb::callable cb) {
        try {
            weak_.emplace(cb);
        } catch (nb::python_error&) {
            strong_ = std::move(cb);
        }
    }

    template <typename... Args>
    void operator()(Args&&... args) const {
        if (!Py_IsInitialized()) return;
        nb::gil_scoped_acquire acq;
        nb::object target = strong_ ? nb::object(strong_) : (*weak_)();
        if (!target || target.is_none()) return;
        try { target(std::forward<Args>(args)...); } catch (...) { report_python_callback_error(); }
    }

private:
    std::optional<nb::weakref> weak_;
    nb::callable strong_;
};

// Standalone field (owns storage) — for quick tests / non-model usage. State lives behind
// a shared_ptr (like SharedHandle/ChannelHandle below) so observe()'s Connection can
// keep_alive(field) instead of relying on nanobind's nb::keep_alive<0,1> on the handle
// itself — see the SharedHandle rationale comment below for why that matters.
template <typename T>
struct FieldHandle {
    std::shared_ptr<Field<T>> field;
    FieldHandle(T init) : field(make_tracked_state<Field<T>>(std::move(init))) {}
    FieldHandle(const FieldHandle&) = delete;
    FieldHandle& operator=(const FieldHandle&) = delete;
    FieldHandle(FieldHandle&&) = delete;
    FieldHandle& operator=(FieldHandle&&) = delete;
    T get() const {
        Field<T>* p = field.get();
        return dispatch_sync_read<T>([p](){ return p->get(); });
    }
    void set(T v) { field_set_dispatch(field, field.get(), std::move(v)); }
    Connection observe(nb::callable cb) {
        auto conn = field->on_change().connect(WeakCallback(cb));
        conn.keep_alive(field);
        return conn;
    }
};

// Standalone Shared<T>/Channel<T> handles (built via nb::init<>, not owned by a PyModel) are not
// in any PyModel::slots vector, so PyModel::drain() never reaches them. Register their drain
// functions here so PyModel::drain() can sweep them too — see drain_standalone() below.
//
// The registry holds weak_ptrs, not raw pointers: a Python observer callback invoked mid-sweep
// (GIL held) can drop the last reference to ANOTHER standalone handle, running its destructor
// and freeing its drain_fn while the snapshot below still references it. lock()ing each weak_ptr
// takes a shared_ptr copy that keeps the function alive for the duration of that one call, even
// if the handle dies inside it; an already-expired entry is simply skipped, never dereferenced.
struct StandaloneDrainers {
    std::mutex m;
    std::vector<std::weak_ptr<std::function<void()>>> fns;
    static StandaloneDrainers& instance() {
        static StandaloneDrainers inst;
        return inst;
    }
};

static void register_standalone_drainer(std::shared_ptr<std::function<void()>> fn) {
    auto& reg = StandaloneDrainers::instance();
    std::lock_guard<std::mutex> lk(reg.m);
    reg.fns.push_back(std::move(fn));
}

// Called from a handle's destructor after it has reset its own drain_fn shared_ptr (so its
// entry is now expired); sweeps out every expired entry, not just this one, so the registry
// doesn't accumulate dead weak_ptrs across many short-lived standalone handles.
static void prune_expired_standalone_drainers() {
    auto& reg = StandaloneDrainers::instance();
    std::lock_guard<std::mutex> lk(reg.m);
    reg.fns.erase(std::remove_if(reg.fns.begin(), reg.fns.end(),
                                  [](const auto& w) { return w.expired(); }),
                  reg.fns.end());
}

static void drain_standalone() {
    std::vector<std::weak_ptr<std::function<void()>>> snapshot;
    {
        auto& reg = StandaloneDrainers::instance();
        std::lock_guard<std::mutex> lk(reg.m);
        snapshot = reg.fns;
    }
    for (auto& weak : snapshot) {
        if (auto fn = weak.lock()) (*fn)();
    }
}

// Type-erased slot for PyModel view — defined before Bound* so they can hold shared_ptr to it.
//
// traverse()/clear() let PyModel's tp_traverse/tp_clear (see NB_MODULE below) participate
// in Python's cyclic GC: a slot that holds an nb::object reaching back into its owning
// Model (SlotDerived's py_fn/dep_keepalive_, SlotTree's py_src_holder) is otherwise a
// GC-invisible edge in the Model -> slots -> callback -> Model cycle. Plain Slot/SlotShared/
// SlotChannel/SlotList/SlotPlot hold no nb::object, so the no-op default is correct for them.
struct SlotBase {
    virtual ~SlotBase() = default;
    virtual void build(ViewBuilder& vb) = 0;
    virtual void drain() {}
    virtual void traverse(visitproc /*visit*/, void* /*arg*/) {}
    virtual void clear() {}
};
template <typename T>
struct Slot : SlotBase {
    Field<T> field;
    explicit Slot(T v) : field(std::move(v)) {}
    void build(ViewBuilder& vb) override { vb.widget(field); }
};
template <typename T>
struct SlotShared : SlotBase {
    Shared<T> shared;
    explicit SlotShared(T v) : shared(std::move(v)) {}
    void build(ViewBuilder& vb) override { vb.widget(shared); }
    void drain() override { shared.drain_notifications(); }
};
template <typename T>
struct SlotChannel : SlotBase {
    Channel<T> channel;
    SlotChannel() = default;
    void build(ViewBuilder& vb) override { (void)vb; /* invisible — drain via PyModel::drain */ }
    void drain() override { channel.drain_notifications(); }
};
template <typename T>
struct SlotList : SlotBase {
    List<T> list;
    explicit SlotList(std::vector<T> init = {}) { for (auto v : init) list.push_back(v); }
    void build(ViewBuilder& vb) override { vb.list(list); }
};
// Slider<T,O>/Checkbox (include/prism/ui/delegate.hpp) are plain structs
// (value/min/max/step, checked/label) each held whole inside one Field —
// unlike Slot<T>'s scalar Field<T>, there is no separate Field<double>/
// Field<bool> to point a BoundField at, so these get their own slot types.
//
// Orientation O is Slider<T,O>'s only template parameter and it does not
// affect the struct's data layout (value/min/max/step) at all — it only
// selects which Widget<Slider<T,O>> specialization renders it (delegate.hpp
// already defines that specialization generically over O, so instantiating
// it for Vertical here needs no new widget code). SlotSliderT<O> is
// templated purely so both orientations get their own Field<Slider<double,O>>
// storage; SlotSlider stays the Horizontal alias other code already refers
// to by that name.
template <Orientation O>
struct SlotSliderT : SlotBase {
    Field<Slider<double, O>> field;
    explicit SlotSliderT(Slider<double, O> v) : field(std::move(v)) {}
    void build(ViewBuilder& vb) override { vb.widget(field); }
};
using SlotSlider = SlotSliderT<Orientation::Horizontal>;
using SlotSliderV = SlotSliderT<Orientation::Vertical>;
struct SlotCheckbox : SlotBase {
    Field<Checkbox> field;
    explicit SlotCheckbox(Checkbox v) : field(std::move(v)) {}
    void build(ViewBuilder& vb) override { vb.widget(field); }
};

// Forward declarations for dispatch helpers used by Plot/Tree handles
inline void list_op_dispatch(std::shared_ptr<void> keep, std::function<void()> fn);
template <typename T> T dispatch_sync_read(std::function<T()> reader);
template <typename T> void field_set_dispatch(std::shared_ptr<void> keep, Field<T>* field, T v);

// replace_series() shared parsing/dispatch — one series (xs, ys, color) worth of C++ data,
// converted from Python objects on the calling thread before crossing to the logic thread.
struct ReplaceSeriesSpec {
    std::vector<double> vx, vy;
    std::string color_str;
};

// Accepts either (xs, ys) for one series (ys != None) or a single list of (xs, ys, color)
// tuples for many series (ys == None) — see BoundPlot/PlotHandle::replace_series binding.
inline std::vector<ReplaceSeriesSpec> parse_replace_series_args(nb::object xs_or_series, nb::object ys, nb::object color) {
    auto to_doubles = [](nb::object seq) {
        nb::list l = nb::cast<nb::list>(seq);
        std::vector<double> v; v.reserve(nb::len(l));
        for (auto h : l) v.push_back(nb::cast<double>(h));
        return v;
    };
    std::vector<ReplaceSeriesSpec> specs;
    if (ys.is_none()) {
        constexpr const char* shape_error = "replace_series(): each series must be (xs, ys) or (xs, ys, color)";
        for (auto item : nb::cast<nb::list>(xs_or_series)) {
            // nb::cast<nb::tuple> silently converts any sequence (incl. a bare list) via
            // PySequence_Tuple — check the real type first so a bare list/short tuple is
            // rejected here, not read out of bounds by tuple::operator[] below.
            if (!nb::isinstance<nb::tuple>(item)) throw nb::type_error(shape_error);
            nb::tuple t = nb::borrow<nb::tuple>(item);
            size_t n = nb::len(t);
            if (n != 2 && n != 3) throw nb::type_error(shape_error);
            ReplaceSeriesSpec spec;
            spec.vx = to_doubles(t[0]);
            spec.vy = to_doubles(t[1]);
            if (n > 2 && !t[2].is_none()) spec.color_str = nb::cast<std::string>(t[2]);
            specs.push_back(std::move(spec));
        }
    } else {
        ReplaceSeriesSpec spec;
        spec.vx = to_doubles(xs_or_series);
        spec.vy = to_doubles(ys);
        if (!color.is_none()) spec.color_str = nb::cast<std::string>(color);
        specs.push_back(std::move(spec));
    }
    return specs;
}

// List form (ys omitted) carries color per-series in each tuple — a top-level color/fill
// would silently do nothing, so reject it instead of accepting it as a no-op.
inline void reject_stray_kwargs_in_list_form(const nb::object& ys, const nb::object& color, bool fill) {
    if (ys.is_none() && (!color.is_none() || fill))
        throw nb::type_error("replace_series(): color/fill are not valid with the list form — set color per series in the (xs, ys, color) tuple");
}

inline void replace_series_dispatch(std::shared_ptr<void> keep, prism::plot::PlotModel* p, std::vector<ReplaceSeriesSpec> specs, float thickness, bool fill) {
    auto fn = [p, specs = std::move(specs), thickness, fill]() mutable {
        p->clear_series();
        for (auto& s : specs) {
            prism::plot::XYData data{std::move(s.vx), std::move(s.vy)};
            prism::plot::SeriesStyle style;
            style.thickness = thickness;
            style.fill = fill;
            if (s.color_str.size() == 7 && s.color_str[0] == '#') {
                int r = std::stoi(s.color_str.substr(1, 2), nullptr, 16);
                int g = std::stoi(s.color_str.substr(3, 2), nullptr, 16);
                int b = std::stoi(s.color_str.substr(5, 2), nullptr, 16);
                style.color = Color::rgba((uint8_t)r, (uint8_t)g, (uint8_t)b);
            }
            p->add_series(std::move(data), style);
        }
        p->notify();
    };
    list_op_dispatch(std::move(keep), std::move(fn));
}

// Plot support — Slot only (Bound* defined after list_op_dispatch)
struct SlotPlot : SlotBase {
    prism::plot::PlotModel plot;
    void build(ViewBuilder& vb) override {
        vb.canvas(plot)
            .depends_on(plot.x_range).depends_on(plot.y_range)
            .depends_on(plot.view).depends_on(plot.cursor)
            .depends_on(plot.revision);
    }
};
// Standalone Plot handle — state lives behind a shared_ptr (like FieldHandle/ListHandle
// above) so posted mutation closures can capture it as `keep` and outlive a `del` that
// races the logic thread draining the post.
struct PlotHandle {
    std::shared_ptr<prism::plot::PlotModel> plot = std::make_shared<prism::plot::PlotModel>();
    void add_series(nb::list xs, nb::list ys, std::string color_str = "", float thickness = 2.f, bool fill = false) {
        std::vector<double> vx, vy;
        vx.reserve(nb::len(xs)); vy.reserve(nb::len(ys));
        for (auto h : xs) vx.push_back(nb::cast<double>(h));
        for (auto h : ys) vy.push_back(nb::cast<double>(h));
        auto* p = plot.get();
        auto fn = [p, vx = std::move(vx), vy = std::move(vy), color_str, thickness, fill]() mutable {
            prism::plot::XYData data{std::move(vx), std::move(vy)};
            prism::plot::SeriesStyle style; style.thickness=thickness; style.fill=fill;
            if (!color_str.empty() && color_str.size()==7 && color_str[0]=='#') {
                int r = std::stoi(color_str.substr(1,2), nullptr, 16);
                int g = std::stoi(color_str.substr(3,2), nullptr, 16);
                int b = std::stoi(color_str.substr(5,2), nullptr, 16);
                style.color = Color::rgba((uint8_t)r,(uint8_t)g,(uint8_t)b);
            }
            p->add_series(std::move(data), style);
        };
        list_op_dispatch(plot, std::move(fn));
    }
    void clear_series(){ auto* p=plot.get(); list_op_dispatch(plot, [p](){ p->clear_series(); }); }
    void notify(){ auto* p=plot.get(); list_op_dispatch(plot, [p](){ p->notify(); }); }
    // Single-post clear+add(+add...)+notify — see parse_replace_series_args for the two call forms.
    void replace_series(nb::object xs_or_series, nb::object ys = nb::none(), nb::object color = nb::none(),
                         float thickness = 2.f, bool fill = false) {
        reject_stray_kwargs_in_list_form(ys, color, fill);
        replace_series_dispatch(plot, plot.get(), parse_replace_series_args(xs_or_series, ys, color), thickness, fill);
    }
    void set_labels(nb::object x = nb::none(), nb::object y = nb::none()) {
        auto* p = plot.get();
        bool has_x = !x.is_none(), has_y = !y.is_none();
        std::string xs = has_x ? nb::cast<std::string>(x) : std::string();
        std::string ys = has_y ? nb::cast<std::string>(y) : std::string();
        list_op_dispatch(plot, [p, has_x, has_y, xs, ys](){
            if (has_x) p->x_label.set(xs);
            if (has_y) p->y_label.set(ys);
        });
    }
    void set_x_label(std::string s){ field_set_dispatch(plot, &plot->x_label, std::move(s)); }
    void set_y_label(std::string s){ field_set_dispatch(plot, &plot->y_label, std::move(s)); }
    std::string get_x_label() const { auto* p=plot.get(); return dispatch_sync_read<std::string>([p](){ return p->x_label.get(); }); }
    std::string get_y_label() const { auto* p=plot.get(); return dispatch_sync_read<std::string>([p](){ return p->y_label.get(); }); }
    size_t series_count() const { auto* p=plot.get(); return dispatch_sync_read<size_t>([p](){ return p->series_count(); }); }
    size_t series_len(size_t i) const { auto* p=plot.get(); return dispatch_sync_read<size_t>([p,i](){ return p->series_len(i); }); }
    void reset_view(){ auto* p=plot.get(); list_op_dispatch(plot, [p](){ p->reset_view(); }); }
};

// Tree support — Python-backed TreeSource
struct PythonTreeSource {
    static prism::ui::TreeSource make(nb::object obj) {
        auto held = std::make_shared<nb::object>(std::move(obj));
        prism::ui::TreeSource src;
        src.root_count = [held]() -> size_t {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "root_count")) return nb::cast<size_t>(o.attr("root_count")());
            return 0;
        };
        src.root_at = [held](size_t i) -> prism::ui::TreeNodeId {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "root_at")) {
                auto v = o.attr("root_at")(i);
                // allow negative Python hash -> uint64_t
                int64_t tmp = nb::cast<int64_t>(v);
                return static_cast<prism::ui::TreeNodeId>(tmp);
            }
            return 0;
        };
        src.child_count = [held](prism::ui::TreeNodeId id) -> size_t {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "child_count")) return nb::cast<size_t>(o.attr("child_count")(id));
            return 0;
        };
        src.child_at = [held](prism::ui::TreeNodeId id, size_t i) -> prism::ui::TreeNodeId {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "child_at")) {
                auto v = o.attr("child_at")(id, i);
                int64_t tmp = nb::cast<int64_t>(v);
                return static_cast<prism::ui::TreeNodeId>(tmp);
            }
            return 0;
        };
        src.label = [held](prism::ui::TreeNodeId id) -> std::string {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "label")) return nb::cast<std::string>(o.attr("label")(id));
            return std::to_string(id);
        };
        src.has_children = [held](prism::ui::TreeNodeId id) -> bool {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "has_children")) return nb::cast<bool>(o.attr("has_children")(id));
            return false;
        };
        src.attributes = [held](prism::ui::TreeNodeId id) -> std::vector<std::pair<std::string,std::string>> {
            nb::gil_scoped_acquire g;
            auto o = *held;
            std::vector<std::pair<std::string,std::string>> out;
            if (nb::hasattr(o, "attributes")) {
                auto res = o.attr("attributes")(id);
                if (!res.is_none()) {
                    auto d = nb::cast<nb::dict>(res);
                    for (auto kv : d) out.emplace_back(nb::cast<std::string>(kv.first), nb::cast<std::string>(kv.second));
                }
            }
            return out;
        };
        // optional icon
        src.icon = [held](prism::ui::TreeNodeId id) -> std::optional<std::string> {
            nb::gil_scoped_acquire g;
            auto o = *held;
            if (nb::hasattr(o, "icon")) {
                auto v = o.attr("icon")(id);
                if (!v.is_none()) return nb::cast<std::string>(v);
            }
            return std::nullopt;
        };
        return src;
    }
};
struct SlotTree : SlotBase {
    std::shared_ptr<nb::object> py_src_holder;
    prism::ui::TreeController ctrl;
    explicit SlotTree(prism::ui::TreeSource src) : ctrl(std::move(src)) {}
    explicit SlotTree(nb::object py_obj) : py_src_holder(std::make_shared<nb::object>(py_obj)), ctrl(PythonTreeSource::make(py_obj)) {}
    void build(ViewBuilder& vb) override { vb.tree(ctrl); }
    void traverse(visitproc visit, void* arg) override {
        if (py_src_holder) if (PyObject* o = py_src_holder->ptr()) visit(o, arg);
    }
    // Reset to None rather than an empty object: PythonTreeSource's callbacks
    // (captured shared_ptr<nb::object>) dereference *py_src_holder via
    // nb::hasattr on every call — None is a safe no-op source, a null
    // PyObject* is not.
    void clear() override {
        if (py_src_holder) *py_src_holder = nb::none();
    }
    // A `keep` shared_ptr<SlotBase> captured by a posted/queued closure (see
    // list_op_dispatch/field_set_dispatch above) can be the reference that drops
    // this SlotTree to zero — and the mutation queue itself is destroyed with the
    // GIL released (model_app.hpp:190). Releasing py_src_holder's PyObject* must
    // still happen under the GIL when the interpreter is alive; once it's gone,
    // leak instead of decref'ing into a torn-down interpreter (same idiom as
    // PyHolder above).
    ~SlotTree() override {
        // Check GIL-held first, not Py_IsInitialized() — see the SlotDerived
        // destructor's comment for why.
        if (!py_src_holder) return;
        if (PyGILState_Check()) { *py_src_holder = nb::object(); return; }
        if (!Py_IsInitialized()) { py_src_holder->release(); return; }
        nb::gil_scoped_acquire g;
        *py_src_holder = nb::object();
    }
};
// The posted reader may run on the logic thread after the interpreter is finalized
// (see try_post_via_handle_impl's drain path), so it must not construct any nb::
// object — TreeRow is plain C++ data, safe to copy there. The nb::list/nb::dict
// conversion happens back on the calling (GIL-holding) thread, in *_to_pylist below.
static std::vector<prism::ui::TreeRow> snapshot_tree_rows(const prism::ui::TreeController* p) {
    std::vector<prism::ui::TreeRow> out;
    out.reserve(p->rows.size());
    for (size_t i = 0; i < p->rows.size(); ++i) out.push_back(p->rows[i]);
    return out;
}
static nb::list tree_rows_to_pylist(const std::vector<prism::ui::TreeRow>& rows) {
    nb::list out;
    for (auto& r : rows) {
        nb::dict d;
        d["label"] = r.label;
        d["depth"] = r.depth;
        d["has_children"] = r.has_children;
        d["expanded"] = r.expanded;
        d["selected"] = r.selected;
        out.append(d);
    }
    return out;
}
struct BoundTree {
    std::shared_ptr<SlotBase> owner;
    prism::ui::TreeController* ctrl = nullptr;
    void refresh(){ if(ctrl){ auto* p=ctrl; list_op_dispatch(owner, [p](){ p->refresh(); }); } }
    nb::list rows(){
        if(!ctrl) return nb::list();
        auto* p = ctrl;
        auto snapshot = dispatch_sync_read<std::vector<prism::ui::TreeRow>>([p](){ return snapshot_tree_rows(p); });
        return tree_rows_to_pylist(snapshot);
    }
};
struct TreeHandle {
    std::shared_ptr<prism::ui::TreeController> ctrl;
    TreeHandle(prism::ui::TreeSource src): ctrl(std::make_shared<prism::ui::TreeController>(std::move(src))){}
    TreeHandle(nb::object py_obj): ctrl(std::make_shared<prism::ui::TreeController>(PythonTreeSource::make(py_obj))){}
};

// --- List dispatch helper (mirrors field_set_dispatch but for arbitrary op) ---
// `keep` is the owning shared_ptr (standalone handle state, or a Bound* handle's
// SlotBase owner) that any posted/buffered copy of `fn` carries along, so the object
// `fn` closes over cannot be freed before the logic thread runs it.
inline void list_op_dispatch(std::shared_ptr<void> keep, std::function<void()> fn) {
    if (txn_active()) { txn_queue.emplace_back([keep, fn]() mutable { fn(); }); return; }
    if (!prism::app::detail_is_logic_thread) {
        auto res = try_post_via_handle_impl([keep, fn]() mutable { fn(); }, false);
        if (res == PostResult::Posted) return;
        if (res == PostResult::Closed) return;
    }
    if (prism::app::detail_is_logic_thread && Py_IsInitialized()) {
        nb::gil_scoped_acquire g; fn();
    } else fn();
}

// Bound handle — references Field owned by PyModel via shared_ptr<SlotBase> (no Model cycle).
// Holding the Slot directly keeps the Field/SenderHub alive even after the Model is GC'd.
template <typename T>
struct BoundField {
    std::shared_ptr<SlotBase> owner;
    Field<T>* field = nullptr;
    T get() const {
        if (!field) return T{};
        Field<T>* p = field;
        return dispatch_sync_read<T>([p](){ return p->get(); });
    }
    void set(T v) {
        if (field) field_set_dispatch(owner, field, std::move(v));
    }
    Connection observe(nb::callable cb) {
        if (!field) throw nb::value_error("observe(): handle is not bound to a Model");
        auto owner_copy = owner;
        Field<T>* f = field;
        auto conn = f->on_change().connect(WeakCallback(cb));
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};

// Per-orientation slider ops shared by BoundSliderValue below. Each posts (or
// buffers under prism.transaction()) a single read-modify-write closure that
// runs entirely on the logic thread — the only thread allowed to touch
// Field<T> directly — so a concurrent set()/set_range() from another thread
// can never race a read against a write: whichever closure runs first is
// fully applied (including the fields it leaves untouched) before the next
// one reads.
template <Orientation O>
double slider_get(Field<Slider<double, O>>* field) {
    Field<Slider<double, O>>* p = field;
    return dispatch_sync_read<Slider<double, O>>([p](){ return p->get(); }).value;
}
template <Orientation O>
void slider_set(std::shared_ptr<void> keep, Field<Slider<double, O>>* field, double v) {
    Field<Slider<double, O>>* p = field;
    list_op_dispatch(std::move(keep), [p, v](){ auto s = p->get(); s.value = v; p->set(s); });
}
template <Orientation O>
nb::tuple slider_range(Field<Slider<double, O>>* field) {
    Field<Slider<double, O>>* p = field;
    auto s = dispatch_sync_read<Slider<double, O>>([p](){ return p->get(); });
    return nb::make_tuple(s.min, s.max);
}
template <Orientation O>
void slider_set_range(std::shared_ptr<void> keep, Field<Slider<double, O>>* field, double mn, double mx) {
    Field<Slider<double, O>>* p = field;
    list_op_dispatch(std::move(keep), [p, mn, mx](){ auto s = p->get(); s.min = mn; s.max = mx; p->set(s); });
}
template <Orientation O>
Connection slider_observe(Field<Slider<double, O>>* field, nb::callable cb) {
    Field<Slider<double, O>>* f = field;
    return f->on_change().connect([wc = WeakCallback(cb)](const Slider<double, O>& val) { wc(val.value); });
}

// Bound handle for a slider's inner value. Slider<double,O> holds
// value/min/max/step together in one struct behind one Field (see
// SlotSliderT above), so unlike BoundField<T> there is no Field<double> to
// point at directly. Orientation is a compile-time Slider<T,O> template
// parameter (see SlotSliderT's comment) — it doesn't affect the struct's
// layout, only which Widget<Slider<T,O>> renders it — so a Field<Slider<double,
// Horizontal>> and a Field<Slider<double,Vertical>> are two distinct C++
// types with identical semantics. Exactly one of field_h/field_v is set
// (chosen once at allocation, by orientation), and every method below just
// branches on which.
struct BoundSliderValue {
    std::shared_ptr<SlotBase> owner;
    Field<Slider<double, Orientation::Horizontal>>* field_h = nullptr;
    Field<Slider<double, Orientation::Vertical>>* field_v = nullptr;
    double get() const {
        if (field_h) return slider_get(field_h);
        if (field_v) return slider_get(field_v);
        return 0.0;
    }
    void set(double v) {
        if (field_h) slider_set(owner, field_h, v);
        else if (field_v) slider_set(owner, field_v, v);
    }
    nb::tuple range() const {
        if (field_h) return slider_range(field_h);
        if (field_v) return slider_range(field_v);
        return nb::make_tuple(0.0, 1.0);
    }
    void set_range(double mn, double mx) {
        if (field_h) slider_set_range(owner, field_h, mn, mx);
        else if (field_v) slider_set_range(owner, field_v, mn, mx);
    }
    Connection observe(nb::callable cb) {
        if (!field_h && !field_v) throw nb::value_error("observe(): handle is not bound to a Model");
        auto owner_copy = owner;
        Connection conn = field_h ? slider_observe(field_h, cb) : slider_observe(field_v, cb);
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};

// Bound handle for a checkbox's inner checked flag — same rationale as BoundSliderValue:
// Checkbox is a plain {checked, label} struct behind one Field, so label is cached here
// at construction rather than read back on every set().
struct BoundCheckboxValue {
    std::shared_ptr<SlotBase> owner;
    Field<Checkbox>* field = nullptr;
    std::string label;
    bool get() const {
        if (!field) return false;
        Field<Checkbox>* p = field;
        return dispatch_sync_read<Checkbox>([p](){ return p->get(); }).checked;
    }
    void set(bool v) {
        if (field) field_set_dispatch<Checkbox>(owner, field, Checkbox{v, label});
    }
    Connection observe(nb::callable cb) {
        if (!field) throw nb::value_error("observe(): handle is not bound to a Model");
        auto owner_copy = owner;
        Field<Checkbox>* f = field;
        auto conn = f->on_change().connect([wc = WeakCallback(cb)](const Checkbox& val) { wc(val.checked); });
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};

// Standalone / bound handles for Shared<T> and Channel<T>
//
// The Shared<T> itself lives behind a shared_ptr ("state"), not by value in the
// handle. drain_fn captures that shared_ptr (never `this`/a raw pointer into the
// handle), so if a Python callback running inside this handle's own
// drain_notifications() drops the handle's last Python reference, the state that
// still-running call is reading/writing outlives the call — only the (now
// pointless) handle wrapper goes away. keep_alive(state) on the returned
// Connection means the state also outlives the handle whenever something else
// (e.g. a Connection stored elsewhere) still needs it.
//
// observe()'s wrapper must NOT also capture `state`: the wrapper is stored inside
// state->on_change()'s receivers_, a member of *state itself, so a `state` capture
// there is a strong reference from the hub back to its own owning object — the hub
// leaks forever (only disconnect() removing the receiver breaks it) for no safety
// benefit, since drain_fn already keeps state alive across the whole drain call,
// callbacks included.
template <typename T>
struct SharedHandle {
    std::shared_ptr<Shared<T>> shared;
    std::shared_ptr<std::function<void()>> drain_fn;
    SharedHandle(T init) : shared(make_tracked_state<Shared<T>>(std::move(init))) {
        auto state = shared;
        drain_fn = std::make_shared<std::function<void()>>([state] { state->drain_notifications(); });
        register_standalone_drainer(drain_fn);
    }
    ~SharedHandle() {
        drain_fn.reset();
        prune_expired_standalone_drainers();
    }
    SharedHandle(const SharedHandle&) = delete;
    SharedHandle& operator=(const SharedHandle&) = delete;
    SharedHandle(SharedHandle&&) = delete;
    SharedHandle& operator=(SharedHandle&&) = delete;
    T get() const { return shared->get(); }
    void set(T v) { shared->set(std::move(v)); ensure_idle_wake(); }
    Connection observe(nb::callable cb) {
        auto conn = shared->on_change().connect(WeakCallback(cb));
        conn.keep_alive(shared);
        return conn;
    }
};
template <typename T>
struct BoundShared {
    std::shared_ptr<SlotBase> owner;
    Shared<T>* shared = nullptr;
    T get() const { return shared ? shared->get() : T{}; }
    void set(T v) {
        if (shared) { shared->set(std::move(v)); ensure_idle_wake(); }
    }
    Connection observe(nb::callable cb) {
        if (!shared) throw nb::value_error("observe(): handle is not bound to a Model");
        auto owner_copy = owner;
        Shared<T>* s = shared;
        auto conn = s->on_change().connect(WeakCallback(cb));
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};
// State ownership rationale (including why observe()'s wrapper captures only `cb`,
// never `state`) mirrors SharedHandle above.
template <typename T>
struct ChannelHandle {
    std::shared_ptr<Channel<T>> channel;
    std::shared_ptr<std::function<void()>> drain_fn;
    ChannelHandle() : channel(make_tracked_state<Channel<T>>()) {
        auto state = channel;
        drain_fn = std::make_shared<std::function<void()>>([state] { state->drain_notifications(); });
        register_standalone_drainer(drain_fn);
    }
    ~ChannelHandle() {
        drain_fn.reset();
        prune_expired_standalone_drainers();
    }
    ChannelHandle(const ChannelHandle&) = delete;
    ChannelHandle& operator=(const ChannelHandle&) = delete;
    ChannelHandle(ChannelHandle&&) = delete;
    ChannelHandle& operator=(ChannelHandle&&) = delete;
    void send(T v) { channel->send(std::move(v)); ensure_idle_wake(); }
    Connection observe(nb::callable cb) {
        auto conn = channel->on_receive().connect(WeakCallback(cb));
        conn.keep_alive(channel);
        return conn;
    }
};
template <typename T>
struct BoundChannel {
    std::shared_ptr<SlotBase> owner;
    Channel<T>* channel = nullptr;
    void send(T v) {
        if (channel) { channel->send(std::move(v)); ensure_idle_wake(); }
    }
    Connection observe(nb::callable cb) {
        if (!channel) throw nb::value_error("observe(): handle is not bound to a Model");
        auto owner_copy = owner;
        Channel<T>* c = channel;
        auto conn = c->on_receive().connect(WeakCallback(cb));
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};

// Derived — Python compute with C++ subscription
template <typename T>
struct SlotDerived : SlotBase {
    T value_{};
    nb::object py_fn = nb::none();
    SenderHub<const T&> changed_;
    std::vector<std::shared_ptr<SlotBase>> dep_owners_;
    std::vector<nb::object> dep_keepalive_; // keeps standalone handles alive
    std::vector<Connection> observers_;
    // Declared last so it's destroyed FIRST (C++ destroys members in reverse
    // declaration order): deps_'s Connections must disconnect from each
    // dependency's SenderHub before dep_owners_ (above) releases the
    // shared_ptr that keeps that dependency's Slot alive — the other order
    // frees the Slot first and deps_'s disconnect() then UAFs into it.
    std::vector<Connection> deps_;
    SlotDerived() = default;
    SlotDerived(nb::object fn, T init) : py_fn(std::move(fn)), value_(std::move(init)) {}
    T get() const {
        const T* p = &value_;
        return dispatch_sync_read<T>([p](){ return *p; });
    }
    SenderHub<const T&>& on_change() { return changed_; }
    void recompute() {
        T nv{};
        {
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire g;
            nb::object result;
            try { result = py_fn(); } catch (...) { report_python_callback_error(); return; }
            try {
                nv = nb::cast<T>(result);
            } catch (const std::bad_cast&) {
                // Not a cast<T> the caller can retry — a clear TypeError, routed through
                // on_error like any other derived-callback failure, rather than a silent
                // no-op (value_ untouched below) or a crash.
                std::string msg = std::string("derived function must return a ") + py_type_name<T>() +
                                   " (or raise); got " + nb::cast<std::string>(nb::repr(result));
                PyErr_SetString(PyExc_TypeError, msg.c_str());
                prism::core::report_unhandled_error(std::make_exception_ptr(nb::python_error()));
                return;
            }
        }
        if (nv == value_) return;
        value_ = std::move(nv);
        emit_or_defer(static_cast<void*>(&changed_), [this]{ changed_.emit(value_); });
    }
    void build(ViewBuilder& vb) override { vb.widget_generic<T>(*this); }
    void traverse(visitproc visit, void* arg) override {
        if (PyObject* o = py_fn.ptr()) visit(o, arg);
        for (auto& o : dep_keepalive_) if (PyObject* p = o.ptr()) visit(p, arg);
    }
    // Only release py_fn/dep_keepalive_ (the nb::objects that can form a cycle back
    // to the owning Model) — NOT deps_/dep_owners_. Those are a live cross-reference
    // into another slot's SenderHub (dep_owners_ is what keeps that slot's Field
    // alive at all past this Model's own teardown); breaking it here from
    // tp_clear, ahead of this SlotDerived's own destruction, races the owning
    // Model's slots vector destructor and can UAF into an already-freed SenderHub.
    // A recompute that still fires after py_fn is cleared is safe either way:
    // py_fn() on nb::none() raises a plain (caught, reported) TypeError.
    void clear() override {
        dep_keepalive_.clear();
        py_fn = nb::none();
    }
    // A `keep` shared_ptr<SlotBase> held by a posted/queued closure can be the
    // reference that drops this SlotDerived to zero, and the mutation queue itself
    // is destroyed with the GIL released (model_app.hpp:190) — same hazard as
    // SlotTree above. Release py_fn/dep_keepalive_ under the GIL (or leak them,
    // same idiom as PyHolder, if the interpreter is already gone) before the
    // member destructors below run; deps_/dep_owners_ need no such guard — they
    // hold no Python objects.
    ~SlotDerived() override {
        // Check GIL-held first, not Py_IsInitialized(): CPython flips
        // Py_IsInitialized() to false partway through Py_FinalizeEx(), well
        // before it finishes clearing module globals and running the final
        // GC passes — ordinary, still-GIL-holding object teardown (e.g. a
        // module-level Model dropping to a zero refcount at shutdown) can
        // run with Py_IsInitialized() already false. Decref'ing is safe
        // whenever this thread already holds the GIL, regardless of that
        // flag — only fall back to leaking when there's truly no GIL to
        // decref under and no interpreter left to acquire one on.
        if (PyGILState_Check()) {
            py_fn = nb::object();
            dep_keepalive_.clear();
            return;
        }
        if (!Py_IsInitialized()) {
            py_fn.release();
            for (auto& o : dep_keepalive_) o.release();
            return;
        }
        nb::gil_scoped_acquire g;
        py_fn = nb::object();
        dep_keepalive_.clear();
    }
};

template <typename T>
struct BoundDerived {
    std::shared_ptr<SlotBase> owner;
    SlotDerived<T>* derived = nullptr;
    T get() const {
        if (!derived) return T{};
        SlotDerived<T>* p = derived;
        // SlotDerived::get already does dispatch, but keep direct to avoid double dispatch
        return p->get();
    }
    Connection observe(nb::callable cb) {
        if (!derived) throw nb::value_error("observe(): handle is not bound to a Model");
        auto owner_copy = owner;
        auto* d = derived;
        auto conn = d->on_change().connect(WeakCallback(cb));
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};
// Standalone list — same shared_ptr-owned-state shape as FieldHandle above (see the
// SharedHandle rationale comment below): observe*()'s Connection keep_alive(list)
// keeps the hub alive, not the Python wrapper.
template <typename T>
struct ListHandle {
    std::shared_ptr<List<T>> list;
    ListHandle() : list(make_tracked_state<List<T>>()) {}
    ListHandle(const ListHandle&) = delete;
    ListHandle& operator=(const ListHandle&) = delete;
    ListHandle(ListHandle&&) = delete;
    ListHandle& operator=(ListHandle&&) = delete;
    void push(T v) {
        List<T>* p = list.get();
        list_op_dispatch(list, [p, v = std::move(v)]() mutable { p->push_back(std::move(v)); });
    }
    void erase(size_t i) {
        List<T>* p = list.get();
        list_op_dispatch(list, [p, i](){ if (i < p->size()) p->erase(i); });
    }
    void set(size_t i, T v) {
        List<T>* p = list.get();
        list_op_dispatch(list, [p, i, v = std::move(v)]() mutable { if (i < p->size()) p->set(i, std::move(v)); });
    }
    void replace_all(nb::list py) {
        std::vector<T> vec; vec.reserve(nb::len(py));
        for (auto h : py) vec.push_back(nb::cast<T>(h));
        List<T>* p = list.get();
        list_op_dispatch(list, [p, vec = std::move(vec)]() mutable { p->replace_all(vec); });
    }
    size_t size() const {
        List<T>* p = list.get();
        return dispatch_sync_read<size_t>([p](){ return p->size(); });
    }
    T get(size_t i) const {
        List<T>* p = list.get();
        return dispatch_sync_read<T>([p,i](){ return i < p->size() ? (*p)[i] : T{}; });
    }
    nb::list to_list() const {
        List<T>* p = list.get();
        // Reader may run on the logic thread post-finalization; build a plain
        // std::vector<T> there (no nb:: construction) and convert to nb::list
        // back on this (GIL-holding) calling thread.
        auto vec = dispatch_sync_read<std::vector<T>>([p](){
            std::vector<T> out; out.reserve(p->size());
            for (size_t i=0;i<p->size();++i) out.push_back((*p)[i]);
            return out;
        });
        nb::list out; for (auto& v : vec) out.append(v); return out;
    }
    Connection observe_insert(nb::callable cb) {
        auto conn = list->on_insert().connect(WeakCallback(cb));
        conn.keep_alive(list);
        return conn;
    }
    Connection observe_remove(nb::callable cb) {
        auto conn = list->on_remove().connect(WeakCallback(cb));
        conn.keep_alive(list);
        return conn;
    }
    Connection observe_update(nb::callable cb) {
        auto conn = list->on_update().connect(WeakCallback(cb));
        conn.keep_alive(list);
        return conn;
    }
};
template <typename T>
struct BoundList {
    std::shared_ptr<SlotBase> owner;
    List<T>* list = nullptr;
    void push(T v) { if(list) { auto* p=list; list_op_dispatch(owner, [p, v=std::move(v)]() mutable { p->push_back(std::move(v)); }); } }
    void erase(size_t i) { if(list) { auto* p=list; list_op_dispatch(owner, [p,i](){ if(i<p->size()) p->erase(i); }); } }
    void set(size_t i, T v) { if(list) { auto* p=list; list_op_dispatch(owner, [p,i,v=std::move(v)]() mutable { if(i<p->size()) p->set(i,std::move(v)); }); } }
    void replace_all(nb::list py) { if(!list) return; std::vector<T> vec; vec.reserve(nb::len(py)); for(auto h:py) vec.push_back(nb::cast<T>(h)); auto* p=list; list_op_dispatch(owner, [p, vec=std::move(vec)]() mutable { p->replace_all(vec); }); }
    size_t size() const {
        if (!list) return 0;
        List<T>* p = list;
        return dispatch_sync_read<size_t>([p](){ return p->size(); });
    }
    T get(size_t i) const {
        if (!list) return T{};
        List<T>* p = list;
        return dispatch_sync_read<T>([p,i](){ return i < p->size() ? (*p)[i] : T{}; });
    }
    nb::list to_list() const {
        if (!list) return nb::list();
        List<T>* p = list;
        auto vec = dispatch_sync_read<std::vector<T>>([p](){
            std::vector<T> out; out.reserve(p->size());
            for (size_t i=0;i<p->size();++i) out.push_back((*p)[i]);
            return out;
        });
        nb::list out; for (auto& v : vec) out.append(v); return out;
    }
    Connection observe_insert(nb::callable cb) {
        if(!list) throw nb::value_error("observe_insert(): handle is not bound to a Model"); auto owner_copy=owner; auto* p=list; auto c=p->on_insert().connect(WeakCallback(cb)); if(owner_copy) c.keep_alive(owner_copy); return c;
    }
    Connection observe_remove(nb::callable cb) {
        if(!list) throw nb::value_error("observe_remove(): handle is not bound to a Model"); auto owner_copy=owner; auto* p=list; auto c=p->on_remove().connect(WeakCallback(cb)); if(owner_copy) c.keep_alive(owner_copy); return c;
    }
    Connection observe_update(nb::callable cb) {
        if(!list) throw nb::value_error("observe_update(): handle is not bound to a Model"); auto owner_copy=owner; auto* p=list; auto c=p->on_update().connect(WeakCallback(cb)); if(owner_copy) c.keep_alive(owner_copy); return c;
    }
};

// BoundPlot — must be after list_op_dispatch
struct BoundPlot {
    std::shared_ptr<SlotBase> owner;
    prism::plot::PlotModel* plot = nullptr;
    void add_series(nb::list xs, nb::list ys, std::string color_str = "", float thickness = 2.f, bool fill = false) {
        if (!plot) return;
        std::vector<double> vx, vy;
        vx.reserve(nb::len(xs)); vy.reserve(nb::len(ys));
        for (auto h : xs) vx.push_back(nb::cast<double>(h));
        for (auto h : ys) vy.push_back(nb::cast<double>(h));
        auto* p = plot;
        auto fn = [p, vx = std::move(vx), vy = std::move(vy), color_str, thickness, fill]() mutable {
            prism::plot::XYData data{std::move(vx), std::move(vy)};
            prism::plot::SeriesStyle style;
            style.thickness = thickness;
            style.fill = fill;
            if (!color_str.empty() && color_str.size()==7 && color_str[0]=='#') {
                int r = std::stoi(color_str.substr(1,2), nullptr, 16);
                int g = std::stoi(color_str.substr(3,2), nullptr, 16);
                int b = std::stoi(color_str.substr(5,2), nullptr, 16);
                style.color = Color::rgba((uint8_t)r,(uint8_t)g,(uint8_t)b);
            }
            p->add_series(std::move(data), style);
        };
        list_op_dispatch(owner, std::move(fn));
    }
    void clear_series() {
        if (!plot) return;
        auto* p = plot;
        list_op_dispatch(owner, [p](){ p->clear_series(); });
    }
    void notify() {
        if (!plot) return;
        auto* p = plot;
        list_op_dispatch(owner, [p](){ p->notify(); });
    }
    // Single-post clear+add(+add...)+notify — see parse_replace_series_args for the two call forms.
    void replace_series(nb::object xs_or_series, nb::object ys = nb::none(), nb::object color = nb::none(),
                         float thickness = 2.f, bool fill = false) {
        if (!plot) return;
        reject_stray_kwargs_in_list_form(ys, color, fill);
        replace_series_dispatch(owner, plot, parse_replace_series_args(xs_or_series, ys, color), thickness, fill);
    }
    void set_labels(nb::object x = nb::none(), nb::object y = nb::none()) {
        if (!plot) return;
        auto* p = plot;
        bool has_x = !x.is_none(), has_y = !y.is_none();
        std::string xs = has_x ? nb::cast<std::string>(x) : std::string();
        std::string ys = has_y ? nb::cast<std::string>(y) : std::string();
        list_op_dispatch(owner, [p, has_x, has_y, xs, ys](){
            if (has_x) p->x_label.set(xs);
            if (has_y) p->y_label.set(ys);
        });
    }
    void set_x_label(std::string s) { if(plot) field_set_dispatch(owner, &plot->x_label, std::move(s)); }
    void set_y_label(std::string s) { if(plot) field_set_dispatch(owner, &plot->y_label, std::move(s)); }
    std::string get_x_label() const { if(!plot) return ""; return dispatch_sync_read<std::string>([p=plot](){ return p->x_label.get(); }); }
    std::string get_y_label() const { if(!plot) return ""; return dispatch_sync_read<std::string>([p=plot](){ return p->y_label.get(); }); }
    size_t series_count() const { if(!plot) return 0; return dispatch_sync_read<size_t>([p=plot](){ return p->series_count(); }); }
    size_t series_len(size_t i) const { if(!plot) return 0; return dispatch_sync_read<size_t>([p=plot,i](){ return p->series_len(i); }); }
    void reset_view() { if(plot){ auto* p=plot; list_op_dispatch(owner, [p](){ p->reset_view(); }); } }
};

// helper to attach a single dep to a derived slot
template <typename U> struct is_std_shared_ptr : std::false_type {};
template <typename U> struct is_std_shared_ptr<std::shared_ptr<U>> : std::true_type {};

template <typename FH>
auto* field_ptr_of(FH& h) {
    using M = std::decay_t<decltype(h.field)>;
    if constexpr (std::is_pointer_v<M>) return h.field;
    else if constexpr (is_std_shared_ptr<M>::value) return h.field.get();
    else return &h.field;
}

template <typename SH>
auto* shared_ptr_of(SH& h) {
    using M = std::decay_t<decltype(h.shared)>;
    if constexpr (std::is_pointer_v<M>) return h.shared;
    else if constexpr (is_std_shared_ptr<M>::value) return h.shared.get();
    else return &h.shared;
}
template <typename T>
void derived_attach_dep(std::shared_ptr<SlotDerived<T>> slot, nb::object dep) {
    std::weak_ptr<SlotDerived<T>> weak = slot;
    auto connect_field = [&](auto* example) -> bool {
        using FH = std::decay_t<decltype(*example)>;
        if (!nb::isinstance<FH>(dep)) return false;
        auto& h = nb::cast<FH&>(dep);
        auto* ptr = field_ptr_of(h);
        if (!ptr) return true;
        slot->deps_.push_back(ptr->on_change().connect([weak](const auto&){ if (auto s = weak.lock()) s->recompute(); }));
        if constexpr (requires { h.owner; }) { if (h.owner) slot->dep_owners_.push_back(h.owner); }
        slot->dep_keepalive_.push_back(dep);
        return true;
    };
    auto connect_shared = [&](auto* example) -> bool {
        using SH = std::decay_t<decltype(*example)>;
        if (!nb::isinstance<SH>(dep)) return false;
        auto& h = nb::cast<SH&>(dep);
        auto* ptr = shared_ptr_of(h);
        if (!ptr) return true;
        slot->deps_.push_back(ptr->on_change().connect([weak](const auto&){ if (auto s = weak.lock()) s->recompute(); }));
        if constexpr (requires { h.owner; }) { if (h.owner) slot->dep_owners_.push_back(h.owner); }
        slot->dep_keepalive_.push_back(dep);
        return true;
    };
    auto connect_derived = [&](auto* example) -> bool {
        using DH = std::decay_t<decltype(*example)>;
        if (!nb::isinstance<DH>(dep)) return false;
        auto& h = nb::cast<DH&>(dep);
        auto* ptr = h.derived;
        if (!ptr) return true;
        slot->deps_.push_back(ptr->on_change().connect([weak](const auto&){ if (auto s = weak.lock()) s->recompute(); }));
        if (h.owner) slot->dep_owners_.push_back(h.owner);
        slot->dep_keepalive_.push_back(dep);
        return true;
    };
    // BoundSliderValue holds field_h XOR field_v (see its own comment) instead
    // of one `h.field` member field_ptr_of() can read generically, so it gets
    // its own connect — otherwise identical to connect_field: only h.owner and
    // whichever field's on_change() are needed to trigger recompute() on
    // change, never the field's value type (the derived compute fn reads the
    // dep's actual value itself, e.g. self.v.value).
    auto connect_slider = [&]() -> bool {
        if (!nb::isinstance<BoundSliderValue>(dep)) return false;
        auto& h = nb::cast<BoundSliderValue&>(dep);
        auto connect_one = [&](auto* ptr) {
            if (!ptr) return;
            slot->deps_.push_back(ptr->on_change().connect([weak](const auto&){ if (auto s = weak.lock()) s->recompute(); }));
        };
        connect_one(h.field_h);
        connect_one(h.field_v);
        if (h.owner) slot->dep_owners_.push_back(h.owner);
        slot->dep_keepalive_.push_back(dep);
        return true;
    };
    // Bound handles
    if (connect_field((BoundField<int>*)nullptr)) return;
    if (connect_field((BoundField<double>*)nullptr)) return;
    if (connect_field((BoundField<std::string>*)nullptr)) return;
    if (connect_field((BoundField<bool>*)nullptr)) return;
    if (connect_slider()) return;
    if (connect_field((BoundCheckboxValue*)nullptr)) return;
    if (connect_shared((BoundShared<int>*)nullptr)) return;
    if (connect_shared((BoundShared<double>*)nullptr)) return;
    if (connect_shared((BoundShared<std::string>*)nullptr)) return;
    if (connect_shared((BoundShared<bool>*)nullptr)) return;
    if (connect_derived((BoundDerived<int>*)nullptr)) return;
    if (connect_derived((BoundDerived<double>*)nullptr)) return;
    if (connect_derived((BoundDerived<std::string>*)nullptr)) return;
    if (connect_derived((BoundDerived<bool>*)nullptr)) return;
    // Standalone handles
    if (connect_field((FieldHandle<int>*)nullptr)) return;
    if (connect_field((FieldHandle<double>*)nullptr)) return;
    if (connect_field((FieldHandle<std::string>*)nullptr)) return;
    if (connect_field((FieldHandle<bool>*)nullptr)) return;
    if (connect_shared((SharedHandle<int>*)nullptr)) return;
    if (connect_shared((SharedHandle<double>*)nullptr)) return;
    if (connect_shared((SharedHandle<std::string>*)nullptr)) return;
    if (connect_shared((SharedHandle<bool>*)nullptr)) return;
    throw std::runtime_error("derived: unsupported dependency handle type");
}

inline void py_widget_dispatch(ViewBuilder& vb, nb::object h) {
    if (nb::isinstance<BoundField<int>>(h)) vb.widget(*nb::cast<BoundField<int>&>(h).field);
    else if (nb::isinstance<BoundField<double>>(h)) vb.widget(*nb::cast<BoundField<double>&>(h).field);
    else if (nb::isinstance<BoundField<std::string>>(h)) vb.widget(*nb::cast<BoundField<std::string>&>(h).field);
    else if (nb::isinstance<BoundField<bool>>(h)) vb.widget(*nb::cast<BoundField<bool>&>(h).field);
    else if (nb::isinstance<BoundShared<int>>(h)) vb.widget(*nb::cast<BoundShared<int>&>(h).shared);
    else if (nb::isinstance<BoundShared<double>>(h)) vb.widget(*nb::cast<BoundShared<double>&>(h).shared);
    else if (nb::isinstance<BoundShared<std::string>>(h)) vb.widget(*nb::cast<BoundShared<std::string>&>(h).shared);
    else if (nb::isinstance<BoundShared<bool>>(h)) vb.widget(*nb::cast<BoundShared<bool>&>(h).shared);
    else if (nb::isinstance<BoundSliderValue>(h)) {
        auto& b = nb::cast<BoundSliderValue&>(h);
        if (b.field_h) vb.widget(*b.field_h);
        else if (b.field_v) vb.widget(*b.field_v);
    }
    else if (nb::isinstance<BoundCheckboxValue>(h)) vb.widget(*nb::cast<BoundCheckboxValue&>(h).field);
    else if (nb::isinstance<BoundDerived<int>>(h)) vb.widget_generic<int>(*nb::cast<BoundDerived<int>&>(h).derived);
    else if (nb::isinstance<BoundDerived<double>>(h)) vb.widget_generic<double>(*nb::cast<BoundDerived<double>&>(h).derived);
    else if (nb::isinstance<BoundDerived<std::string>>(h)) vb.widget_generic<std::string>(*nb::cast<BoundDerived<std::string>&>(h).derived);
    else if (nb::isinstance<BoundDerived<bool>>(h)) vb.widget_generic<bool>(*nb::cast<BoundDerived<bool>&>(h).derived);
    else if (nb::isinstance<BoundList<int>>(h)) vb.list(*nb::cast<BoundList<int>&>(h).list);
    else if (nb::isinstance<BoundList<double>>(h)) vb.list(*nb::cast<BoundList<double>&>(h).list);
    else if (nb::isinstance<BoundList<std::string>>(h)) vb.list(*nb::cast<BoundList<std::string>&>(h).list);
     else throw std::runtime_error("ViewBuilder.widget: unsupported handle type");
}
inline void py_list_dispatch(ViewBuilder& vb, nb::object h) {
    if (nb::isinstance<BoundList<int>>(h)) vb.list(*nb::cast<BoundList<int>&>(h).list);
    else if (nb::isinstance<BoundList<double>>(h)) vb.list(*nb::cast<BoundList<double>&>(h).list);
    else if (nb::isinstance<BoundList<std::string>>(h)) vb.list(*nb::cast<BoundList<std::string>&>(h).list);
    else throw std::runtime_error("ViewBuilder.list: unsupported handle type");
}
inline void py_canvas_dispatch(ViewBuilder& vb, nb::object h) {
    if (nb::isinstance<BoundPlot>(h)) {
        auto& b = nb::cast<BoundPlot&>(h);
        vb.canvas(*b.plot).depends_on(b.plot->x_range).depends_on(b.plot->y_range).depends_on(b.plot->view).depends_on(b.plot->cursor).depends_on(b.plot->revision);
    } else throw std::runtime_error("ViewBuilder.canvas: unsupported handle type (expected BoundPlot)");
}
inline void py_tree_dispatch(ViewBuilder& vb, nb::object h) {
    if (nb::isinstance<BoundTree>(h)) {
        auto& b = nb::cast<BoundTree&>(h);
        if (b.ctrl) vb.tree(*b.ctrl);
    } else throw std::runtime_error("ViewBuilder.tree: unsupported handle type (expected BoundTree)");
}

// add_slider_slot()'s return type: exactly one of field_h/field_v is set,
// matching BoundSliderValue's own field_h/field_v split (see its comment).
struct SliderAlloc {
    std::shared_ptr<SlotBase> owner;
    Field<Slider<double, Orientation::Horizontal>>* field_h = nullptr;
    Field<Slider<double, Orientation::Vertical>>* field_v = nullptr;
};

struct PyModel {
    std::vector<std::shared_ptr<SlotBase>> slots;
    std::mutex slots_mutex;

    std::pair<std::shared_ptr<SlotBase>, Field<int>*> add_int_slot(int v) {
        auto s = std::make_shared<Slot<int>>(v);
        auto* p = &s->field;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Field<double>*> add_float_slot(double v) {
        auto s = std::make_shared<Slot<double>>(v);
        auto* p = &s->field;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Field<std::string>*> add_str_slot(std::string v) {
        auto s = std::make_shared<Slot<std::string>>(std::move(v));
        auto* p = &s->field;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Field<bool>*> add_bool_slot(bool v) {
        auto s = std::make_shared<Slot<bool>>(v);
        auto* p = &s->field;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    SliderAlloc add_slider_slot(double v, double mn, double mx, bool vertical) {
        std::lock_guard<std::mutex> lk(slots_mutex);
        if (vertical) {
            auto s = std::make_shared<SlotSliderV>(Slider<double, Orientation::Vertical>{v, mn, mx, 0.0});
            auto* p = &s->field;
            slots.push_back(s);
            return {s, nullptr, p};
        }
        auto s = std::make_shared<SlotSlider>(Slider<double>{v, mn, mx, 0.0});
        auto* p = &s->field;
        slots.push_back(s);
        return {s, p, nullptr};
    }
    std::pair<std::shared_ptr<SlotBase>, Field<Checkbox>*> add_checkbox_slot(bool v, std::string label) {
        auto s = std::make_shared<SlotCheckbox>(Checkbox{v, std::move(label)});
        auto* p = &s->field;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Shared<int>*> add_shared_int_slot(int v) {
        auto s = std::make_shared<SlotShared<int>>(v);
        auto* p = &s->shared;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Shared<double>*> add_shared_float_slot(double v) {
        auto s = std::make_shared<SlotShared<double>>(v);
        auto* p = &s->shared;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Shared<std::string>*> add_shared_str_slot(std::string v) {
        auto s = std::make_shared<SlotShared<std::string>>(std::move(v));
        auto* p = &s->shared;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Shared<bool>*> add_shared_bool_slot(bool v) {
        auto s = std::make_shared<SlotShared<bool>>(v);
        auto* p = &s->shared;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Channel<int>*> add_channel_int_slot() {
        auto s = std::make_shared<SlotChannel<int>>();
        auto* p = &s->channel;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Channel<double>*> add_channel_float_slot() {
        auto s = std::make_shared<SlotChannel<double>>();
        auto* p = &s->channel;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Channel<std::string>*> add_channel_str_slot() {
        auto s = std::make_shared<SlotChannel<std::string>>();
        auto* p = &s->channel;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, Channel<bool>*> add_channel_bool_slot() {
        auto s = std::make_shared<SlotChannel<bool>>();
        auto* p = &s->channel;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, List<int>*> add_list_int_slot(std::vector<int> v) {
        auto s = std::make_shared<SlotList<int>>(std::move(v));
        auto* p = &s->list;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, List<double>*> add_list_float_slot(std::vector<double> v) {
        auto s = std::make_shared<SlotList<double>>(std::move(v));
        auto* p = &s->list;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, List<std::string>*> add_list_str_slot(std::vector<std::string> v) {
        auto s = std::make_shared<SlotList<std::string>>(std::move(v));
        auto* p = &s->list;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    // List<bool> disabled due to vector<bool> proxy issues — Python bool lists map to List<int> via __init__.py

    std::pair<std::shared_ptr<SlotBase>, prism::plot::PlotModel*> add_plot_slot() {
        auto s = std::make_shared<SlotPlot>();
        auto* p = &s->plot;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }
    std::pair<std::shared_ptr<SlotBase>, prism::ui::TreeController*> add_tree_slot(nb::object py_obj) {
        auto s = std::make_shared<SlotTree>(py_obj);
        auto* p = &s->ctrl;
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, p};
    }

    // Derived slots — vector-based deps (avoid immutable tuple assignment)
    template <typename T>
    std::pair<std::shared_ptr<SlotBase>, SlotDerived<T>*> add_derived_slot_vec(nb::object fn, const std::vector<nb::object>& deps, const std::string& name = "<derived>") {
        T init{};
        {
            nb::gil_scoped_acquire g;
            // fn() runs synchronously under the GIL inside this direct Python call
            // (Model() construction) — let a genuine exception from the user's
            // function (nb::python_error, e.g. ZeroDivisionError) propagate
            // unchanged, so Model() raises it as-is instead of silently defaulting
            // to T{}. Only a type mismatch on the *result* is ours to translate.
            nb::object result = fn();
            try {
                init = nb::cast<T>(result);
            } catch (const std::bad_cast&) {
                std::string msg = "derived '" + name + "': function returned " +
                                   nb::cast<std::string>(nb::repr(result)) + ", expected " + py_type_name<T>();
                throw nb::type_error(msg.c_str());
            }
        }
        auto s = std::make_shared<SlotDerived<T>>(std::move(fn), std::move(init));
        for (auto& dep : deps) derived_attach_dep(s, dep);
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, s.get()};
    }
    // Python view callback — set from Model.__init__ if subclass overrides view().
    nb::object py_view_cb = nb::none();
    void set_view_callback(nb::object cb) { py_view_cb = std::move(cb); }

    void view(ViewBuilder& vb) {
        // Check callback with GIL held — nb::object copy/incref requires GIL.
        {
            nb::gil_scoped_acquire gil;
            if (!py_view_cb.is_none()) {
                nb::object cb = py_view_cb;
                nb::object vb_obj = nb::cast(&vb, nb::rv_policy::reference);
                try {
                    cb(vb_obj);
                } catch (...) {
                    report_python_callback_error();
                }
                return;
            }
        }
        std::lock_guard<std::mutex> lk(slots_mutex);
        for (auto& s : slots) s->build(vb);
    }

    void drain() {
        // Snapshot under the lock, then drain the copy lock-free: s->drain() runs
        // Python observer callbacks (Shared/Channel), and any allocation inside one
        // can trigger CPython's automatic GC on this same thread — which re-enters
        // pymodel_tp_traverse/pymodel_tp_clear, both of which also take
        // slots_mutex. Holding the lock across drain() would self-deadlock (a
        // plain std::mutex isn't recursive) the moment that happens.
        std::vector<std::shared_ptr<SlotBase>> slots_copy;
        {
            std::lock_guard<std::mutex> lk(slots_mutex);
            slots_copy = slots;
        }
        for (auto& s : slots_copy) s->drain();
        drain_standalone();
    }
};

// tp_traverse/tp_clear — makes PyModel (and every Python subclass, including
// prism.Model) participate in the cyclic GC. Without this, py_view_cb and
// each slot's Python callables (SlotDerived::py_fn/dep_keepalive_,
// SlotTree::py_src_holder) are strong references held entirely on the C++
// side: a cycle through them (Model -> slot -> callback -> Model) is
// invisible to and unbreakable by the GC. See nanobind's refleaks.rst
// "Reference cycles" section for the pattern.
static int pymodel_tp_traverse(PyObject* self, visitproc visit, void* arg) {
    Py_VISIT(Py_TYPE(self));
    if (!nb::inst_ready(self)) return 0; // constructor may not have run yet
    PyModel* m = nb::inst_ptr<PyModel>(self);
    if (PyObject* o = m->py_view_cb.ptr()) {
        int vret = visit(o, arg);
        if (vret) return vret;
    }
    std::lock_guard<std::mutex> lk(m->slots_mutex);
    for (auto& s : m->slots) s->traverse(visit, arg);
    return 0;
}

static int pymodel_tp_clear(PyObject* self) {
    if (!nb::inst_ready(self)) return 0; // constructor may not have run yet — see tp_traverse
    PyModel* m = nb::inst_ptr<PyModel>(self);
    m->py_view_cb = nb::none();
    std::vector<std::shared_ptr<SlotBase>> slots_copy;
    {
        std::lock_guard<std::mutex> lk(m->slots_mutex);
        slots_copy = m->slots;
    }
    for (auto& s : slots_copy) s->clear();
    return 0;
}

static PyType_Slot pymodel_type_slots[] = {
    {Py_tp_traverse, (void*)pymodel_tp_traverse},
    {Py_tp_clear, (void*)pymodel_tp_clear},
    {0, nullptr},
};

// simplify: leaked by design. A weakref.WeakSet tracking every observed handle needs one
// long-lived Python object shared across all observe() calls. A `static nb::object` local
// would run its Py_DECREF destructor during C++ static teardown, which happens after
// Py_Finalize — decref'ing into an already-torn-down interpreter is UB. A raw PyObject*
// that is created once and never released sidesteps that: nothing ever runs its destructor.
// python/prism/__init__.py's _atexit_clear() empties this set (while the interpreter is
// still fully alive) before shutdown, so the only thing actually leaked is one now-empty
// WeakSet container. Upgrade path: a Py_AtExit callback if a hard release guarantee is
// ever needed instead of relying on atexit ordering.
static PyObject* g_observed_handles = nullptr;

static nb::handle observed_handles() {
    if (!g_observed_handles) {
        nb::object ws = nb::module_::import_("weakref").attr("WeakSet")();
        g_observed_handles = ws.release().ptr();
    }
    return nb::handle(g_observed_handles);
}

// Appends the Connection returned by an observe*() call to self.__dict__["_prism_keepalive"]
// (creating the list on first use) so a fire-and-forget `handle.observe(cb)` keeps firing —
// nothing else holds a reference to the Connection once the caller drops it. Also registers
// self in the module-level _observed_handles WeakSet so python/prism/__init__.py's atexit
// cleanup can find and disconnect any handle still referenced (e.g. a long-lived module
// global) before interpreter shutdown. Standalone observe*() no longer takes
// nb::keep_alive<0,1> on `self` (Task 14 fix — see FieldHandle/SharedHandle above): the
// Connection's keep_alive(state) keeps the hub alive instead, so handle -> keepalive
// list -> Connection is a plain acyclic chain that dies with `self`, not a GC-invisible
// self-cycle. Wraps the Connection into a Python object exactly once and returns that
// same object.
//
// `cb` is also appended to self.__dict__["_prism_callbacks"] — this is now the callback's
// ONLY strong reference (see WeakCallback above: every observe*() wrapper holds just a weak
// reference to `cb`, resolved under the GIL at call time). A callback that captures the
// owning Model (`m.x.observe(lambda v: m.rebuild())`) closes a Model -> ... -> handle ->
// callback -> Model cycle; making the handle's own dynamic_attr dict the sole strong owner
// of `cb` — which nanobind's inst_traverse walks — puts every strong reference in that cycle
// on a Python-visible edge, so the cyclic GC can actually detect and collect it (a real
// std::function-held nb::callable copy from a plain C++ member, as it was before, would be
// an extra reference the GC can never see — subtract_refs()'s refcount bookkeeping treats
// that as "referenced from outside," which keeps the whole cycle alive forever regardless of
// what else also references the same callback; mirroring `cb` elsewhere without first making
// the receivers_ side weak would not have fixed anything).
static nb::object keep_connection(nb::object self, Connection conn, nb::callable cb) {
    nb::object result = nb::cast(std::move(conn));
    nb::dict d = self.attr("__dict__");
    // setdefault, not contains()-then-[]= : the two-step check-then-set race
    // (two threads both seeing "absent" and each installing their own fresh
    // list, one clobbering the other's) is real on a free-threaded build —
    // setdefault's own C level get-or-insert is atomic under the dict's lock.
    nb::list keepalive = nb::cast<nb::list>(d.attr("setdefault")("_prism_keepalive", nb::list()));
    keepalive.append(result);
    nb::list callbacks = nb::cast<nb::list>(d.attr("setdefault")("_prism_callbacks", nb::list()));
    callbacks.append(cb);
    observed_handles().attr("add")(self);
    return result;
}

// Runs a user validator installed by field()/shared()'s Python descriptor
// (as `handle.__dict__["_prism_validator"]`, see _FieldDescriptor._allocate
// / _SharedDescriptor._allocate in python/prism/__init__.py) before a write
// reaches the handle's underlying Field/Shared. GIL is already held here
// (we're inside a Python call), so the validator runs synchronously on the
// calling thread, before any off-thread dispatch. A rejecting validator's
// exception (nb::python_error) propagates unchanged.
template <typename T>
T apply_validator(nb::object self, T v) {
    nb::dict d = self.attr("__dict__");
    if (!d.contains("_prism_validator")) return v;
    nb::object validator = d["_prism_validator"];
    nb::object result = validator(v);
    try {
        return nb::cast<T>(result);
    } catch (const std::bad_cast&) {
        std::string name = d.contains("_prism_name") ? nb::cast<std::string>(nb::str(d["_prism_name"])) : "<field>";
        std::string msg = "validator for '" + name + "' must return a " + py_type_name<T>() +
                           " (or raise); got " + nb::cast<std::string>(nb::repr(result));
        throw nb::type_error(msg.c_str());
    }
}

// Shared body for a handle's `.value` setter and `.set()` method — same
// validate-then-write on both, so `m.x = v`, `m.x.value = v` and
// `m.x.set(v)` behave identically (the Python descriptor's __set__ just
// delegates to `.value =`, giving a single validation path).
template <typename Handle, typename T>
void validated_set(nb::object self, T v) {
    v = apply_validator<T>(self, std::move(v));
    nb::cast<Handle&>(self).set(std::move(v));
}

// One templated registration per scalar handle family (Field/Bound/Shared/BoundShared/
// Channel/BoundChannel/BoundDerived), instantiated once per T from NB_MODULE below.
// Naming is not uniformly "<Family>" + suffix: BoundField<T> is historically named
// "Bound" + suffix (not "BoundField" + suffix) — preserved here since it is Python-visible API.
template <typename T>
void bind_scalar(nb::module_& m, const char* suffix) {
    std::string field_name = std::string("Field") + suffix;
    auto field_cls = nb::class_<FieldHandle<T>>(m, field_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<T>(), nb::arg("value") = T{})
        .def_prop_rw("value", &FieldHandle<T>::get, &validated_set<FieldHandle<T>, T>)
        .def("get", &FieldHandle<T>::get)
        .def("set", &validated_set<FieldHandle<T>, T>);
    field_cls.def("observe", [](nb::object self, nb::callable cb) {
        return keep_connection(self, nb::cast<FieldHandle<T>&>(self).observe(cb), cb);
    }, nb::arg("callback"));
    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
        field_cls.def("add", [](nb::object self, T n) {
            auto& h = nb::cast<FieldHandle<T>&>(self);
            field_add_dispatch<T>(self, h.field, h.field.get(), n);
        }, nb::arg("n"), "Any thread; atomic increment applied on the logic thread.");
    }

    std::string bound_field_name = std::string("Bound") + suffix;
    auto bound_field_cls = nb::class_<BoundField<T>>(m, bound_field_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundField<T>::get, &validated_set<BoundField<T>, T>)
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundField<T>&>(self).observe(cb), cb);
        }, nb::arg("callback"))
        .def("get", &BoundField<T>::get)
        .def("set", &validated_set<BoundField<T>, T>);
    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
        bound_field_cls.def("add", [](nb::object self, T n) {
            auto& h = nb::cast<BoundField<T>&>(self);
            if (h.field) field_add_dispatch<T>(self, h.owner, h.field, n);
        }, nb::arg("n"), "Any thread; atomic increment applied on the logic thread.");
    }

    std::string shared_name = std::string("Shared") + suffix;
    auto shared_cls = nb::class_<SharedHandle<T>>(m, shared_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<T>(), nb::arg("value") = T{})
        .def_prop_rw("value", &SharedHandle<T>::get, &validated_set<SharedHandle<T>, T>)
        .def("get", &SharedHandle<T>::get).def("set", &validated_set<SharedHandle<T>, T>);
    shared_cls.def("observe", [](nb::object self, nb::callable cb) {
        return keep_connection(self, nb::cast<SharedHandle<T>&>(self).observe(cb), cb);
    }, nb::arg("callback"));

    std::string bound_shared_name = std::string("BoundShared") + suffix;
    nb::class_<BoundShared<T>>(m, bound_shared_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundShared<T>::get, &validated_set<BoundShared<T>, T>)
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundShared<T>&>(self).observe(cb), cb);
        }, nb::arg("callback"))
        .def("get", &BoundShared<T>::get).def("set", &validated_set<BoundShared<T>, T>);

    std::string channel_name = std::string("Channel") + suffix;
    nb::class_<ChannelHandle<T>>(m, channel_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<>()).def("send", &ChannelHandle<T>::send)
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<ChannelHandle<T>&>(self).observe(cb), cb);
        }, nb::arg("callback"));

    std::string bound_channel_name = std::string("BoundChannel") + suffix;
    nb::class_<BoundChannel<T>>(m, bound_channel_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("send", &BoundChannel<T>::send)
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundChannel<T>&>(self).observe(cb), cb);
        }, nb::arg("callback"));

    std::string bound_derived_name = std::string("BoundDerived") + suffix;
    nb::class_<BoundDerived<T>>(m, bound_derived_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_ro("value", &BoundDerived<T>::get).def("get", &BoundDerived<T>::get)
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundDerived<T>&>(self).observe(cb), cb);
        }, nb::arg("callback"));
}

// List<bool>/BoundList<bool> are deliberately never instantiated — vector<bool> proxy is
// incompatible with the const T& Signal used by List<T>; use an int list for bool data.
template <typename T>
void bind_list(nb::module_& m, const char* suffix) {
    std::string list_name = std::string("List") + suffix;
    nb::class_<ListHandle<T>>(m, list_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<>()).def("push", &ListHandle<T>::push).def("erase", &ListHandle<T>::erase)
        .def("set", &ListHandle<T>::set).def("replace_all", &ListHandle<T>::replace_all)
        .def("size", &ListHandle<T>::size).def("get", &ListHandle<T>::get).def("to_list", &ListHandle<T>::to_list)
        .def("observe_insert", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<ListHandle<T>&>(self).observe_insert(cb), cb);
        }, nb::arg("callback"))
        .def("observe_remove", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<ListHandle<T>&>(self).observe_remove(cb), cb);
        }, nb::arg("callback"))
        .def("observe_update", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<ListHandle<T>&>(self).observe_update(cb), cb);
        }, nb::arg("callback"));

    std::string bound_list_name = std::string("BoundList") + suffix;
    nb::class_<BoundList<T>>(m, bound_list_name.c_str(), nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("push", &BoundList<T>::push).def("erase", &BoundList<T>::erase).def("set", &BoundList<T>::set).def("replace_all", &BoundList<T>::replace_all)
        .def("size", &BoundList<T>::size).def("get", &BoundList<T>::get).def("to_list", &BoundList<T>::to_list)
        .def("observe_insert", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundList<T>&>(self).observe_insert(cb), cb);
        }, nb::arg("callback"))
        .def("observe_remove", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundList<T>&>(self).observe_remove(cb), cb);
        }, nb::arg("callback"))
        .def("observe_update", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundList<T>&>(self).observe_update(cb), cb);
        }, nb::arg("callback"));
}

// Module-wide Python error hub backing prism.on_error(). Mirrors g_observed_handles above:
// a raw PyObject* the module owns, incref/decref'd explicitly under the GIL rather than a
// `static nb::object` whose destructor would run (and decref into a torn-down interpreter)
// during C++ static teardown after Py_Finalize. on_error() always decrefs the handler it
// replaces, so nothing accumulates here.
static std::mutex g_error_handler_mutex;
static PyObject* g_error_handler = nullptr;

// Same stderr fallback error_hub.hpp's default_error_handler prints for a plain C++
// exception_ptr, plus the Python-specific case: a caught nb::python_error gets Python's
// own traceback printer instead of ex.what().
static void print_default_error(std::exception_ptr e) {
    try {
        std::rethrow_exception(e);
    } catch (nb::python_error& pe) {
        // restore()/PyErr_Print() need a live interpreter to print into; with none up
        // (e.g. this is running during/after Py_Finalize) fall back to a plain line.
        if (Py_IsInitialized()) {
            pe.restore();
            PyErr_Print();
        } else {
            std::cerr << "[prism] unhandled exception in posted callback: " << pe.what() << '\n';
        }
    } catch (const std::exception& ex) {
        std::cerr << "[prism] unhandled exception in posted callback: " << ex.what() << '\n';
    } catch (...) {
        std::cerr << "[prism] unhandled exception in posted callback: <non-std exception>\n";
    }
}

// Builds the object passed to a Python on_error handler: nb::python_error already carries
// the original Python exception instance; anything else becomes a RuntimeError.
static nb::object exception_to_python(std::exception_ptr e) {
    try {
        std::rethrow_exception(e);
    } catch (nb::python_error& pe) {
        return nb::borrow(pe.value());
    } catch (const std::exception& ex) {
        return nb::module_::import_("builtins").attr("RuntimeError")(ex.what());
    } catch (...) {
        return nb::module_::import_("builtins").attr("RuntimeError")("<non-std exception>");
    }
}

// Installed once at module init as prism::core's process-wide unhandled-error hook (Task 1
// — include/prism/core/error_hub.hpp). May run on any thread, with or without the GIL
// already held by the caller, so it acquires the GIL itself and only if the interpreter is
// up at all. If this handler throws, report_unhandled_error() (error_hub.hpp) is already
// guarded to fall back to its own stderr default — never call it re-entrantly here.
static void python_error_hub(std::exception_ptr e) {
    if (!Py_IsInitialized()) {
        print_default_error(e);
        return;
    }
    nb::gil_scoped_acquire gil;
    PyObject* handler_raw;
    {
        std::lock_guard<std::mutex> lk(g_error_handler_mutex);
        handler_raw = g_error_handler;
    }
    if (!handler_raw) {
        print_default_error(e);
        return;
    }
    nb::object handler = nb::borrow(nb::handle(handler_raw));
    try {
        handler(exception_to_python(e));
    } catch (nb::python_error& he) {
        // The handler itself raised — print its exception (not the original) so the
        // failure is visible, then still report the original via the default path.
        he.restore();
        PyErr_Print();
        print_default_error(e);
    } catch (...) {
        print_default_error(e);
    }
}

// Every Python callback wrapper below routes a caught exception here instead of printing
// or silently swallowing it — see task-11-brief.md. Only valid inside a catch block
// (relies on std::current_exception()).
static void report_python_callback_error() {
    prism::core::report_unhandled_error(std::current_exception());
}

// Test-only debug hook: how many strong references does a standalone handle's
// Shared<T>/Channel<T> state have right now? Exposed as
// _standalone_shared_use_count() so a test can directly prove observe() no longer
// leaves the hub holding a reference to itself (a use_count that grows by one per
// observe() call, never reclaimed short of disconnect(), would be that leak) —
// more precise than watching the alive/dead count, which a full disconnect()
// cleans up either way and so can't distinguish the redundant reference from a
// correctly-single-owned one.
static int64_t standalone_state_use_count(nb::object h) {
    if (nb::isinstance<SharedHandle<int>>(h)) return nb::cast<SharedHandle<int>&>(h).shared.use_count();
    if (nb::isinstance<SharedHandle<double>>(h)) return nb::cast<SharedHandle<double>&>(h).shared.use_count();
    if (nb::isinstance<SharedHandle<std::string>>(h)) return nb::cast<SharedHandle<std::string>&>(h).shared.use_count();
    if (nb::isinstance<SharedHandle<bool>>(h)) return nb::cast<SharedHandle<bool>&>(h).shared.use_count();
    if (nb::isinstance<ChannelHandle<int>>(h)) return nb::cast<ChannelHandle<int>&>(h).channel.use_count();
    if (nb::isinstance<ChannelHandle<double>>(h)) return nb::cast<ChannelHandle<double>&>(h).channel.use_count();
    if (nb::isinstance<ChannelHandle<std::string>>(h)) return nb::cast<ChannelHandle<std::string>&>(h).channel.use_count();
    if (nb::isinstance<ChannelHandle<bool>>(h)) return nb::cast<ChannelHandle<bool>&>(h).channel.use_count();
    throw std::runtime_error("_standalone_shared_use_count: unsupported handle type");
}

// Headless backend for pytest and prism.headless() — stays alive until either
// delay_ms elapses or _request_quit() is called from any thread. Single-instance
// (only one _run_headless can be active at a time, enforced by g_run_guard), so a
// plain global condition variable + flag is enough — no per-instance state needed.
static std::mutex g_headless_cv_mutex;
static std::condition_variable g_headless_cv;
static std::atomic<bool> g_headless_quit_requested{false};

// Runs the same drain a mutation-queue post/tick already does (drain_queue_loop,
// whose tail is AppContext's shared drain_publish — WidgetTree::drain_shared(),
// which calls model.drain(), then publish_dirty/schedule_tick) on the logic
// thread, and blocks the caller until it completes. Called right before
// WindowClose fires so a post/Shared-write made just before quit() is not
// dropped (WindowClose sets the mutation queue's closed flag, after which any
// later post is a silent no-op).
static void final_drain_before_close() {
    std::optional<AppContext::PostHandle> hopt;
    {
        std::lock_guard<std::mutex> lk(g_handle_mutex);
        if (!g_has_handle.load(std::memory_order_acquire) || !g_post_handle) return;
        hopt = *g_post_handle;
    }
    auto q = hopt->queue.lock();
    auto sf = hopt->scheduled.lock();
    if (!q || !sf) return;
    auto tp = hopt->drain_publish.lock();
    auto prom = std::make_shared<std::promise<void>>();
    auto fut = prom->get_future();
    exec::start_detached(stdexec::schedule(hopt->sched) | stdexec::then([q, sf, tp, prom] {
        if (Py_IsInitialized()) {
            nb::gil_scoped_acquire gil;
            drain_queue_loop(q, sf, tp);
        }
        prom->set_value();
    }));
    // This runs on the thread that called _run_headless (released the GIL around
    // model_app()), same guard as the spins/waits above — only re-release if we
    // actually hold it.
    std::optional<nb::gil_scoped_release> rel;
    if (Py_IsInitialized() && PyGILState_Check()) rel.emplace();
    fut.wait();
}

struct DelayHeadlessBackend final : BackendBase {
    HeadlessWindow window_{0, {}};
    int delay_ms_ = 100;
    explicit DelayHeadlessBackend(int d) : delay_ms_(d) {}
    Window& create_window(WindowConfig cfg) override { window_ = HeadlessWindow{1, cfg}; return window_; }
    void run(std::function<void(const WindowEvent&)> cb) override {
        {
            std::unique_lock<std::mutex> lk(g_headless_cv_mutex);
            g_headless_cv.wait_for(lk, std::chrono::milliseconds(delay_ms_),
                                    [] { return g_headless_quit_requested.load(std::memory_order_acquire); });
        }
        final_drain_before_close();
        cb(WindowEvent{window_.id(), WindowClose{}});
    }
    void submit(WindowId, std::shared_ptr<const SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {}
};

NB_MODULE(_prism_ext, m) {
    prism::core::set_unhandled_error_handler(python_error_hub);
    m.def("_set_error_handler", [](nb::object handler) {
        PyObject* new_raw = nullptr;
        if (!handler.is_none()) {
            handler.inc_ref();
            new_raw = handler.ptr();
        }
        PyObject* old_raw;
        {
            std::lock_guard<std::mutex> lk(g_error_handler_mutex);
            old_raw = g_error_handler;
            g_error_handler = new_raw;
        }
        if (old_raw) nb::handle(old_raw).dec_ref();
    }, nb::arg("handler").none());
    m.def("is_logic_thread", [](){ return detail_is_logic_thread; });
    m.attr("_observed_handles") = observed_handles();
    m.def("_standalone_state_alive_count", [](){ return g_standalone_state_alive.load(std::memory_order_relaxed); });
    m.def("_standalone_shared_use_count", &standalone_state_use_count, nb::arg("handle"));

    nb::class_<Connection>(m, "Connection", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("disconnect", &Connection::disconnect)
        .def("__enter__", [](Connection& self){ return &self; })
        .def("__exit__", [](Connection& self, nb::object, nb::object, nb::object){ self.disconnect(); return false; });

    bind_scalar<int>(m, "Int");
    bind_scalar<double>(m, "Float");
    bind_scalar<std::string>(m, "Str");
    bind_scalar<bool>(m, "Bool");

    nb::class_<BoundSliderValue>(m, "BoundSlider", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundSliderValue::get, &validated_set<BoundSliderValue, double>)
        .def_prop_ro("range", &BoundSliderValue::range)
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundSliderValue&>(self).observe(cb), cb);
        }, nb::arg("callback"))
        .def("get", &BoundSliderValue::get)
        .def("set", &validated_set<BoundSliderValue, double>)
        .def("set_range", &BoundSliderValue::set_range, nb::arg("min"), nb::arg("max"));

    nb::class_<BoundCheckboxValue>(m, "BoundCheckbox", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundCheckboxValue::get, &validated_set<BoundCheckboxValue, bool>)
        .def("observe", [](nb::object self, nb::callable cb) {
            return keep_connection(self, nb::cast<BoundCheckboxValue&>(self).observe(cb), cb);
        }, nb::arg("callback"))
        .def("get", &BoundCheckboxValue::get)
        .def("set", &validated_set<BoundCheckboxValue, bool>);

    bind_list<int>(m, "Int");
    bind_list<double>(m, "Float");
    bind_list<std::string>(m, "Str");
    // List<bool>/BoundList<bool> disabled — vector<bool> proxy incompatible with const T& Signal; use int list for bool data

    nb::class_<BoundPlot>(m, "BoundPlot", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("add_series", &BoundPlot::add_series, nb::arg("xs"), nb::arg("ys"), nb::arg("color")="", nb::arg("thickness")=2.f, nb::arg("fill")=false)
        .def("clear_series", &BoundPlot::clear_series)
        .def("notify", &BoundPlot::notify)
        .def("replace_series", &BoundPlot::replace_series, nb::arg("xs"), nb::arg("ys") = nb::none(), nb::kw_only(),
             nb::arg("color") = nb::none(), nb::arg("thickness") = 2.f, nb::arg("fill") = false)
        .def("set_labels", &BoundPlot::set_labels, nb::kw_only(), nb::arg("x") = nb::none(), nb::arg("y") = nb::none())
        .def("series_count", &BoundPlot::series_count)
        .def("series_len", &BoundPlot::series_len, nb::arg("i"))
        .def("reset_view", &BoundPlot::reset_view)
        .def_prop_rw("x_label", &BoundPlot::get_x_label, &BoundPlot::set_x_label)
        .def_prop_rw("y_label", &BoundPlot::get_y_label, &BoundPlot::set_y_label);
    nb::class_<PlotHandle>(m, "PlotHandle", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<>())
        .def("add_series", &PlotHandle::add_series, nb::arg("xs"), nb::arg("ys"), nb::arg("color")="", nb::arg("thickness")=2.f, nb::arg("fill")=false)
        .def("clear_series", &PlotHandle::clear_series)
        .def("notify", &PlotHandle::notify)
        .def("replace_series", &PlotHandle::replace_series, nb::arg("xs"), nb::arg("ys") = nb::none(), nb::kw_only(),
             nb::arg("color") = nb::none(), nb::arg("thickness") = 2.f, nb::arg("fill") = false)
        .def("set_labels", &PlotHandle::set_labels, nb::kw_only(), nb::arg("x") = nb::none(), nb::arg("y") = nb::none())
        .def("series_count", &PlotHandle::series_count)
        .def("series_len", &PlotHandle::series_len, nb::arg("i"))
        .def("reset_view", &PlotHandle::reset_view)
        .def_prop_rw("x_label", &PlotHandle::get_x_label, &PlotHandle::set_x_label)
        .def_prop_rw("y_label", &PlotHandle::get_y_label, &PlotHandle::set_y_label);
    nb::class_<BoundTree>(m, "BoundTree", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("refresh", &BoundTree::refresh)
        .def("rows", &BoundTree::rows);
    nb::class_<TreeHandle>(m, "TreeHandle", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<nb::object>(), nb::arg("source"))
        .def("refresh", [](TreeHandle& h){ auto* p=h.ctrl.get(); list_op_dispatch(h.ctrl, [p](){ p->refresh(); }); })
        .def("rows", [](TreeHandle& h){
            auto* p = h.ctrl.get();
            auto snapshot = dispatch_sync_read<std::vector<prism::ui::TreeRow>>([p](){ return snapshot_tree_rows(p); });
            return tree_rows_to_pylist(snapshot);
         });

    nb::class_<ViewBuilder>(m, "ViewBuilder")
        .def("widget", [](ViewBuilder& vb, nb::object h){ py_widget_dispatch(vb, h); }, nb::arg("handle"))
        .def("list", [](ViewBuilder& vb, nb::object h){ py_list_dispatch(vb, h); }, nb::arg("handle"))
        .def("canvas", [](ViewBuilder& vb, nb::object h){ py_canvas_dispatch(vb, h); }, nb::arg("handle"))
        .def("tree", [](ViewBuilder& vb, nb::object h){ py_tree_dispatch(vb, h); }, nb::arg("handle"))
        .def("hstack", [](ViewBuilder& vb, nb::args args){
            // hstack(handle1, handle2, ...) -> Row container with those widgets
            // single callable arg -> container with callable body (for lambda capturing vb)
            if (args.size() == 1 && nb::isinstance<nb::callable>(args[0])) {
                auto fn = nb::cast<nb::callable>(args[0]);
                vb.hstack([&]{ fn(); });
                return;
            }
            vb.hstack([&]{
                for (auto a : args) py_widget_dispatch(vb, nb::cast<nb::object>(a));
            });
        })
        .def("vstack", [](ViewBuilder& vb, nb::args args){
            if (args.size() == 1 && nb::isinstance<nb::callable>(args[0])) {
                auto fn = nb::cast<nb::callable>(args[0]);
                vb.vstack([&]{ fn(); });
                return;
            }
            vb.vstack([&]{
                for (auto a : args) py_widget_dispatch(vb, nb::cast<nb::object>(a));
            });
        });

    nb::class_<PyModel>(m, "Model", nb::type_slots(pymodel_type_slots))
        .def(nb::init<>())
        .def("_set_view_callback", &PyModel::set_view_callback, nb::arg("callback"))
        // Single internal allocator set — deduped (public add_* was duplicate dead code)
        .def("_add_int_internal", [](PyModel& self, int v){
                auto [owner, p] = self.add_int_slot(v);
                BoundField<int> h; h.owner = std::move(owner); h.field = p; return h;
            }, nb::arg("value")=0)
        .def("_add_float_internal", [](PyModel& self, double v){
                auto [owner, p] = self.add_float_slot(v);
                BoundField<double> h; h.owner = std::move(owner); h.field = p; return h;
            }, nb::arg("value")=0.0)
        .def("_add_str_internal", [](PyModel& self, std::string v){
                auto [owner, p] = self.add_str_slot(std::move(v));
                BoundField<std::string> h; h.owner = std::move(owner); h.field = p; return h;
            }, nb::arg("value")="")
        .def("_add_bool_internal", [](PyModel& self, bool v){
                auto [owner, p] = self.add_bool_slot(v);
                BoundField<bool> h; h.owner = std::move(owner); h.field = p; return h;
            }, nb::arg("value")=false)
        .def("_add_slider_internal", [](PyModel& self, double v, double mn, double mx, std::string orientation){
                bool vertical;
                if (orientation == "horizontal") vertical = false;
                else if (orientation == "vertical") vertical = true;
                else throw nb::value_error(("slider(): orientation must be 'horizontal' or 'vertical', got '" + orientation + "'").c_str());
                auto alloc = self.add_slider_slot(v, mn, mx, vertical);
                BoundSliderValue h; h.owner = std::move(alloc.owner); h.field_h = alloc.field_h; h.field_v = alloc.field_v; return h;
            }, nb::arg("value")=0.0, nb::arg("min")=0.0, nb::arg("max")=1.0, nb::arg("orientation")="horizontal")
        .def("_add_checkbox_internal", [](PyModel& self, bool v, std::string label){
                auto [owner, p] = self.add_checkbox_slot(v, label);
                BoundCheckboxValue h; h.owner = std::move(owner); h.field = p; h.label = std::move(label); return h;
            }, nb::arg("value")=false, nb::arg("label")="")
        // Shared internal allocators
        .def("_add_shared_int_internal", [](PyModel& self, int v){
                auto [owner, p] = self.add_shared_int_slot(v);
                BoundShared<int> h; h.owner = std::move(owner); h.shared = p; return h;
            }, nb::arg("value")=0)
        .def("_add_shared_float_internal", [](PyModel& self, double v){
                auto [owner, p] = self.add_shared_float_slot(v);
                BoundShared<double> h; h.owner = std::move(owner); h.shared = p; return h;
            }, nb::arg("value")=0.0)
        .def("_add_shared_str_internal", [](PyModel& self, std::string v){
                auto [owner, p] = self.add_shared_str_slot(std::move(v));
                BoundShared<std::string> h; h.owner = std::move(owner); h.shared = p; return h;
            }, nb::arg("value")="")
        .def("_add_shared_bool_internal", [](PyModel& self, bool v){
                auto [owner, p] = self.add_shared_bool_slot(v);
                BoundShared<bool> h; h.owner = std::move(owner); h.shared = p; return h;
            }, nb::arg("value")=false)
        .def("_add_channel_int_internal", [](PyModel& self){
                auto [owner, p] = self.add_channel_int_slot();
                BoundChannel<int> h; h.owner = std::move(owner); h.channel = p; return h;
            })
        .def("_add_channel_float_internal", [](PyModel& self){
                auto [owner, p] = self.add_channel_float_slot();
                BoundChannel<double> h; h.owner = std::move(owner); h.channel = p; return h;
            })
        .def("_add_channel_str_internal", [](PyModel& self){
                auto [owner, p] = self.add_channel_str_slot();
                BoundChannel<std::string> h; h.owner = std::move(owner); h.channel = p; return h;
            })
        .def("_add_channel_bool_internal", [](PyModel& self){
                auto [owner, p] = self.add_channel_bool_slot();
                BoundChannel<bool> h; h.owner = std::move(owner); h.channel = p; return h;
            })
        .def("_add_derived_int_internal", [](PyModel& self, nb::object fn, std::string name, nb::args deps){
                std::vector<nb::object> v; v.reserve(deps.size());
                for (size_t i=0;i<deps.size();++i) v.push_back(nb::cast<nb::object>(deps[i]));
                auto [owner, p] = self.add_derived_slot_vec<int>(fn, v, name);
                BoundDerived<int> h; h.owner = std::move(owner); h.derived = p; return h;
            })
        .def("_add_derived_float_internal", [](PyModel& self, nb::object fn, std::string name, nb::args deps){
                std::vector<nb::object> v; v.reserve(deps.size());
                for (size_t i=0;i<deps.size();++i) v.push_back(nb::cast<nb::object>(deps[i]));
                auto [owner, p] = self.add_derived_slot_vec<double>(fn, v, name);
                BoundDerived<double> h; h.owner = std::move(owner); h.derived = p; return h;
            })
        .def("_add_derived_str_internal", [](PyModel& self, nb::object fn, std::string name, nb::args deps){
                std::vector<nb::object> v; v.reserve(deps.size());
                for (size_t i=0;i<deps.size();++i) v.push_back(nb::cast<nb::object>(deps[i]));
                auto [owner, p] = self.add_derived_slot_vec<std::string>(fn, v, name);
                BoundDerived<std::string> h; h.owner = std::move(owner); h.derived = p; return h;
            })
        .def("_add_derived_bool_internal", [](PyModel& self, nb::object fn, std::string name, nb::args deps){
                std::vector<nb::object> v; v.reserve(deps.size());
                for (size_t i=0;i<deps.size();++i) v.push_back(nb::cast<nb::object>(deps[i]));
                auto [owner, p] = self.add_derived_slot_vec<bool>(fn, v, name);
                BoundDerived<bool> h; h.owner = std::move(owner); h.derived = p; return h;
            })
        .def("_add_list_int_internal", [](PyModel& self, nb::list py){
                std::vector<int> vec; vec.reserve(nb::len(py)); for(auto h: py) vec.push_back(nb::cast<int>(h));
                auto [owner, p] = self.add_list_int_slot(std::move(vec));
                BoundList<int> h; h.owner = std::move(owner); h.list = p; return h;
            }, nb::arg("values") = nb::list())
        .def("_add_list_float_internal", [](PyModel& self, nb::list py){
                std::vector<double> vec; vec.reserve(nb::len(py)); for(auto h: py) vec.push_back(nb::cast<double>(h));
                auto [owner, p] = self.add_list_float_slot(std::move(vec));
                BoundList<double> h; h.owner = std::move(owner); h.list = p; return h;
            }, nb::arg("values") = nb::list())
        .def("_add_list_str_internal", [](PyModel& self, nb::list py){
                std::vector<std::string> vec; vec.reserve(nb::len(py)); for(auto h: py) vec.push_back(nb::cast<std::string>(h));
                auto [owner, p] = self.add_list_str_slot(std::move(vec));
                BoundList<std::string> h; h.owner = std::move(owner); h.list = p; return h;
            }, nb::arg("values") = nb::list())
        .def("_add_plot_internal", [](PyModel& self){
                auto [owner, p] = self.add_plot_slot();
                BoundPlot h; h.owner = std::move(owner); h.plot = p; return h;
            })
        .def("_add_tree_internal", [](PyModel& self, nb::object py_obj){
                auto [owner, p] = self.add_tree_slot(py_obj);
                BoundTree h; h.owner = std::move(owner); h.ctrl = p; return h;
            }, nb::arg("source"));


    m.def("_txn_begin", [](){
        txn_marks.push_back(txn_queue.size());
        ++txn_depth;
    });
    m.def("_txn_commit", [](){
        if (txn_depth == 0) return;
        if (--txn_depth == 0) {
            txn_marks.clear();
            txn_flush_batch();
        } else {
            txn_marks.pop_back();
        }
    });
    m.def("_txn_abort", [](){
        if (txn_depth == 0) return;
        size_t mark = txn_marks.empty() ? 0 : txn_marks.back();
        txn_queue.resize(mark);
        txn_marks.pop_back();
        if (--txn_depth == 0) {
            txn_marks.clear();
            txn_queue.clear();
            txn_queue.shrink_to_fit();
        }
    });

    m.def("_is_running", [](){ return g_has_handle.load(std::memory_order_acquire) || g_run_guard.load(std::memory_order_acquire); });
    m.def("_request_quit", [](){
        // Must set the flag under the same mutex wait_for() locks — otherwise a
        // notify landing between the waiter's predicate check and its actual
        // wait (inside wait_for's own lock) is a lost wakeup: the flag flip and
        // the notify are invisible to a waiter that hasn't started waiting yet
        // and has no lock held to serialize against.
        {
            std::lock_guard<std::mutex> lk(g_headless_cv_mutex);
            g_headless_quit_requested.store(true, std::memory_order_release);
        }
        g_headless_cv.notify_all();
    });
    m.def("_fail_next_run", [](){ g_fail_next_run.store(true, std::memory_order_release); });
    m.def("_run_headless", [](PyModel& model, int delay_ms){
        {
            bool expected = false;
            if (!g_run_guard.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                throw std::runtime_error("prism.run already running");
            g_app_closed.store(false, std::memory_order_release);
            g_headless_quit_requested.store(false, std::memory_order_release);
        }
        RunGuardReset reset_guard;
        if (g_fail_next_run.exchange(false, std::memory_order_acq_rel))
            throw std::runtime_error("_fail_next_run(): forced failure for testing");
        nb::gil_scoped_release release;
        auto backend = Backend{std::make_unique<DelayHeadlessBackend>(delay_ms)};
        auto& window = backend.create_window({});
        auto setup = [](AppContext& ctx){
            // Install GIL wrapper for logic-thread drains (mouse/tick) — SDL-free core hook
            ctx.set_logic_wrapper([](std::function<void()> fn){
                // Once the interpreter is finalized, nothing that might drain Python
                // callbacks (mouse/tick logic-thread work) may run — drop it.
                // Safe: g_post_handle is reset before run()/_run_headless() returns, so no post can reach this drain anyway.
                if (!Py_IsInitialized()) return;
                nb::gil_scoped_acquire g;
                fn();
            });
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle = ctx.post_handle();
            g_has_handle.store(true, std::memory_order_release);
            g_app_closed.store(false, std::memory_order_release);
        };
        model_app(backend, window, model, setup);
    }, nb::arg("model"), nb::arg("delay_ms")=100);

    m.def("run", [](PyModel& model, std::string title){
        {
            bool expected = false;
            if (!g_run_guard.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                throw std::runtime_error("prism.run already running");
            g_app_closed.store(false, std::memory_order_release);
        }
        RunGuardReset reset_guard;
        if (g_fail_next_run.exchange(false, std::memory_order_acq_rel))
            throw std::runtime_error("_fail_next_run(): forced failure for testing");
        // Must be called from main thread on macOS
        nb::gil_scoped_release release;
        auto backend = Backend::software(RenderConfig{});
        WindowConfig cfg;
        cfg.title = title.c_str();
        auto& window = backend.create_window(cfg);
        auto setup = [](AppContext& ctx){
            ctx.set_logic_wrapper([](std::function<void()> fn){
                // Once the interpreter is finalized, nothing that might drain Python
                // callbacks (mouse/tick logic-thread work) may run — drop it.
                // Safe: g_post_handle is reset before run()/_run_headless() returns, so no post can reach this drain anyway.
                if (!Py_IsInitialized()) return;
                nb::gil_scoped_acquire g;
                fn();
            });
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle = ctx.post_handle();
            g_has_handle.store(true, std::memory_order_release);
            g_app_closed.store(false, std::memory_order_release);
        };
        model_app(backend, window, model, setup);
    }, nb::arg("model"), nb::arg("title")="PRISM App");
}