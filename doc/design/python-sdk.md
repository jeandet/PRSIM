# Python SDK — GIL-free Design

> Status: **Design / Pre-implementation**. Captures findings from 2026-08-30 review of whether PRISM is ready for a GIL-free Python UI SDK and what glue is needed to bridge C++ compile-time to Python runtime.

## 1. TL;DR

* **Architecture is ready, binding layer is not — which is the good way round.** The hard problem (lock-free, `GIL`-free render isolation via immutable snapshot) is already correct (`doc/design/threading-model.md:5`, `include/prism/render/scene_snapshot.hpp:16`, `include/prism/core/atomic_cell.hpp:11`). The remaining work is a thin, type-erased runtime adapter — not a redesign.
* `Node` (`include/prism/ui/node.hpp:23`) is already the firewall between compile-time `C++` (`Widget<T>` concepts, `P2996` reflection walk) and a runtime tree. Python needs a `Tier0 dynamic` adapter beside the existing `Tier1 manual / Tier2 concept / Tier3 reflection` tiers for `Table`/`Tree` (`include/prism/ui/table.hpp:42`, `README.md:219`, `AGENTS.md:18`).
* `Pydantic BaseModel` is the `Python` analog of `Field<T>` reflection and the right selling point: `struct{Field<int> count{42}} → model_app()` in `README.md:68` becomes `class M(BaseModel): count:int = 42 → pydantic_app()`.
* Recommended build order: `PyField + ViewBuilder` explicit binding first (forcing function), then `PydanticMirror` auto-UI on top. Keep render thread `Py_BEGIN_ALLOW_THREADS`.

## 2. Are we ready for GIL-free? (`include/prism/core/*`, `include/prism/app/*`)

### What is already GIL-free ready

| Property | Where | Why it matters for `3.13t` |
|---|---|---|
| Decoupled app vs render: `WidgetTree` dirty→`build_snapshot()`→`atomic_cell<SceneSnapshot>` atomic `shared_ptr` swap → `SoftwareBackend::submit()` `memory_order_release` | `include/prism/app/widget_tree.hpp:350`, `include/prism/app/model_app.hpp:99`, `src/backends/software_backend.cpp:306` | Render never calls into app, never holds a lock. Render can stay `GIL`-free forever. |
| `mpsc_queue<T>` lock-free Michael-Scott, cache-line-padded | `include/prism/core/mpsc_queue.hpp:19` | Basis for lossless `Python → C++` event streams without `GIL` contention. |
| `Channel<T>` lossless ordered, `Shared<T>` coalescing latest-value | `include/prism/core/channel.hpp:14`, `include/prism/core/shared.hpp:13` (contrasts in `channel.hpp:11`) | Exact two semantics `free-threaded Python` needs: every event vs latest value. Both expose `drain_notifications()` for owner-thread coalescing. |
| `stdexec run_loop` scheduler; `Backend::run()` on real main thread (macOS `AppKit` requirement), `logic_thread` owns `run_loop` | `include/prism/app/model_app.hpp:143,188` | Matches `Python` main thread owns `SDL`; `AppContext::scheduler()` (`model_app.hpp:36`) is the `GIL`-aware hop. |
| Idle sleeps at OS level (`futex`/`SDL_WaitEvent`), zero-CPU | `doc/design/threading-model.md:10` | No `GIL` polling. |
| `core/` is `SDL`-free; `BackendBase` vtable + `Headless`/`Null`/`Test`/`Capturing` backends | `include/prism/app/backend.hpp:19`, `include/prism/backends/software_backend.hpp:27`, `include/prism/app/headless_window.hpp` etc. | Test `Python` bindings headlessly. |
| Dirty-repaint persistent `WidgetTree`, `cached_snapshot_draws` | `include/prism/ui/widget_node.hpp:67`, `include/prism/app/widget_tree_layout.hpp` | `Python` not paying per-frame DOM rebuild. |

### What blocks a shippable SDK today

