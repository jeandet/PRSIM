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
#include <prism/widgets/plot.hpp>
#include <prism/ui/tree.hpp>

#include <prism/app/headless_window.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace nb = nanobind;
using namespace prism::core;
using namespace prism::app;

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
    if (g_run_guard.load(std::memory_order_acquire) && !g_has_handle.load(std::memory_order_acquire)) {
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
            // for the try_post path which bypasses AppContext::post.
            if (Py_IsInitialized()) {
                nb::gil_scoped_acquire gil;
                drain_queue_loop(qq, sf, tp);
            } else {
                drain_queue_loop(qq, sf, tp);
            }
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
    // Startup window: spin briefly like write path before falling back to direct.
    if (g_run_guard.load(std::memory_order_acquire) && !g_has_handle.load(std::memory_order_acquire)) {
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
    // Block caller (off logic thread) until logic thread runs reader — release GIL while waiting
    {
        nb::gil_scoped_release rel;
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
inline bool txn_buffer_or_dispatch(Field<T>* field, const T& v) {
    if (!txn_active()) return false;
    T copy = v;
    txn_queue.emplace_back([field, copy = std::move(copy)]() mutable {
        field->set(std::move(copy));
    });
    return true;
}

// Helper to post or direct-set a Field.
template <typename T>
void field_set_dispatch(Field<T>* field, T v) {
    if (txn_buffer_or_dispatch(field, v)) return;
    if (!prism::app::detail_is_logic_thread) {
        T copy = v;
        auto res = try_post_via_handle_impl([field, copy = std::move(copy)]() mutable {
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

// Standalone field (owns storage) — for quick tests / non-model usage.
template <typename T>
struct FieldHandle {
    Field<T> field;
    FieldHandle(T init) : field(std::move(init)) {}
    T get() const {
        const Field<T>* p = &field;
        return dispatch_sync_read<T>([p](){ return p->get(); });
    }
    void set(T v) { field_set_dispatch(&field, std::move(v)); }
    Connection observe(nb::callable cb) {
        auto wrapper = [cb](const T& val) {
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire acq;
            try { cb(val); } catch (nb::python_error&) { PyErr_Print(); } catch (...) {}
        };
        return field.on_change().connect(std::move(wrapper));
    }
};

// Standalone Shared<T>/Channel<T> handles (built via nb::init<>, not owned by a PyModel) are not
// in any PyModel::slots vector, so PyModel::drain() never reaches them. Register their drain
// functions here so PyModel::drain() can sweep them too — see drain_standalone() below.
struct StandaloneDrainers {
    std::mutex m;
    std::vector<std::function<void()>*> fns;
    static StandaloneDrainers& instance() {
        static StandaloneDrainers inst;
        return inst;
    }
};

static void register_standalone_drainer(std::function<void()>* fn) {
    auto& reg = StandaloneDrainers::instance();
    std::lock_guard<std::mutex> lk(reg.m);
    reg.fns.push_back(fn);
}

static void unregister_standalone_drainer(std::function<void()>* fn) {
    auto& reg = StandaloneDrainers::instance();
    std::lock_guard<std::mutex> lk(reg.m);
    auto it = std::find(reg.fns.begin(), reg.fns.end(), fn);
    if (it != reg.fns.end()) reg.fns.erase(it);
}

// Snapshot-then-call is only UAF-safe because a handle's destructor (which unregisters
// its pointer) can only run under the GIL, and this whole sweep also runs under the one
// continuously-held GIL of the logic-thread tick (logic_wrapper -> widget_tree.hpp:684 ->
// model_app.hpp:250) — so no destructor can interleave between the snapshot and the calls.
// Releasing the GIL mid-sweep, or invoking drain_fn from a thread outside that wrapper,
// would let a concurrent handle destructor dangle these raw pointers.
static void drain_standalone() {
    std::vector<std::function<void()>*> snapshot;
    {
        auto& reg = StandaloneDrainers::instance();
        std::lock_guard<std::mutex> lk(reg.m);
        snapshot = reg.fns;
    }
    for (auto* fn : snapshot) (*fn)();
}

// Type-erased slot for PyModel view — defined before Bound* so they can hold shared_ptr to it.
struct SlotBase {
    virtual ~SlotBase() = default;
    virtual void build(ViewBuilder& vb) = 0;
    virtual void drain() {}
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

// Forward declarations for dispatch helpers used by Plot/Tree handles
inline void list_op_dispatch(std::function<void()> fn);
template <typename T> T dispatch_sync_read(std::function<T()> reader);
template <typename T> void field_set_dispatch(Field<T>* field, T v);

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
struct PlotHandle {
    prism::plot::PlotModel plot;
    void add_series(nb::list xs, nb::list ys, std::string color_str = "", float thickness = 2.f, bool fill = false) {
        std::vector<double> vx, vy;
        vx.reserve(nb::len(xs)); vy.reserve(nb::len(ys));
        for (auto h : xs) vx.push_back(nb::cast<double>(h));
        for (auto h : ys) vy.push_back(nb::cast<double>(h));
        auto* p = &plot;
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
        list_op_dispatch(std::move(fn));
    }
    void clear_series(){ auto* p=&plot; list_op_dispatch([p](){ p->clear_series(); }); }
    void notify(){ auto* p=&plot; list_op_dispatch([p](){ p->notify(); }); }
    void set_x_label(std::string s){ field_set_dispatch(&plot.x_label, std::move(s)); }
    void set_y_label(std::string s){ field_set_dispatch(&plot.y_label, std::move(s)); }
    std::string get_x_label() const { auto* p=&plot; return dispatch_sync_read<std::string>([p](){ return p->x_label.get(); }); }
    std::string get_y_label() const { auto* p=&plot; return dispatch_sync_read<std::string>([p](){ return p->y_label.get(); }); }
    void reset_view(){ auto* p=&plot; list_op_dispatch([p](){ p->reset_view(); }); }
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
};
struct BoundTree {
    std::shared_ptr<SlotBase> owner;
    prism::ui::TreeController* ctrl = nullptr;
    void refresh(){ if(ctrl){ auto* p=ctrl; list_op_dispatch([p](){ p->refresh(); }); } }
    nb::list rows(){
        if(!ctrl) return nb::list();
        auto* p = ctrl;
        return dispatch_sync_read<nb::list>([p](){
            nb::gil_scoped_acquire g;
            nb::list out;
            for(size_t i=0;i<p->rows.size();++i){
                auto r = p->rows[i];
                nb::dict d;
                d["label"] = r.label;
                d["depth"] = r.depth;
                d["has_children"] = r.has_children;
                d["expanded"] = r.expanded;
                d["selected"] = r.selected;
                out.append(d);
            }
            return out;
        });
    }
};
struct TreeHandle {
    std::shared_ptr<prism::ui::TreeController> ctrl;
    TreeHandle(prism::ui::TreeSource src): ctrl(std::make_shared<prism::ui::TreeController>(std::move(src))){}
    TreeHandle(nb::object py_obj): ctrl(std::make_shared<prism::ui::TreeController>(PythonTreeSource::make(py_obj))){}
};

// --- List dispatch helper (mirrors field_set_dispatch but for arbitrary op) ---
inline void list_op_dispatch(std::function<void()> fn) {
    if (txn_active()) { txn_queue.emplace_back(std::move(fn)); return; }
    if (!prism::app::detail_is_logic_thread) {
        auto fn_copy = fn;
        auto res = try_post_via_handle_impl([fn_copy = std::move(fn_copy)]() mutable { fn_copy(); }, false);
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
        if (field) field_set_dispatch(field, std::move(v));
    }
    Connection observe(nb::callable cb) {
        if (!field) return {};
        auto owner_copy = owner;
        Field<T>* f = field;
        auto wrapper = [cb, f](const T& val) {
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire acq;
            try { cb(val); } catch (nb::python_error&) { PyErr_Print(); } catch (...) {}
        };
        auto conn = f->on_change().connect(std::move(wrapper));
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};

// Standalone / bound handles for Shared<T> and Channel<T>
template <typename T>
struct SharedHandle {
    Shared<T> shared;
    std::function<void()> drain_fn;
    SharedHandle(T init) : shared(std::move(init)) {
        drain_fn = [this] { shared.drain_notifications(); };
        register_standalone_drainer(&drain_fn);
    }
    ~SharedHandle() { unregister_standalone_drainer(&drain_fn); }
    SharedHandle(const SharedHandle&) = delete;
    SharedHandle& operator=(const SharedHandle&) = delete;
    SharedHandle(SharedHandle&&) = delete;
    SharedHandle& operator=(SharedHandle&&) = delete;
    T get() const { return shared.get(); }
    void set(T v) { shared.set(std::move(v)); ensure_idle_wake(); }
    Connection observe(nb::callable cb) {
        auto wrapper = [cb](const T& val) {
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire acq;
            try { cb(val); } catch (nb::python_error&) { PyErr_Print(); } catch (...) {}
        };
        return shared.on_change().connect(std::move(wrapper));
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
        if (!shared) return {};
        auto owner_copy = owner;
        Shared<T>* s = shared;
        auto wrapper = [cb, s](const T& val) {
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire acq;
            try { cb(val); } catch (nb::python_error&) { PyErr_Print(); } catch (...) {}
        };
        auto conn = s->on_change().connect(std::move(wrapper));
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};
template <typename T>
struct ChannelHandle {
    Channel<T> channel;
    std::function<void()> drain_fn;
    ChannelHandle() {
        drain_fn = [this] { channel.drain_notifications(); };
        register_standalone_drainer(&drain_fn);
    }
    ~ChannelHandle() { unregister_standalone_drainer(&drain_fn); }
    ChannelHandle(const ChannelHandle&) = delete;
    ChannelHandle& operator=(const ChannelHandle&) = delete;
    ChannelHandle(ChannelHandle&&) = delete;
    ChannelHandle& operator=(ChannelHandle&&) = delete;
    void send(T v) { channel.send(std::move(v)); ensure_idle_wake(); }
    Connection observe(nb::callable cb) {
        auto wrapper = [cb](const T& val) {
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire acq;
            try { cb(val); } catch (nb::python_error&) { PyErr_Print(); } catch (...) {}
        };
        return channel.on_receive().connect(std::move(wrapper));
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
        if (!channel) return {};
        auto owner_copy = owner;
        Channel<T>* c = channel;
        auto wrapper = [cb, c](const T& val) {
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire acq;
            try { cb(val); } catch (nb::python_error&) { PyErr_Print(); } catch (...) {}
        };
        auto conn = c->on_receive().connect(std::move(wrapper));
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
    std::vector<Connection> deps_;
    std::vector<std::shared_ptr<SlotBase>> dep_owners_;
    std::vector<nb::object> dep_keepalive_; // keeps standalone handles alive
    std::vector<Connection> observers_;
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
            try { nv = nb::cast<T>(py_fn()); } catch (nb::python_error& e) { e.restore(); PyErr_Print(); return; } catch (...) { return; }
        }
        if (nv == value_) return;
        value_ = std::move(nv);
        emit_or_defer(static_cast<void*>(&changed_), [this]{ changed_.emit(value_); });
    }
    void build(ViewBuilder& vb) override { vb.widget_generic<T>(*this); }
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
        if (!derived) return {};
        auto owner_copy = owner;
        auto* d = derived;
        auto wrapper = [cb, d](const T& v){ if (!Py_IsInitialized()) return; nb::gil_scoped_acquire g; try{cb(v);}catch(nb::python_error&){PyErr_Print();}catch(...){} };
        auto conn = d->on_change().connect(std::move(wrapper));
        if (owner_copy) conn.keep_alive(owner_copy);
        return conn;
    }
};
template <typename T>
struct ListHandle {
    List<T> list;
    void push(T v) {
        List<T>* p = &list;
        list_op_dispatch([p, v = std::move(v)]() mutable { p->push_back(std::move(v)); });
    }
    void erase(size_t i) {
        List<T>* p = &list;
        list_op_dispatch([p, i](){ if (i < p->size()) p->erase(i); });
    }
    void set(size_t i, T v) {
        List<T>* p = &list;
        list_op_dispatch([p, i, v = std::move(v)]() mutable { if (i < p->size()) p->set(i, std::move(v)); });
    }
    void replace_all(nb::list py) {
        std::vector<T> vec; vec.reserve(nb::len(py));
        for (auto h : py) vec.push_back(nb::cast<T>(h));
        List<T>* p = &list;
        list_op_dispatch([p, vec = std::move(vec)]() mutable { p->replace_all(vec); });
    }
    size_t size() const {
        const List<T>* p = &list;
        return dispatch_sync_read<size_t>([p](){ return p->size(); });
    }
    T get(size_t i) const {
        const List<T>* p = &list;
        return dispatch_sync_read<T>([p,i](){ return i < p->size() ? (*p)[i] : T{}; });
    }
    nb::list to_list() const {
        const List<T>* p = &list;
        return dispatch_sync_read<nb::list>([p](){
            nb::gil_scoped_acquire g;
            nb::list out; for (size_t i=0;i<p->size();++i) out.append((*p)[i]); return out;
        });
    }
    Connection observe_insert(nb::callable cb) {
        auto w=[cb](size_t idx, const T& v){ if(!Py_IsInitialized()) return; nb::gil_scoped_acquire g; try{cb(idx,v);}catch(nb::python_error&){PyErr_Print();}catch(...){} };
        return list.on_insert().connect(std::move(w));
    }
    Connection observe_remove(nb::callable cb) {
        auto w=[cb](size_t idx){ if(!Py_IsInitialized()) return; nb::gil_scoped_acquire g; try{cb(idx);}catch(nb::python_error&){PyErr_Print();}catch(...){} };
        return list.on_remove().connect(std::move(w));
    }
    Connection observe_update(nb::callable cb) {
        auto w=[cb](size_t idx, const T& v){ if(!Py_IsInitialized()) return; nb::gil_scoped_acquire g; try{cb(idx,v);}catch(nb::python_error&){PyErr_Print();}catch(...){} };
        return list.on_update().connect(std::move(w));
    }
};
template <typename T>
struct BoundList {
    std::shared_ptr<SlotBase> owner;
    List<T>* list = nullptr;
    void push(T v) { if(list) { auto* p=list; list_op_dispatch([p, v=std::move(v)]() mutable { p->push_back(std::move(v)); }); } }
    void erase(size_t i) { if(list) { auto* p=list; list_op_dispatch([p,i](){ if(i<p->size()) p->erase(i); }); } }
    void set(size_t i, T v) { if(list) { auto* p=list; list_op_dispatch([p,i,v=std::move(v)]() mutable { if(i<p->size()) p->set(i,std::move(v)); }); } }
    void replace_all(nb::list py) { if(!list) return; std::vector<T> vec; vec.reserve(nb::len(py)); for(auto h:py) vec.push_back(nb::cast<T>(h)); auto* p=list; list_op_dispatch([p, vec=std::move(vec)]() mutable { p->replace_all(vec); }); }
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
        return dispatch_sync_read<nb::list>([p](){
            nb::gil_scoped_acquire g;
            nb::list out; for (size_t i=0;i<p->size();++i) out.append((*p)[i]); return out;
        });
    }
    Connection observe_insert(nb::callable cb) {
        if(!list) return {}; auto owner_copy=owner; auto* p=list; auto w=[cb](size_t idx, const T& v){ if(!Py_IsInitialized()) return; nb::gil_scoped_acquire g; try{cb(idx,v);}catch(nb::python_error&){PyErr_Print();}catch(...){} }; auto c=p->on_insert().connect(std::move(w)); if(owner_copy) c.keep_alive(owner_copy); return c;
    }
    Connection observe_remove(nb::callable cb) {
        if(!list) return {}; auto owner_copy=owner; auto* p=list; auto w=[cb](size_t idx){ if(!Py_IsInitialized()) return; nb::gil_scoped_acquire g; try{cb(idx);}catch(nb::python_error&){PyErr_Print();}catch(...){} }; auto c=p->on_remove().connect(std::move(w)); if(owner_copy) c.keep_alive(owner_copy); return c;
    }
    Connection observe_update(nb::callable cb) {
        if(!list) return {}; auto owner_copy=owner; auto* p=list; auto w=[cb](size_t idx, const T& v){ if(!Py_IsInitialized()) return; nb::gil_scoped_acquire g; try{cb(idx,v);}catch(nb::python_error&){PyErr_Print();}catch(...){} }; auto c=p->on_update().connect(std::move(w)); if(owner_copy) c.keep_alive(owner_copy); return c;
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
        list_op_dispatch(std::move(fn));
    }
    void clear_series() {
        if (!plot) return;
        auto* p = plot;
        list_op_dispatch([p](){ p->clear_series(); });
    }
    void notify() {
        if (!plot) return;
        auto* p = plot;
        list_op_dispatch([p](){ p->notify(); });
    }
    void set_x_label(std::string s) { if(plot) field_set_dispatch(&plot->x_label, std::move(s)); }
    void set_y_label(std::string s) { if(plot) field_set_dispatch(&plot->y_label, std::move(s)); }
    std::string get_x_label() const { if(!plot) return ""; return dispatch_sync_read<std::string>([p=plot](){ return p->x_label.get(); }); }
    std::string get_y_label() const { if(!plot) return ""; return dispatch_sync_read<std::string>([p=plot](){ return p->y_label.get(); }); }
    void reset_view() { if(plot){ auto* p=plot; list_op_dispatch([p](){ p->reset_view(); }); } }
};

// helper to attach a single dep to a derived slot
template <typename FH>
auto* field_ptr_of(FH& h) {
    if constexpr (std::is_pointer_v<decltype(h.field)>) return h.field;
    else return &h.field;
}
template <typename SH>
auto* shared_ptr_of(SH& h) {
    if constexpr (std::is_pointer_v<decltype(h.shared)>) return h.shared;
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
    // Bound handles
    if (connect_field((BoundField<int>*)nullptr)) return;
    if (connect_field((BoundField<double>*)nullptr)) return;
    if (connect_field((BoundField<std::string>*)nullptr)) return;
    if (connect_field((BoundField<bool>*)nullptr)) return;
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
    std::pair<std::shared_ptr<SlotBase>, SlotDerived<T>*> add_derived_slot_vec(nb::object fn, const std::vector<nb::object>& deps) {
        T init{};
        {
            nb::gil_scoped_acquire g;
            try { init = nb::cast<T>(fn()); } catch (...) {}
        }
        auto s = std::make_shared<SlotDerived<T>>(std::move(fn), std::move(init));
        for (auto& dep : deps) derived_attach_dep(s, dep);
        std::lock_guard<std::mutex> lk(slots_mutex);
        slots.push_back(s);
        return {s, s.get()};
    }
    // kept for tuple-compat if needed
    template <typename T>
    std::pair<std::shared_ptr<SlotBase>, SlotDerived<T>*> add_derived_slot(nb::object fn, nb::tuple deps) {
        std::vector<nb::object> v;
        v.reserve(deps.size());
        for (size_t i=0;i<deps.size();++i) v.push_back(nb::cast<nb::object>(deps[i]));
        return add_derived_slot_vec<T>(std::move(fn), v);
    }
    std::pair<std::shared_ptr<SlotBase>, SlotDerived<int>*> add_derived_int_slot(nb::object fn, nb::tuple deps) { return add_derived_slot<int>(std::move(fn), std::move(deps)); }
    std::pair<std::shared_ptr<SlotBase>, SlotDerived<double>*> add_derived_double_slot(nb::object fn, nb::tuple deps) { return add_derived_slot<double>(std::move(fn), std::move(deps)); }
    std::pair<std::shared_ptr<SlotBase>, SlotDerived<std::string>*> add_derived_str_slot(nb::object fn, nb::tuple deps) { return add_derived_slot<std::string>(std::move(fn), std::move(deps)); }
    std::pair<std::shared_ptr<SlotBase>, SlotDerived<bool>*> add_derived_bool_slot(nb::object fn, nb::tuple deps) { return add_derived_slot<bool>(std::move(fn), std::move(deps)); }

    // Legacy raw-pointer accessors (kept for internal use if needed)
    Field<int>* add_int(int v) { return add_int_slot(v).second; }
    Field<double>* add_float(double v) { return add_float_slot(v).second; }
    Field<std::string>* add_str(std::string v) { return add_str_slot(std::move(v)).second; }
    Field<bool>* add_bool(bool v) { return add_bool_slot(v).second; }

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
                } catch (nb::python_error& e) {
                    e.restore();
                    PyErr_Print();
                } catch (...) {}
                return;
            }
        }
        std::lock_guard<std::mutex> lk(slots_mutex);
        for (auto& s : slots) s->build(vb);
    }

    void drain() {
        {
            std::lock_guard<std::mutex> lk(slots_mutex);
            for (auto& s : slots) s->drain();
        }
        drain_standalone();
    }
};

NB_MODULE(_prism_ext, m) {
    m.def("is_logic_thread", [](){ return detail_is_logic_thread; });

    nb::class_<Connection>(m, "Connection", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("disconnect", &Connection::disconnect)
        .def("__enter__", [](Connection& self){ return &self; })
        .def("__exit__", [](Connection& self, nb::object, nb::object, nb::object){ self.disconnect(); return false; });

    nb::class_<FieldHandle<int>>(m, "FieldInt", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<int>(), nb::arg("value") = 0)
        .def_prop_rw("value", &FieldHandle<int>::get, &FieldHandle<int>::set)
        .def("observe", &FieldHandle<int>::observe, nb::keep_alive<0, 1>(), nb::arg("callback"))
        .def("get", &FieldHandle<int>::get)
        .def("set", &FieldHandle<int>::set);
    nb::class_<FieldHandle<double>>(m, "FieldFloat", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<double>(), nb::arg("value") = 0.0)
        .def_prop_rw("value", &FieldHandle<double>::get, &FieldHandle<double>::set)
        .def("observe", &FieldHandle<double>::observe, nb::keep_alive<0, 1>())
        .def("get", &FieldHandle<double>::get)
        .def("set", &FieldHandle<double>::set);
    nb::class_<FieldHandle<std::string>>(m, "FieldStr", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<std::string>(), nb::arg("value") = "")
        .def_prop_rw("value", &FieldHandle<std::string>::get, &FieldHandle<std::string>::set)
        .def("observe", &FieldHandle<std::string>::observe, nb::keep_alive<0, 1>())
        .def("get", &FieldHandle<std::string>::get)
        .def("set", &FieldHandle<std::string>::set);
    nb::class_<FieldHandle<bool>>(m, "FieldBool", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<bool>(), nb::arg("value") = false)
        .def_prop_rw("value", &FieldHandle<bool>::get, &FieldHandle<bool>::set)
        .def("observe", &FieldHandle<bool>::observe, nb::keep_alive<0, 1>())
        .def("get", &FieldHandle<bool>::get)
        .def("set", &FieldHandle<bool>::set);

    nb::class_<BoundField<int>>(m, "BoundInt", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundField<int>::get, &BoundField<int>::set)
        .def("observe", &BoundField<int>::observe)
        .def("get", &BoundField<int>::get)
        .def("set", &BoundField<int>::set);
    nb::class_<BoundField<double>>(m, "BoundFloat", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundField<double>::get, &BoundField<double>::set)
        .def("observe", &BoundField<double>::observe)
        .def("get", &BoundField<double>::get)
        .def("set", &BoundField<double>::set);
    nb::class_<BoundField<std::string>>(m, "BoundStr", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundField<std::string>::get, &BoundField<std::string>::set)
        .def("observe", &BoundField<std::string>::observe)
        .def("get", &BoundField<std::string>::get)
        .def("set", &BoundField<std::string>::set);
    nb::class_<BoundField<bool>>(m, "BoundBool", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundField<bool>::get, &BoundField<bool>::set)
        .def("observe", &BoundField<bool>::observe)
        .def("get", &BoundField<bool>::get)
        .def("set", &BoundField<bool>::set);

    // Standalone Shared handles
    nb::class_<SharedHandle<int>>(m, "SharedInt", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<int>(), nb::arg("value") = 0)
        .def_prop_rw("value", &SharedHandle<int>::get, &SharedHandle<int>::set)
        .def("observe", &SharedHandle<int>::observe, nb::keep_alive<0, 1>(), nb::arg("callback"))
        .def("get", &SharedHandle<int>::get).def("set", &SharedHandle<int>::set);
    nb::class_<SharedHandle<double>>(m, "SharedFloat", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<double>(), nb::arg("value") = 0.0)
        .def_prop_rw("value", &SharedHandle<double>::get, &SharedHandle<double>::set)
        .def("observe", &SharedHandle<double>::observe, nb::keep_alive<0, 1>())
        .def("get", &SharedHandle<double>::get).def("set", &SharedHandle<double>::set);
    nb::class_<SharedHandle<std::string>>(m, "SharedStr", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<std::string>(), nb::arg("value") = "")
        .def_prop_rw("value", &SharedHandle<std::string>::get, &SharedHandle<std::string>::set)
        .def("observe", &SharedHandle<std::string>::observe, nb::keep_alive<0, 1>())
        .def("get", &SharedHandle<std::string>::get).def("set", &SharedHandle<std::string>::set);
    nb::class_<SharedHandle<bool>>(m, "SharedBool", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<bool>(), nb::arg("value") = false)
        .def_prop_rw("value", &SharedHandle<bool>::get, &SharedHandle<bool>::set)
        .def("observe", &SharedHandle<bool>::observe, nb::keep_alive<0, 1>())
        .def("get", &SharedHandle<bool>::get).def("set", &SharedHandle<bool>::set);

    nb::class_<BoundShared<int>>(m, "BoundSharedInt", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundShared<int>::get, &BoundShared<int>::set)
        .def("observe", &BoundShared<int>::observe)
        .def("get", &BoundShared<int>::get).def("set", &BoundShared<int>::set);
    nb::class_<BoundShared<double>>(m, "BoundSharedFloat", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundShared<double>::get, &BoundShared<double>::set)
        .def("observe", &BoundShared<double>::observe)
        .def("get", &BoundShared<double>::get).def("set", &BoundShared<double>::set);
    nb::class_<BoundShared<std::string>>(m, "BoundSharedStr", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundShared<std::string>::get, &BoundShared<std::string>::set)
        .def("observe", &BoundShared<std::string>::observe)
        .def("get", &BoundShared<std::string>::get).def("set", &BoundShared<std::string>::set);
    nb::class_<BoundShared<bool>>(m, "BoundSharedBool", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_rw("value", &BoundShared<bool>::get, &BoundShared<bool>::set)
        .def("observe", &BoundShared<bool>::observe)
        .def("get", &BoundShared<bool>::get).def("set", &BoundShared<bool>::set);

    // Channel handles
    nb::class_<ChannelHandle<int>>(m, "ChannelInt", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<>()).def("send", &ChannelHandle<int>::send).def("observe", &ChannelHandle<int>::observe, nb::keep_alive<0, 1>());
    nb::class_<ChannelHandle<double>>(m, "ChannelFloat", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<>()).def("send", &ChannelHandle<double>::send).def("observe", &ChannelHandle<double>::observe, nb::keep_alive<0, 1>());
    nb::class_<ChannelHandle<std::string>>(m, "ChannelStr", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<>()).def("send", &ChannelHandle<std::string>::send).def("observe", &ChannelHandle<std::string>::observe, nb::keep_alive<0, 1>());
    nb::class_<ChannelHandle<bool>>(m, "ChannelBool", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<>()).def("send", &ChannelHandle<bool>::send).def("observe", &ChannelHandle<bool>::observe, nb::keep_alive<0, 1>());

    nb::class_<BoundChannel<int>>(m, "BoundChannelInt", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("send", &BoundChannel<int>::send).def("observe", &BoundChannel<int>::observe);
    nb::class_<BoundChannel<double>>(m, "BoundChannelFloat", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("send", &BoundChannel<double>::send).def("observe", &BoundChannel<double>::observe);
    nb::class_<BoundChannel<std::string>>(m, "BoundChannelStr", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("send", &BoundChannel<std::string>::send).def("observe", &BoundChannel<std::string>::observe);
    nb::class_<BoundChannel<bool>>(m, "BoundChannelBool", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("send", &BoundChannel<bool>::send).def("observe", &BoundChannel<bool>::observe);

    nb::class_<BoundDerived<int>>(m, "BoundDerivedInt", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_ro("value", &BoundDerived<int>::get).def("get", &BoundDerived<int>::get)
        .def("observe", &BoundDerived<int>::observe);
    nb::class_<BoundDerived<double>>(m, "BoundDerivedFloat", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_ro("value", &BoundDerived<double>::get).def("get", &BoundDerived<double>::get)
        .def("observe", &BoundDerived<double>::observe);
    nb::class_<BoundDerived<std::string>>(m, "BoundDerivedStr", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_ro("value", &BoundDerived<std::string>::get).def("get", &BoundDerived<std::string>::get)
        .def("observe", &BoundDerived<std::string>::observe);
    nb::class_<BoundDerived<bool>>(m, "BoundDerivedBool", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def_prop_ro("value", &BoundDerived<bool>::get).def("get", &BoundDerived<bool>::get)
        .def("observe", &BoundDerived<bool>::observe);

    nb::class_<ListHandle<int>>(m, "ListInt", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<>()).def("push", &ListHandle<int>::push).def("erase", &ListHandle<int>::erase)
        .def("set", &ListHandle<int>::set).def("replace_all", &ListHandle<int>::replace_all)
        .def("size", &ListHandle<int>::size).def("get", &ListHandle<int>::get).def("to_list", &ListHandle<int>::to_list)
        .def("observe_insert", &ListHandle<int>::observe_insert, nb::keep_alive<0, 1>()).def("observe_remove", &ListHandle<int>::observe_remove, nb::keep_alive<0, 1>()).def("observe_update", &ListHandle<int>::observe_update, nb::keep_alive<0, 1>());
    nb::class_<ListHandle<double>>(m, "ListFloat", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<>()).def("push", &ListHandle<double>::push).def("erase", &ListHandle<double>::erase)
        .def("set", &ListHandle<double>::set).def("replace_all", &ListHandle<double>::replace_all)
        .def("size", &ListHandle<double>::size).def("get", &ListHandle<double>::get).def("to_list", &ListHandle<double>::to_list)
        .def("observe_insert", &ListHandle<double>::observe_insert, nb::keep_alive<0, 1>()).def("observe_remove", &ListHandle<double>::observe_remove, nb::keep_alive<0, 1>()).def("observe_update", &ListHandle<double>::observe_update, nb::keep_alive<0, 1>());
    nb::class_<ListHandle<std::string>>(m, "ListStr", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<>()).def("push", &ListHandle<std::string>::push).def("erase", &ListHandle<std::string>::erase)
        .def("set", &ListHandle<std::string>::set).def("replace_all", &ListHandle<std::string>::replace_all)
        .def("size", &ListHandle<std::string>::size).def("get", &ListHandle<std::string>::get).def("to_list", &ListHandle<std::string>::to_list)
        .def("observe_insert", &ListHandle<std::string>::observe_insert, nb::keep_alive<0, 1>()).def("observe_remove", &ListHandle<std::string>::observe_remove, nb::keep_alive<0, 1>()).def("observe_update", &ListHandle<std::string>::observe_update, nb::keep_alive<0, 1>());
    // List<bool> disabled — vector<bool> proxy incompatible with const T& Signal; use int list for bool data


    nb::class_<BoundList<int>>(m, "BoundListInt", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("push", &BoundList<int>::push).def("erase", &BoundList<int>::erase).def("set", &BoundList<int>::set).def("replace_all", &BoundList<int>::replace_all)
        .def("size", &BoundList<int>::size).def("get", &BoundList<int>::get).def("to_list", &BoundList<int>::to_list)
        .def("observe_insert", &BoundList<int>::observe_insert).def("observe_remove", &BoundList<int>::observe_remove).def("observe_update", &BoundList<int>::observe_update);
    nb::class_<BoundList<double>>(m, "BoundListFloat", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("push", &BoundList<double>::push).def("erase", &BoundList<double>::erase).def("set", &BoundList<double>::set).def("replace_all", &BoundList<double>::replace_all)
        .def("size", &BoundList<double>::size).def("get", &BoundList<double>::get).def("to_list", &BoundList<double>::to_list)
        .def("observe_insert", &BoundList<double>::observe_insert).def("observe_remove", &BoundList<double>::observe_remove).def("observe_update", &BoundList<double>::observe_update);
    nb::class_<BoundList<std::string>>(m, "BoundListStr", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("push", &BoundList<std::string>::push).def("erase", &BoundList<std::string>::erase).def("set", &BoundList<std::string>::set).def("replace_all", &BoundList<std::string>::replace_all)
        .def("size", &BoundList<std::string>::size).def("get", &BoundList<std::string>::get).def("to_list", &BoundList<std::string>::to_list)
        .def("observe_insert", &BoundList<std::string>::observe_insert).def("observe_remove", &BoundList<std::string>::observe_remove).def("observe_update", &BoundList<std::string>::observe_update);
    // BoundList<bool> disabled — see above


    nb::class_<BoundPlot>(m, "BoundPlot", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("add_series", &BoundPlot::add_series, nb::arg("xs"), nb::arg("ys"), nb::arg("color")="", nb::arg("thickness")=2.f, nb::arg("fill")=false)
        .def("clear_series", &BoundPlot::clear_series)
        .def("notify", &BoundPlot::notify)
        .def("reset_view", &BoundPlot::reset_view)
        .def_prop_rw("x_label", &BoundPlot::get_x_label, &BoundPlot::set_x_label)
        .def_prop_rw("y_label", &BoundPlot::get_y_label, &BoundPlot::set_y_label);
    nb::class_<PlotHandle>(m, "PlotHandle", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<>())
        .def("add_series", &PlotHandle::add_series, nb::arg("xs"), nb::arg("ys"), nb::arg("color")="", nb::arg("thickness")=2.f, nb::arg("fill")=false)
        .def("clear_series", &PlotHandle::clear_series)
        .def("notify", &PlotHandle::notify)
        .def("reset_view", &PlotHandle::reset_view)
        .def_prop_rw("x_label", &PlotHandle::get_x_label, &PlotHandle::set_x_label)
        .def_prop_rw("y_label", &PlotHandle::get_y_label, &PlotHandle::set_y_label);
    nb::class_<BoundTree>(m, "BoundTree", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def("refresh", &BoundTree::refresh)
        .def("rows", &BoundTree::rows);
    nb::class_<TreeHandle>(m, "TreeHandle", nb::dynamic_attr(), nb::is_weak_referenceable())
        .def(nb::init<nb::object>(), nb::arg("source"))
        .def("refresh", [](TreeHandle& h){ auto* p=h.ctrl.get(); list_op_dispatch([p](){ p->refresh(); }); })
        .def("rows", [](TreeHandle& h){
            auto* p = h.ctrl.get();
            return dispatch_sync_read<nb::list>([p](){
                nb::gil_scoped_acquire g;
                nb::list out;
                for(size_t i=0;i<p->rows.size();++i){
                    auto r=p->rows[i];
                    nb::dict d;
                    d["label"]=r.label; d["depth"]=r.depth; d["has_children"]=r.has_children; d["expanded"]=r.expanded; d["selected"]=r.selected;
                    out.append(d);
                }
                return out;
            });
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

    nb::class_<PyModel>(m, "Model")
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
        .def("_add_derived_int_internal", [](PyModel& self, nb::object fn, nb::args deps){
                std::vector<nb::object> v; v.reserve(deps.size());
                for (size_t i=0;i<deps.size();++i) v.push_back(nb::cast<nb::object>(deps[i]));
                auto [owner, p] = self.add_derived_slot_vec<int>(fn, v);
                BoundDerived<int> h; h.owner = std::move(owner); h.derived = p; return h;
            })
        .def("_add_derived_float_internal", [](PyModel& self, nb::object fn, nb::args deps){
                std::vector<nb::object> v; v.reserve(deps.size());
                for (size_t i=0;i<deps.size();++i) v.push_back(nb::cast<nb::object>(deps[i]));
                auto [owner, p] = self.add_derived_slot_vec<double>(fn, v);
                BoundDerived<double> h; h.owner = std::move(owner); h.derived = p; return h;
            })
        .def("_add_derived_str_internal", [](PyModel& self, nb::object fn, nb::args deps){
                std::vector<nb::object> v; v.reserve(deps.size());
                for (size_t i=0;i<deps.size();++i) v.push_back(nb::cast<nb::object>(deps[i]));
                auto [owner, p] = self.add_derived_slot_vec<std::string>(fn, v);
                BoundDerived<std::string> h; h.owner = std::move(owner); h.derived = p; return h;
            })
        .def("_add_derived_bool_internal", [](PyModel& self, nb::object fn, nb::args deps){
                std::vector<nb::object> v; v.reserve(deps.size());
                for (size_t i=0;i<deps.size();++i) v.push_back(nb::cast<nb::object>(deps[i]));
                auto [owner, p] = self.add_derived_slot_vec<bool>(fn, v);
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

    // Headless backend for pytest — stays alive for delay_ms then fires WindowClose.
    struct DelayHeadlessBackend final : BackendBase {
        HeadlessWindow window_{0, {}};
        int delay_ms_ = 100;
        explicit DelayHeadlessBackend(int d) : delay_ms_(d) {}
        Window& create_window(WindowConfig cfg) override { window_ = HeadlessWindow{1, cfg}; return window_; }
        void run(std::function<void(const WindowEvent&)> cb) override {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
            cb(WindowEvent{window_.id(), WindowClose{}});
        }
        void submit(WindowId, std::shared_ptr<const SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    m.def("_is_running", [](){ return g_has_handle.load(std::memory_order_acquire) || g_run_guard.load(std::memory_order_acquire); });
    m.def("_run_headless", [](PyModel& model, int delay_ms){
        {
            bool expected = false;
            if (!g_run_guard.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                throw std::runtime_error("prism.run already running");
            g_app_closed.store(false, std::memory_order_release);
        }
        nb::gil_scoped_release release;
        auto backend = Backend{std::make_unique<DelayHeadlessBackend>(delay_ms)};
        auto& window = backend.create_window({});
        auto setup = [](AppContext& ctx){
            // Install GIL wrapper for logic-thread drains (mouse/tick) — SDL-free core hook
            ctx.set_logic_wrapper([](std::function<void()> fn){
                if (Py_IsInitialized()) { nb::gil_scoped_acquire g; fn(); } else fn();
            });
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle = ctx.post_handle();
            g_has_handle.store(true, std::memory_order_release);
            g_app_closed.store(false, std::memory_order_release);
        };
        model_app(backend, window, model, setup);
        {
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle.reset();
            g_app_closed.store(true, std::memory_order_release);
        }
        g_has_handle.store(false, std::memory_order_release);
        g_run_guard.store(false, std::memory_order_release);
        g_app_closed.store(false, std::memory_order_release);
    }, nb::arg("model"), nb::arg("delay_ms")=100);

    m.def("run", [](PyModel& model, std::string title){
        {
            bool expected = false;
            if (!g_run_guard.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                throw std::runtime_error("prism.run already running");
            g_app_closed.store(false, std::memory_order_release);
        }
        // Must be called from main thread on macOS
        nb::gil_scoped_release release;
        auto backend = Backend::software(RenderConfig{});
        WindowConfig cfg;
        cfg.title = title.c_str();
        auto& window = backend.create_window(cfg);
        auto setup = [](AppContext& ctx){
            ctx.set_logic_wrapper([](std::function<void()> fn){
                if (Py_IsInitialized()) { nb::gil_scoped_acquire g; fn(); } else fn();
            });
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle = ctx.post_handle();
            g_has_handle.store(true, std::memory_order_release);
            g_app_closed.store(false, std::memory_order_release);
        };
        model_app(backend, window, model, setup);
        {
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle.reset();
            g_app_closed.store(true, std::memory_order_release);
        }
        g_has_handle.store(false, std::memory_order_release);
        g_run_guard.store(false, std::memory_order_release);
        g_app_closed.store(false, std::memory_order_release);
    }, nb::arg("model"), nb::arg("title")="PRISM App");
}