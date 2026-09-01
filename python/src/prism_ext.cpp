#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/function.h>

#include <prism/core/field.hpp>
#include <prism/core/connection.hpp>
#include <prism/app/model_app.hpp>
#include <prism/app/backend.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <deque>

namespace nb = nanobind;
using namespace prism::core;
using namespace prism::app;

// Global PostHandle for off-thread posting (set during prism.run, cleared after).
// Holds weak_ptrs to the mutation queue so post after model_app returns is not UAF.
static std::mutex g_handle_mutex;
static std::optional<AppContext::PostHandle> g_post_handle;
static bool g_has_handle = false;

static bool try_post_via_handle(std::function<void()> fn) {
    if (prism::app::detail_is_logic_thread) return false;
    std::optional<AppContext::PostHandle> hopt;
    {
        std::lock_guard<std::mutex> lk(g_handle_mutex);
        if (!g_has_handle || !g_post_handle) return false;
        hopt = *g_post_handle;
    }
    auto& h = *hopt;
    auto q = h.queue.lock();
    if (!q) return false;
    if (auto c = h.closed.lock()) {
        if (c->load(std::memory_order_acquire)) return false;
    }
    auto sched_flag = h.scheduled.lock();
    if (!sched_flag) return false;
    auto tail = h.drain_publish.lock();
    q->push(std::move(fn));
    bool expected = false;
    if (sched_flag->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        auto qq = q;
        auto sf = sched_flag;
        auto tp = tail;
        auto sch = h.sched;
        exec::start_detached(stdexec::schedule(sch) | stdexec::then([qq, sf, tp, sch] {
            do {
                prism::app::detail_in_mutation_batch = true;
                while (auto f = qq->pop()) (*f)();
                prism::app::detail_in_mutation_batch = false;
                if (tp && *tp) (*tp)();
                sf->store(false, std::memory_order_release);
                if (qq->empty()) break;
                bool exp = false;
                if (!sf->compare_exchange_strong(exp, true, std::memory_order_acq_rel)) break;
            } while (true);
        }));
    }
    return true;
}

// Helper to post or direct-set a Field.
template <typename T>
void field_set_dispatch(Field<T>* field, T v) {
    if (!prism::app::detail_is_logic_thread) {
        T copy = v;
        bool posted = try_post_via_handle([field, copy = std::move(copy)]() mutable {
            field->set(std::move(copy));
        });
        if (posted) return;
    }
    field->set(std::move(v));
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

// Type-erased slot for PyModel view — defined before BoundField so BoundField can hold shared_ptr to it.
struct SlotBase { virtual ~SlotBase() = default; virtual void build(ViewBuilder& vb) = 0; };
template <typename T>
struct Slot : SlotBase {
    Field<T> field;
    explicit Slot(T v) : field(std::move(v)) {}
    void build(ViewBuilder& vb) override { vb.widget(field); }
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
    // Legacy raw-pointer accessors (kept for internal use if needed)
    Field<int>* add_int(int v) { return add_int_slot(v).second; }
    Field<double>* add_float(double v) { return add_float_slot(v).second; }
    Field<std::string>* add_str(std::string v) { return add_str_slot(std::move(v)).second; }
    Field<bool>* add_bool(bool v) { return add_bool_slot(v).second; }

    void view(ViewBuilder& vb) {
        // Custom Python view() deferred to P3 — auto-stack fields in declaration order.
        std::lock_guard<std::mutex> lk(slots_mutex);
        for (auto& s : slots) s->build(vb);
    }

    void drain() {
        // PyModel currently has no Shared/Channel to drain
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

    nb::class_<PyModel>(m, "Model")
        .def(nb::init<>())
        .def("add_int", [](PyModel& self, int v){
                auto [owner, p] = self.add_int_slot(v);
                BoundField<int> h; h.owner = std::move(owner); h.field = p; return h;
            }, nb::arg("value")=0)
        .def("add_float", [](PyModel& self, double v){
                auto [owner, p] = self.add_float_slot(v);
                BoundField<double> h; h.owner = std::move(owner); h.field = p; return h;
            }, nb::arg("value")=0.0)
        .def("add_str", [](PyModel& self, std::string v){
                auto [owner, p] = self.add_str_slot(std::move(v));
                BoundField<std::string> h; h.owner = std::move(owner); h.field = p; return h;
            }, nb::arg("value")="")
        .def("add_bool", [](PyModel& self, bool v){
                auto [owner, p] = self.add_bool_slot(v);
                BoundField<bool> h; h.owner = std::move(owner); h.field = p; return h;
            }, nb::arg("value")=false)
        // Internal allocators — same ownership via shared_ptr<SlotBase>, no keep_alive cycle.
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
            }, nb::arg("value")=false);

    m.def("run", [](PyModel& model, std::string title){
        // Must be called from main thread on macOS
        nb::gil_scoped_release release;
        auto backend = Backend::software(RenderConfig{});
        WindowConfig cfg;
        cfg.title = title.c_str();
        auto& window = backend.create_window(cfg);
        auto setup = [](AppContext& ctx){
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle = ctx.post_handle();
            g_has_handle = true;
        };
        model_app(backend, window, model, setup);
        {
            std::lock_guard<std::mutex> lk(g_handle_mutex);
            g_post_handle.reset();
            g_has_handle = false;
        }
    }, nb::arg("model"), nb::arg("title")="PRISM App");
}