1. **No binding layer.** No `subprojects/nanobind.wrap`, no `python/` dir, `meson.build:8` is `cpp_std=c++26` only. Roadmap `README.md:519` Phase 5 lists `Python bindings` as future.
2. **`Field<T>::set()` is owner-thread-only.** (`include/prism/core/field.hpp:21` direct `emit`, `vector<Connection>`, no atomics.) `Field`/`State`/`Derived` are not thread-safe; only `Shared`/`Channel` are. Free-threaded `Python` threads cannot `field.set()` directly — must `Channel.send()` / `Shared.set()` + owner `drain_notifications()` or `schedule(sched)|then()` (`include/prism/core/exec.hpp`, `model_app.hpp:120`). Binding must enforce and queue.
3. **Compile-time View.** `Widget<T>` is `concept` dispatch (`include/prism/ui/delegate.hpp:32,285`) + `P2996` walk (`include/prism/core/reflect.hpp:16`, `app/widget_tree.hpp:686`). `Python` needs a runtime registry. The `C++23 view(ViewBuilder&)` fallback (`widget_tree.hpp:647`) proves the runtime path exists; it just needs a `PyViewBuilder` binding.
4. **Ownership / GIL awareness.** `WidgetTree(Model&)` holds bare `Model&` (`app/widget_tree.hpp:44`), `SenderHub` holds `std::function` (`core/connection.hpp:46`) with no `GIL` management. `PyObject*` refcount + `observe()` holding a `Python` callable under `3.13t` deferred refcount / `PyMutex` is unhandled. Requires `nanobind` (free-threaded-aware) not `pybind11`, plus `shared_ptr<PyObject>` keepalive.
5. **Event-loop embedding.** `model_app()` blocks `logic_thread.join()`+`backend.run()` (`model_app.hpp:191,254`). `Python` needs a non-blocking `AppContext` + exposed `scheduler()` for `prism.then/on`.
6. **`transaction` is `thread_local`** (`core/transaction.hpp:21`). Cross-thread `Python` batching must go through `Channel` then `TransactionGuard` on the owner.

## 3. The Core Gap: Compile-time Graph → Runtime Graph

```
C++26:  struct M { Field<int> count; Field<Slider<>> vol; }  →  ^^M enumerators_of → Node{build_widget,on_change} → WidgetNode
C++23:  struct M { void view(ViewBuilder& vb){ vb.vstack(count,vol);} }              →  same Node
Python: dict / BaseModel / dataclass  →  ???  →  Node{build_widget,on_change}  →  same WidgetNode
                                        ^^^^ missing glue
```

Both `C++` paths converge at `Node` (`ui/node.hpp:23`):

```cpp
struct Node {
  WidgetId id; bool is_leaf; LayoutKind layout_kind; vector<Node> children;
  function<void(WidgetNode&)> build_widget;               // captures Widget<T>::record/handle_input
  function<Connection(function<void()>)> on_change;        // captures Field<T>::on_change()
  vector<function<Connection(function<void()>)>> dependencies; // canvas depends_on
  shared_ptr<TableState> table_state; // + vlist_* virtualised tiers
};
```

`ViewBuilder` (`app/view_builder.hpp:103,294,367,441`) is the imperative runtime builder that already constructs those `Node`s: `widget(Field<T>&)`, `list(List<T>&)`, `table(ColumnStorage)`, `tree(TreeController)`, `canvas(T&)`, `hstack/vstack`. For `Python` this is the `public` surface — the `Python` analog of the `C++23` path.

## 4. Glue Architecture — Three Layers

### Layer 1 — Runtime `Field`: `PyField`

Don't expose `Field<T>` directly. Mirror the `Shared`/`Channel` boundary object:

```cpp
// GIL-free boundary object, analogous to core/field.hpp:38 but thread-aware
struct PyField {
  PyObject* value; // protected by PyMutex / atomic, not GIL; incref/decref with 3.13t deferred refcount
  SenderHub<PyObject*> on_change;
  PyObject* get() const;
  void set(PyObject* v); // may be called from any Python thread: store + pending flag, no emit
  void drain_on_owner(); // called on logic_thread, does emit
};
```

Producers (`Python` threads) `Channel<PyObject*>::send()` or `PyField::set()` lock-free; consumer (`logic_thread`) `drain_notifications()` on each `tick` (`app/widget_tree.hpp:66`, `model_app.hpp:129` `drain_shared`). This is how `Inspector<Shared<T>>` already bridges threads (`widgets/inspector.hpp:27` `observe` + `Shared::drain`). `Derived<T>` diamond coalescing via `TransactionGuard` (`core/transaction.hpp:45`) still runs `thread_local` on the owner only.

### Layer 2 — Runtime `Widget` dispatch: `WidgetRegistry`

`Widget<T>` (`ui/delegate.hpp:291`) is concept-selected at compile time. `Python` must switch at runtime:

```cpp
enum class PyKind { Int, Float, Str, Bool, SliderDesc, ButtonDesc, TextFieldDesc, DropdownDesc, CanvasDesc };

Node node_py_field(PyField& f, PyKind kind, json meta) {
  Node n; n.is_leaf = true; n.id = next_id++;
  n.build_widget = [kind, &f, meta](WidgetNode& wn){
    wn.focus_policy = kind_policy(kind);
    wn.record = [kind, &f, meta](WidgetNode& node){
      // GIL-free: DrawList ops only; py_to_string re-acquires GIL briefly
      node.draws.clear();
      switch(kind){ case PyKind::Str: node.draws.text(py_to_string(f.get()),...); break; /*...*/ }
    };
    wn.wire = [kind, &f](WidgetNode& node){
      node.connections.push_back(node.on_input.connect([&f](const InputEvent& ev){
        // handle_input: GIL acquire → call Python callable → f.set()
      }));
    };
    wn.record(wn);
  };
  n.on_change = [&f](auto cb){ return f.on_change.connect([cb](PyObject*){ cb(); }); };
  return n;
}
```

Keep `DrawList` allocation (`render/draw_list.hpp:81`) on `logic_thread` with `GIL` released; only `py_to_string` / validation re-acquires.

### Layer 3 — Runtime `View`: `PyViewBuilder`

Expose `ViewBuilder` to `Python`. First iteration is **explicit** — mirrors the `C++23` manual path and proves `GIL-free` isolation before any auto-magic:

```python
import prism

count = prism.Field(42)               # PyField
vol   = prism.SliderField(0.75, 0.0, 1.0)

def view(vb):
    vb.vstack(lambda: [vb.widget(count), vb.widget(vol)])
    vb.table(readings, headers=["Sensor","Value"])

prism.model_app("Demo", view)  # holds py::function alive; Node captures keep PyObject* inc-ref'd
```

`nanobind` wrapper forwards `widget(PyField*) → node_py_field`, `hstack(fn)` holds `py::function`, `table(ColumnStorage)` reuses existing `wrap_column_storage` (`ui/table.hpp:42`) / `wrap_row_storage` / `wrap_soa_columns` adaptors. `WidgetNode::wire/record` are already `std::function` (`ui/widget_node.hpp:56`) so `Python` lambdas can capture `PyObject*`.

## 5. Pydantic — The Selling Point

`README.md:68` demo (`Counter{Field<int>}` → auto UI) and `FieldMirror<T>` (`widgets/field_mirror.hpp:94` `tuple<LeafSlot<T>>` + `sync_from`/`build`/`for_each_leaf`/`view`) are exactly what `Pydantic` does at runtime: walk `model_fields` + `Annotated` metadata, synthesize a form, `sync_from`/`build` with validation.

**Pitch:** `Pydantic` familiar to every `Python` dev, typed, validated, `JSON Schema` already emitted. PRISM gives it a `60fps`, `GIL`-free, dirty-repainted renderer no `Streamlit`/`NiceGUI` can match.

```python
from pydantic import BaseModel, Field
from typing import Annotated
import prism

class Mixer(BaseModel):
    volume: Annotated[float, prism.Slider(min=0, max=1)] = 0.75
    mute: bool = False
    name: str = Field(default="chan1", max_length=32, description="Channel name")
    # Literal["a","b"] → Dropdown, ge/le → Slider bounds, description → label

prism.pydantic_app("Mixer", Mixer())  # FieldMirror<BaseModel> at runtime
```

Mapping:

| Pydantic | Sentinel / Annotation (`core/reflect_annotations.hpp`) |
|---|---|
| `Annotated[float, prism.Slider(...)]` | `Slider<T>` (`ui/delegate.hpp:469`) |
| `str Field(description=…)` | `label_t` / `section_t` (`field_mirror.hpp:103,125`) |
| `bool` | `Checkbox` / `Widget<bool>` (`delegate.hpp:418`) |
| `Literal` / `Enum` | `Dropdown` / `Widget<ScopedEnum>` (`delegate.hpp:648,646`) |
| `constr(max_length=…)` | `TextField` (`delegate.hpp:62`) |
| `Field(ge/le)` | `Slider` `min`/`max` |

### `PydanticMirror` — runtime `FieldMirror`

```
BaseModel mutated (any thread) ──Channel<dict>/Shared<dict>──▶ logic_thread drain
  ── PydanticMirror.sync_from(model.model_dump()) ──▶ each PyField.value.set()
  ──▶ Node.on_change ──▶ WidgetTree dirty ──▶ snapshot ──▶ render (GIL-free)

UI handle_input (logic_thread) ──▶ PyField.set() ──▶ PydanticMirror.build()
  ──▶ model_copy(update=…) + model_validate() ──▶ ValidationError ? mark WidgetNode dirty with error DrawList : Channel.send(new_model)
```

