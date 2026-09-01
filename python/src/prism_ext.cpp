#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/function.h>

#include <prism/core/field.hpp>
#include <prism/core/shared.hpp>
#include <prism/core/channel.hpp>
#include <prism/core/connection.hpp>
#include <prism/core/transaction.hpp>
#include <prism/app/model_app.hpp>
#include <prism/app/backend.hpp>

#include <prism/app/headless_window.hpp>
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
static std::atomic<bool> g_app_closed{false};

enum class PostResult { Posted, NoApp, Closed };

static void drain_queue_loop(const std::shared_ptr<mpsc_queue<std::function<void()>>>& q,
                             const std::shared_ptr<std::atomic<bool>>& sf,
                             const std::shared_ptr<std::function<void()>>& tp) {
    do {
        prism::app::detail_in_mutation_batch = true;
        while (auto f = q->pop()) (*f)();
        prism::app::detail_in_mutation_batch = false;
        if (tp && *tp) (*tp)();
        sf->store(false, std::memory_order_release);
        if (q->empty()) break;
        bool exp = false;
        if (!sf->compare_exchange_strong(exp, true, std::memory_order_acq_rel)) break;
    } while (true);
}

static PostResult try_post_via_handle_impl(std::function<void()> fn, bool allow_logic_thread) {
    if (!allow_logic_thread && prism::app::detail_is_logic_thread) return PostResult::NoApp;
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
    T get() const { return field.get(); }
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

// Bound handle — references Field owned by PyModel via shared_ptr<SlotBase> (no Model cycle).
// Holding the Slot directly keeps the Field/SenderHub alive even after the Model is GC'd.
template <typename T>
struct BoundField {
    std::shared_ptr<SlotBase> owner;
    Field<T>* field = nullptr;
    T get() const { return field ? field->get() : T{}; }
    void set(T v) {
        if (field) field_set_dispatch(field, std::move(v));
    }
    Connection observe(nb::callable cb) {
        if (!field) return {};
        Field<T>* f = field;
        auto wrapper = [cb, f](const T& val) {
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire acq;
            try { cb(val); } catch (nb::python_error&) { PyErr_Print(); } catch (...) {}
        };
        return f->on_change().connect(std::move(wrapper));
    }
};

// Standalone / bound handles for Shared<T> and Channel<T>
template <typename T>
struct SharedHandle {
    Shared<T> shared;
    SharedHandle(T init) : shared(std::move(init)) {}
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
        Shared<T>* s = shared;
        auto wrapper = [cb, s](const T& val) {
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire acq;
            try { cb(val); } catch (nb::python_error&) { PyErr_Print(); } catch (...) {}
        };
        return s->on_change().connect(std::move(wrapper));
    }
};
template <typename T>
struct ChannelHandle {
    Channel<T> channel;
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
        Channel<T>* c = channel;
        auto wrapper = [cb, c](const T& val) {
            if (!Py_IsInitialized()) return;
            nb::gil_scoped_acquire acq;
            try { cb(val); } catch (nb::python_error&) { PyErr_Print(); } catch (...) {}
        };
        return c->on_receive().connect(std::move(wrapper));
    }
};

inline void py_widget_dispatch(ViewBuilder& vb, nb::object h) {
    if (nb::isinstance<BoundField<int>>(h)) vb.widget(*nb::cast<BoundField<int>&>(h).field);
    else if (nb::isinstance<BoundField<double>>(h)) vb.widget(*nb::cast<BoundField<double>&>(h).field);
    else if (nb::isinstance<BoundField<std::string>>(h)) vb.widget(*nb::cast<BoundField<std::string>&>(h).field);
    else if (nb::isinstance<BoundField<bool>>(h)) vb.widget(*nb::cast<BoundField<bool>&>(h).field);
    else if (nb::isinstance<BoundShared<int>>(h)) vb.widget(*nb::cast<BoundShared<int>&>(h).shared);
    else if (nb::isinstance<BoundShared<double>>(h)) vb.widget(*nb::cast<BoundShared<double>&>(h).shared);
    else if (nb::isinstance<BoundShared<std::string>>(h)) vb.widget(*nb::cast<BoundShared<std::string>&>(h).shared);
    else if (nb::isinstance<BoundShared<bool>>(h)) vb.widget(*nb::cast<BoundShared<bool>&>(h).shared);
    else throw std::runtime_error("ViewBuilder.widget: unsupported handle type");
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
        std::lock_guard<std::mutex> lk(slots_mutex);
        for (auto& s : slots) s->drain();
    }
};