Reuse `Inspector`'s `SyncGuard` (`widgets/inspector.hpp:55`, `field_mirror.hpp:144`) — multi-field `sync_from` would otherwise `push_local` per leaf with a torn value. Reuse `TransactionGuard` (`core/transaction.hpp:45`) to coalesce the fan-out. Don't reuse `pydantic.Field` name (collides with `prism.Field`); prefer `Annotated` or `prism.ui(...)` metadata (`field_mirror.hpp:76` `has_annotation<skip>` pattern).

`Python` `dict.__setitem__` isn't observable — force `proxy = prism.proxify({"count":42})` that intercepts `__setitem__`/`__setattr__` or require `py_field.set(v)`. Same reason `FieldMirror` stores `LeafSlot<M> value` (`field_mirror.hpp:34`) not bare `M`.

`Pydantic v2` core is `Rust`/`maturin`; verify `free-threaded 3.13t` wheel before locking (`free-threaded` deferred refcount interacts with `PyField` inc/dec).

### Build order

1. **Tier0: `PyField` + `PyViewBuilder` explicit** — proves `GIL-free` isolation, exercises `Channel`/`Shared` contract (`core/channel.hpp:18` vs `core/field.hpp:21`), exercises reference `Python` multi-thread `model_system_monitor` forcing function (`AGENTS.md:31`, `examples/model_system_monitor/proc_metrics.hpp:313` `Shared`+`Channel` poll loops). No `Pydantic` dependency.
2. **Tier1: `PydanticMirror` / `pydantic_app()`** — thin adapter that walks `model_fields` and delegates to Tier0. No new `Widget` code.

## 6. Concrete Next Steps

1. `meson` `nanobind` wrap, `3.13t` CI, `Headless`/`Test` backend harness for `Python` tests (`app/test_backend.hpp`, `app/capturing_backend.hpp`).
2. `python/prism/` module: `Field`, `SliderField`, `Channel`, `Shared`, `List` (`core/list.hpp:11`), `ViewBuilder` bindings; `GIL`-release in `record()`/`SoftwareBackend::submit()`, `GIL`-acquire only in `wire` callbacks.
3. `PydanticMirror` in `python/` that reuses `FieldMirror` shape (`widgets/field_mirror.hpp:170` `view()` loop over `slots`).
4. `examples/python_pydantic_demo/` as the composite multi-thread reference app (extend `model_system_monitor`, don't add a single-widget demo — `AGENTS.md:31`).

## 7. References

* Threading: `doc/design/threading-model.md`, `include/prism/app/model_app.hpp:120,143`, `src/backends/software_backend.cpp:96,311`
* Snapshot / dirty repaint: `include/prism/render/scene_snapshot.hpp:16`, `include/prism/ui/widget_node.hpp:67`, `include/prism/app/widget_tree_layout.hpp`
* Core reactivity: `include/prism/core/field.hpp:21`, `include/prism/core/shared.hpp:13`, `include/prism/core/channel.hpp:14`, `include/prism/core/transaction.hpp:21,45`, `include/prism/core/connection.hpp:42`
* Node / ViewBuilder runtime seam: `include/prism/ui/node.hpp:23`, `include/prism/app/view_builder.hpp:103`, `include/prism/app/widget_tree.hpp:44,629`, `doc/design/dynamic-node-tree.md`
* Widget dispatch: `include/prism/ui/delegate.hpp:32,285,311,469,648`
* Existing adapter precedent: `include/prism/ui/table.hpp:42`, `include/prism/widgets/field_mirror.hpp:94`, `include/prism/widgets/inspector.hpp:27`, `include/prism/core/traits.hpp:79`
* Roadmap: `README.md:68,445,519`, `doc/design/README.md`

## 8. Open Questions

* `Field ge/le → Slider` vs `TextField` heuristic — infer or require `Annotated`?
* `Pydantic` validation error presentation — per-field `DrawList` error or `overlay` (`render/scene_snapshot.hpp:24`)?
* `List[BaseModel]` → `Table` `RowStorage` (`ui/table.hpp:89` `wrap_row_storage`) vs `SoA` (`ui/table.hpp:185` `wrap_soa_columns`) auto-selection for `Python` lists.
* `nanobind` `free-threaded` `PyObject` lifetime in `WidgetNode::edit_state` (`std::any` `ui/widget_node.hpp:37`) — store `py::object` with `PyMutex` or `shared_ptr<void>` holder?