NB_MODULE(_prism_ext, m) {
    m.def("is_logic_thread", [](){ return detail_is_logic_thread; });

    nb::class_<Connection>(m, "Connection")
        .def("disconnect", &Connection::disconnect)
        .def("__enter__", [](Connection& self){ return &self; })
        .def("__exit__", [](Connection& self, nb::object, nb::object, nb::object){ self.disconnect(); return false; });

    nb::class_<FieldHandle<int>>(m, "FieldInt")
        .def(nb::init<int>(), nb::arg("value") = 0)
        .def_prop_rw("value", &FieldHandle<int>::get, &FieldHandle<int>::set)
        .def("observe", &FieldHandle<int>::observe, nb::keep_alive<0, 1>(), nb::arg("callback"))
        .def("get", &FieldHandle<int>::get)
        .def("set", &FieldHandle<int>::set);
    nb::class_<FieldHandle<double>>(m, "FieldFloat")
        .def(nb::init<double>(), nb::arg("value") = 0.0)
        .def_prop_rw("value", &FieldHandle<double>::get, &FieldHandle<double>::set)
        .def("observe", &FieldHandle<double>::observe, nb::keep_alive<0, 1>())
        .def("get", &FieldHandle<double>::get)
        .def("set", &FieldHandle<double>::set);
    nb::class_<FieldHandle<std::string>>(m, "FieldStr")
        .def(nb::init<std::string>(), nb::arg("value") = "")
        .def_prop_rw("value", &FieldHandle<std::string>::get, &FieldHandle<std::string>::set)
        .def("observe", &FieldHandle<std::string>::observe, nb::keep_alive<0, 1>())
        .def("get", &FieldHandle<std::string>::get)
        .def("set", &FieldHandle<std::string>::set);
    nb::class_<FieldHandle<bool>>(m, "FieldBool")
        .def(nb::init<bool>(), nb::arg("value") = false)
        .def_prop_rw("value", &FieldHandle<bool>::get, &FieldHandle<bool>::set)
        .def("observe", &FieldHandle<bool>::observe, nb::keep_alive<0, 1>())
        .def("get", &FieldHandle<bool>::get)
        .def("set", &FieldHandle<bool>::set);

    nb::class_<BoundField<int>>(m, "BoundInt")
        .def_prop_rw("value", &BoundField<int>::get, &BoundField<int>::set)
        .def("observe", &BoundField<int>::observe, nb::keep_alive<0, 1>())
        .def("get", &BoundField<int>::get)
        .def("set", &BoundField<int>::set);
    nb::class_<BoundField<double>>(m, "BoundFloat")
        .def_prop_rw("value", &BoundField<double>::get, &BoundField<double>::set)
        .def("observe", &BoundField<double>::observe, nb::keep_alive<0, 1>())
        .def("get", &BoundField<double>::get)
        .def("set", &BoundField<double>::set);
    nb::class_<BoundField<std::string>>(m, "BoundStr")
        .def_prop_rw("value", &BoundField<std::string>::get, &BoundField<std::string>::set)
        .def("observe", &BoundField<std::string>::observe, nb::keep_alive<0, 1>())
        .def("get", &BoundField<std::string>::get)
        .def("set", &BoundField<std::string>::set);
    nb::class_<BoundField<bool>>(m, "BoundBool")
        .def_prop_rw("value", &BoundField<bool>::get, &BoundField<bool>::set)
        .def("observe", &BoundField<bool>::observe, nb::keep_alive<0, 1>())
        .def("get", &BoundField<bool>::get)
        .def("set", &BoundField<bool>::set);

    // Standalone Shared handles
    nb::class_<SharedHandle<int>>(m, "SharedInt")
        .def(nb::init<int>(), nb::arg("value") = 0)
        .def_prop_rw("value", &SharedHandle<int>::get, &SharedHandle<int>::set)
        .def("observe", &SharedHandle<int>::observe, nb::keep_alive<0, 1>(), nb::arg("callback"))
        .def("get", &SharedHandle<int>::get).def("set", &SharedHandle<int>::set);
    nb::class_<SharedHandle<double>>(m, "SharedFloat")
        .def(nb::init<double>(), nb::arg("value") = 0.0)
        .def_prop_rw("value", &SharedHandle<double>::get, &SharedHandle<double>::set)
        .def("observe", &SharedHandle<double>::observe, nb::keep_alive<0, 1>())
        .def("get", &SharedHandle<double>::get).def("set", &SharedHandle<double>::set);
    nb::class_<SharedHandle<std::string>>(m, "SharedStr")
        .def(nb::init<std::string>(), nb::arg("value") = "")
        .def_prop_rw("value", &SharedHandle<std::string>::get, &SharedHandle<std::string>::set)
        .def("observe", &SharedHandle<std::string>::observe, nb::keep_alive<0, 1>())
        .def("get", &SharedHandle<std::string>::get).def("set", &SharedHandle<std::string>::set);
    nb::class_<SharedHandle<bool>>(m, "SharedBool")
        .def(nb::init<bool>(), nb::arg("value") = false)
        .def_prop_rw("value", &SharedHandle<bool>::get, &SharedHandle<bool>::set)
        .def("observe", &SharedHandle<bool>::observe, nb::keep_alive<0, 1>())
        .def("get", &SharedHandle<bool>::get).def("set", &SharedHandle<bool>::set);

    nb::class_<BoundShared<int>>(m, "BoundSharedInt")
        .def_prop_rw("value", &BoundShared<int>::get, &BoundShared<int>::set)
        .def("observe", &BoundShared<int>::observe, nb::keep_alive<0, 1>())
        .def("get", &BoundShared<int>::get).def("set", &BoundShared<int>::set);
    nb::class_<BoundShared<double>>(m, "BoundSharedFloat")
        .def_prop_rw("value", &BoundShared<double>::get, &BoundShared<double>::set)
        .def("observe", &BoundShared<double>::observe, nb::keep_alive<0, 1>())
        .def("get", &BoundShared<double>::get).def("set", &BoundShared<double>::set);
    nb::class_<BoundShared<std::string>>(m, "BoundSharedStr")
        .def_prop_rw("value", &BoundShared<std::string>::get, &BoundShared<std::string>::set)
        .def("observe", &BoundShared<std::string>::observe, nb::keep_alive<0, 1>())
        .def("get", &BoundShared<std::string>::get).def("set", &BoundShared<std::string>::set);
    nb::class_<BoundShared<bool>>(m, "BoundSharedBool")
        .def_prop_rw("value", &BoundShared<bool>::get, &BoundShared<bool>::set)
        .def("observe", &BoundShared<bool>::observe, nb::keep_alive<0, 1>())
        .def("get", &BoundShared<bool>::get).def("set", &BoundShared<bool>::set);

    // Channel handles
    nb::class_<ChannelHandle<int>>(m, "ChannelInt")
        .def(nb::init<>()).def("send", &ChannelHandle<int>::send).def("observe", &ChannelHandle<int>::observe, nb::keep_alive<0, 1>());
    nb::class_<ChannelHandle<double>>(m, "ChannelFloat")
        .def(nb::init<>()).def("send", &ChannelHandle<double>::send).def("observe", &ChannelHandle<double>::observe, nb::keep_alive<0, 1>());
    nb::class_<ChannelHandle<std::string>>(m, "ChannelStr")
        .def(nb::init<>()).def("send", &ChannelHandle<std::string>::send).def("observe", &ChannelHandle<std::string>::observe, nb::keep_alive<0, 1>());
    nb::class_<ChannelHandle<bool>>(m, "ChannelBool")
        .def(nb::init<>()).def("send", &ChannelHandle<bool>::send).def("observe", &ChannelHandle<bool>::observe, nb::keep_alive<0, 1>());

    nb::class_<BoundChannel<int>>(m, "BoundChannelInt")
        .def("send", &BoundChannel<int>::send).def("observe", &BoundChannel<int>::observe, nb::keep_alive<0, 1>());
    nb::class_<BoundChannel<double>>(m, "BoundChannelFloat")
        .def("send", &BoundChannel<double>::send).def("observe", &BoundChannel<double>::observe, nb::keep_alive<0, 1>());
    nb::class_<BoundChannel<std::string>>(m, "BoundChannelStr")
        .def("send", &BoundChannel<std::string>::send).def("observe", &BoundChannel<std::string>::observe, nb::keep_alive<0, 1>());
    nb::class_<BoundChannel<bool>>(m, "BoundChannelBool")
        .def("send", &BoundChannel<bool>::send).def("observe", &BoundChannel<bool>::observe, nb::keep_alive<0, 1>());

    nb::class_<ViewBuilder>(m, "ViewBuilder")
        .def("widget", [](ViewBuilder& vb, nb::object h){ py_widget_dispatch(vb, h); }, nb::arg("handle"))
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
            });

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

    m.def("_is_running", [](){ return g_has_handle.load(std::memory_order_acquire); });
    m.def("_run_headless", [](PyModel& model, int delay_ms){
        {
            bool expected = false;
            if (!g_has_handle.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
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
            g_app_closed.store(false, std::memory_order_release);
        };
        model_app(backend, window, model, setup);
        {
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle.reset();
            g_app_closed.store(true, std::memory_order_release);
        }
        g_has_handle.store(false, std::memory_order_release);
    }, nb::arg("model"), nb::arg("delay_ms")=100);

    m.def("run", [](PyModel& model, std::string title){
        {
            bool expected = false;
            if (!g_has_handle.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
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
            g_app_closed.store(false, std::memory_order_release);
        };
        model_app(backend, window, model, setup);
        {
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle.reset();
            g_app_closed.store(true, std::memory_order_release);
        }
        g_has_handle.store(false, std::memory_order_release);
    }, nb::arg("model"), nb::arg("title")="PRISM App");
}
