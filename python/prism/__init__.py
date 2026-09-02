from collections.abc import Callable as _Callable
from typing import Annotated, Protocol, get_args, get_origin, runtime_checkable

from ._prism_ext import (
    Model as _ModelBase,
    ViewBuilder,
    FieldInt,
    FieldFloat,
    FieldStr,
    FieldBool,
    BoundInt,
    BoundFloat,
    BoundStr,
    BoundBool,
    SharedInt,
    SharedFloat,
    SharedStr,
    SharedBool,
    BoundSharedInt,
    BoundSharedFloat,
    BoundSharedStr,
    BoundSharedBool,
    ChannelInt,
    ChannelFloat,
    ChannelStr,
    ChannelBool,
    BoundChannelInt,
    BoundChannelFloat,
    BoundChannelStr,
    BoundChannelBool,
    BoundDerivedInt,
    BoundDerivedFloat,
    BoundDerivedStr,
    BoundDerivedBool,
    BoundListInt,
    BoundListFloat,
    BoundListStr,
    ListInt,
    ListFloat,
    ListStr,
    BoundPlot,
    PlotHandle,
    BoundTree,
    TreeHandle,
    Connection,
    is_logic_thread,
    run as _run,
    _txn_begin,
    _txn_commit,
    _txn_abort,
    _run_headless as _run_headless_impl,
    _is_running,
)

TreeNodeId = int


@runtime_checkable
class TreeSource(Protocol):
    """Pythonic structural type for ``prism.tree_field(source)``.

    C++ calls these with the GIL held (see ``python/src/prism_ext.cpp:365``
    ``PythonTreeSource``). All six methods below are needed for a useful
    tree, but C++ will not raise if any are missing — each is
    ``hasattr``-gated on the C++ side with a silent fallback
    (``root_count`` → 0, ``label`` → ``str(id)``, ``has_children`` →
    false, etc.; see ``prism_ext.cpp:373-414``). A source missing
    ``child_at`` etc. will simply render as empty/leaf rather than error,
    so consider these *behaviourally* required and ``attributes``/``icon``
    the truly optional ones.

    Using a ``Protocol`` (not an ABC) keeps it duck-typed and avoids
    inheritance requirements, while giving static checkers / IDEs a real
    type to check against. Example::

        class MySource(TreeSource):  # or just duck-type without inheriting
            def root_count(self) -> int: ...
            def root_at(self, i: int) -> TreeNodeId: ...
            def child_count(self, nid: TreeNodeId) -> int: ...
            def child_at(self, nid: TreeNodeId, i: int) -> TreeNodeId: ...
            def label(self, nid: TreeNodeId) -> str: ...
            def has_children(self, nid: TreeNodeId) -> bool: ...
            # optional:
            def attributes(self, nid: TreeNodeId) -> dict[str, str]: ...
            def icon(self, nid: TreeNodeId) -> str | None: ...
    """

    def root_count(self) -> int: ...
    def root_at(self, i: int) -> TreeNodeId: ...
    def child_count(self, nid: TreeNodeId) -> int: ...
    def child_at(self, nid: TreeNodeId, i: int) -> TreeNodeId: ...
    def label(self, nid: TreeNodeId) -> str: ...
    def has_children(self, nid: TreeNodeId) -> bool: ...


@runtime_checkable
class TableSource(Protocol):
    """Pythonic structural type for future ``prism.table_field(source)``.

    The typed form mirrors the C++ ``TableSource`` struct
    (``include/prism/ui/table.hpp:27``) — the runtime container whose
    ``header`` member is optional (consumer checks
    ``source.header ? source.header(col) : \"\"``, see ``table.hpp:272``).
    The ``ColumnStorage`` *concept* at ``table.hpp:35`` conversely requires
    ``header()`` unconditionally at compile time; the distinction matters
    only once a Python binding exists.

    Not yet wired to a Python binding — added now so table adapters can be
    written against a typed protocol and work unchanged once
    ``table_field`` lands. The three methods below will be required;
    ``header`` will be optional (a future ``PythonTableSource`` binding is
    intended to mirror ``PythonTreeSource`` via ``hasattr`` and fall back
    to ``\"\"`` when missing)::

        class MyTable(TableSource):
            def column_count(self) -> int: ...
            def row_count(self) -> int: ...
            def cell_text(self, row: int, col: int) -> str: ...
            def header(self, col: int) -> str: ...  # optional
    """

    def column_count(self) -> int: ...
    def row_count(self) -> int: ...
    def cell_text(self, row: int, col: int) -> str: ...


__all__ = [
    "Model",
    "field",
    "slider",
    "checkbox",
    "shared",
    "channel",
    "derived",
    "transaction",
    "list_field",
    "plot_field",
    "tree_field",
    "TreeSource",
    "TreeNodeId",
    "TableSource",
    "is_tree_source",
    "is_table_source",
    "validator_for",
    "FieldInt",
    "FieldFloat",
    "FieldStr",
    "FieldBool",
    "SharedInt",
    "SharedFloat",
    "SharedStr",
    "SharedBool",
    "ChannelInt",
    "ChannelFloat",
    "ChannelStr",
    "ChannelBool",
    "BoundDerivedInt",
    "BoundDerivedFloat",
    "BoundDerivedStr",
    "BoundDerivedBool",
    "BoundListInt",
    "BoundListFloat",
    "BoundListStr",
    "ListInt",
    "ListFloat",
    "ListStr",
    "BoundPlot",
    "PlotHandle",
    "BoundTree",
    "TreeHandle",
    "Connection",
    "is_logic_thread",
    "run",
]


import weakref as _wr_mod
import atexit as _atexit_mod

# Fire-and-forget keepalive: Python-side storage replaces nanobind keep_alive<1,0>
# which created a non-GC immortal cycle (handle<->Connection via two keep_alive records).
# Handles are now nanobind objects with dynamic_attr + weakref (see prism_ext.cpp),
# so we store keepalive per-handle via __dict__ (_prism_keepalive) — no global id map,
# no id-reuse bug, and the Model->handle->Connection->cb->Model cycle becomes GC-collectable.
# Fallback global id map remains for any handle that somehow lacks __dict__ (defensive).
_keepalive_by_handle: dict[int, list] = {}
_observed_handles: _wr_mod.WeakSet = _wr_mod.WeakSet()
_all_models: _wr_mod.WeakSet = _wr_mod.WeakSet()


def _patch_bound_observe():
    # Bound handles (Model-owned) + standalone handles (FieldInt etc.)
    bound_observe = [
        (BoundInt, "observe"),
        (BoundFloat, "observe"),
        (BoundStr, "observe"),
        (BoundBool, "observe"),
        (BoundSharedInt, "observe"),
        (BoundSharedFloat, "observe"),
        (BoundSharedStr, "observe"),
        (BoundSharedBool, "observe"),
        (BoundChannelInt, "observe"),
        (BoundChannelFloat, "observe"),
        (BoundChannelStr, "observe"),
        (BoundChannelBool, "observe"),
        (BoundDerivedInt, "observe"),
        (BoundDerivedFloat, "observe"),
        (BoundDerivedStr, "observe"),
        (BoundDerivedBool, "observe"),
        (BoundListInt, "observe_insert"),
        (BoundListInt, "observe_remove"),
        (BoundListInt, "observe_update"),
        (BoundListFloat, "observe_insert"),
        (BoundListFloat, "observe_remove"),
        (BoundListFloat, "observe_update"),
        (BoundListStr, "observe_insert"),
        (BoundListStr, "observe_remove"),
        (BoundListStr, "observe_update"),
        # standalone fields (not Model-owned) — same fire-and-forget semantics
        (FieldInt, "observe"),
        (FieldFloat, "observe"),
        (FieldStr, "observe"),
        (FieldBool, "observe"),
        (SharedInt, "observe"),
        (SharedFloat, "observe"),
        (SharedStr, "observe"),
        (SharedBool, "observe"),
        (ChannelInt, "observe"),
        (ChannelFloat, "observe"),
        (ChannelStr, "observe"),
        (ChannelBool, "observe"),
        (ListInt, "observe_insert"),
        (ListInt, "observe_remove"),
        (ListInt, "observe_update"),
        (ListFloat, "observe_insert"),
        (ListFloat, "observe_remove"),
        (ListFloat, "observe_update"),
        (ListStr, "observe_insert"),
        (ListStr, "observe_remove"),
        (ListStr, "observe_update"),
    ]
    for cls, meth in bound_observe:
        try:
            orig = getattr(cls, meth)
        except AttributeError:
            continue

        # capture orig in default arg to avoid late-binding
        def _wrap(self, *args, _orig=orig, **kwargs):  # type: ignore[no-untyped-def]
            conn = _orig(self, *args, **kwargs)
            # per-handle storage via __dict__ (handles now have dynamic_attr + weakref);
            # falls back to global id map only if handle lacks __dict__ (defensive).
            try:
                d = self.__dict__  # type: ignore[attr-defined]
                lst = d.setdefault("_prism_keepalive", [])
                lst.append(conn)
                try:
                    _observed_handles.add(self)  # type: ignore[attr-defined]
                except TypeError:
                    # self doesn't support weak references (exotic subclass);
                    # atexit cleanup then relies on the id-keyed fallback map.
                    pass
            except (AttributeError, TypeError):
                lst = _keepalive_by_handle.setdefault(id(self), [])
                lst.append(conn)
            return conn

        setattr(cls, meth, _wrap)


_patch_bound_observe()
del _patch_bound_observe

# _all_models / _observed_handles / _keepalive_by_handle already defined at top
# (kept here for atexit ordering; no re-import needed)


def _clear_model_observers(model):
    # disconnect all Connections kept per-handle (and legacy global map) for this model's handles
    d = getattr(model, "__dict__", {})
    fields = d.get("_prism_fields", {})
    for h in list(fields.values()):
        # per-handle list
        lst = None
        try:
            lst = h.__dict__.get("_prism_keepalive")  # type: ignore[attr-defined]
        except (AttributeError, TypeError):
            lst = None
        if lst is None:
            try:
                lst = getattr(h, "_prism_keepalive", None)
            except Exception:
                lst = None
        if lst:
            for conn in list(lst):
                try:
                    conn.disconnect()
                except Exception:
                    pass
            try:
                lst.clear()
            except Exception:
                pass
        # legacy global fallback
        lst2 = _keepalive_by_handle.pop(id(h), None)
        if lst2:
            for conn in list(lst2):
                try:
                    conn.disconnect()
                except Exception:
                    pass
            lst2.clear()
    # also clear any _prism_keepalive directly on model (future)
    ml = getattr(model, "_prism_keepalive", None)
    if ml:
        for conn in list(ml):
            try:
                conn.disconnect()
            except Exception:
                pass
        try:
            ml.clear()
        except Exception:
            pass
    # break Model -> handle cycle so nanobind doesn't report Bound* as leaked
    # when Model is still in __main__ globals at shutdown (common for examples)
    try:
        fields.clear()
    except Exception:
        pass
    try:
        if "_prism_fields" in d:
            d.pop("_prism_fields", None)
    except Exception:
        pass


def _atexit_clear():
    _models_snapshot = list(_all_models)
    for m in _models_snapshot:
        try:
            _clear_model_observers(m)
        except Exception:
            pass
    # disconnect any remaining keepalive entries (standalone fields, leaked handles)
    # per-handle WeakSet first (covers most cases after dynamic_attr change)
    for h in list(_observed_handles):  # type: ignore[arg-type]
        lst = None
        try:
            lst = h.__dict__.get("_prism_keepalive")  # type: ignore[attr-defined]
        except (AttributeError, TypeError):
            try:
                lst = getattr(h, "_prism_keepalive", None)
            except Exception:
                lst = None
        if lst:
            for conn in list(lst):
                try:
                    conn.disconnect()
                except Exception:
                    pass
            try:
                lst.clear()
            except Exception:
                pass
    try:
        _observed_handles.clear()  # type: ignore[attr-defined]
    except Exception:
        pass
    # legacy global fallback
    for lst in list(_keepalive_by_handle.values()):
        for conn in list(lst):
            try:
                conn.disconnect()
            except Exception:
                pass
        lst.clear()
    _keepalive_by_handle.clear()
    try:
        _all_models.clear()
    except Exception:
        pass
    # A Model left in a module global at interpreter exit stays alive past
    # nanobind's leak check and may print a leak warning; that is acceptable.
    # Examples should use `def _main(): m = ...` (function scope) to avoid it.


_atexit_mod.register(_atexit_clear)


_KIND_NAME_BY_TYPE = {bool: "bool", int: "int", float: "float", str: "str"}


def _kind_of(value, who="field"):
    """Classify *value* into the scalar kind the C++ side stores it as.

    Accepts either a representative value (``0``, ``0.0``, ``""``, ``False``)
    or the type itself (``int``, ``float``, ``str``, ``bool``) — the latter
    is how ``derived(..., type_hint=float)`` names a kind without a sample
    value. bool is checked before int because bool is an int subclass in
    Python.
    """
    if isinstance(value, type) and value in _KIND_NAME_BY_TYPE:
        return _KIND_NAME_BY_TYPE[value]
    if isinstance(value, bool):
        return "bool"
    if isinstance(value, int):
        return "int"
    if isinstance(value, float):
        return "float"
    if isinstance(value, str):
        return "str"
    hint = "; use list_field() for lists" if isinstance(value, list) else ""
    raise TypeError(
        f"prism.{who}(): unsupported default type {type(value).__name__}{hint}"
    )


class _FieldDescriptor:
    def __init__(self, default, kind=None, meta=None, validator=None):
        self.default = default
        self.kind = kind
        self.meta = meta or {}
        self.validator = validator
        self.name = None

    def __set_name__(self, owner, name):
        self.name = name

    def _allocate(self, instance):
        cache = instance.__dict__.setdefault("_prism_fields", {})
        if self.name in cache:
            return cache[self.name]
        # allocate via Model's internal add_* (no keep_alive cycle — Model owns slots)
        kind = _kind_of(self.default)
        h = getattr(instance, f"_add_{kind}_internal")(self.default)
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)

    def _validate(self, value):
        if self.validator is not None:
            return self.validator(value)
        return value

    def __set__(self, instance, value):
        h = self._allocate(instance)
        h.value = self._validate(value)

    # non-string, type-safe observe: M.volume.observe(m, cb) instead of m.observe('volume', cb)
    def observe(self, instance, callback):
        return self._allocate(instance).observe(callback)

    def get(self, instance):
        return self._allocate(instance).value

    def set(self, instance, value):
        self.__set__(instance, value)


def field(default, validator=None):
    """Value may be set from any thread (posted to the logic thread).

    Plain scalar field descriptor: ``count = prism.field(0)``.
    """
    return _FieldDescriptor(default, validator=validator)


def slider(default, min=0.0, max=1.0, validator=None):
    """Value may be set from any thread (posted to the logic thread).

    Float field descriptor rendered as a slider widget.
    """
    return _FieldDescriptor(
        float(default),
        kind="slider",
        meta={"min": float(min), "max": float(max)},
        validator=validator,
    )


def checkbox(default, label=None, validator=None):
    """Value may be set from any thread (posted to the logic thread).

    Bool field descriptor rendered as a checkbox widget.
    """
    return _FieldDescriptor(
        bool(default),
        kind="checkbox",
        meta={"label": label} if label is not None else {},
        validator=validator,
    )


class _SharedDescriptor:
    def __init__(self, default, validator=None):
        self.default = default
        self.validator = validator
        self.name = None

    def __set_name__(self, owner, name):
        self.name = name

    def _allocate(self, instance):
        cache = instance.__dict__.setdefault("_prism_fields", {})
        if self.name in cache:
            return cache[self.name]
        kind = _kind_of(self.default, "shared")
        h = getattr(instance, f"_add_shared_{kind}_internal")(self.default)
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)

    def _validate(self, value):
        return self.validator(value) if self.validator is not None else value

    def __set__(self, instance, value):
        h = self._allocate(instance)
        h.value = self._validate(value)

    def observe(self, instance, callback):
        return self._allocate(instance).observe(callback)

    def get(self, instance):
        return self._allocate(instance).value

    def set(self, instance, value):
        self.__set__(instance, value)


class _ChannelDescriptor:
    def __init__(self, type_hint=0):
        # type_hint determines Channel<T>: int / float / str / bool
        self.type_hint = type_hint
        self.name = None

    def __set_name__(self, owner, name):
        self.name = name

    def _allocate(self, instance):
        cache = instance.__dict__.setdefault("_prism_fields", {})
        if self.name in cache:
            return cache[self.name]
        kind = _kind_of(self.type_hint, "channel")
        h = getattr(instance, f"_add_channel_{kind}_internal")()
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)

    def observe(self, instance, callback):
        return self._allocate(instance).observe(callback)


def shared(default, validator=None):
    """Value may be set from any thread; writes apply directly, not posted.

    Cross-thread latest-value slot. Standalone handles are drained on every app tick while an app runs.
    """
    return _SharedDescriptor(default, validator=validator)


def channel(type_hint=0):
    """May be sent from any thread; events queue and drain on the logic thread.

    Cross-thread FIFO event stream. Standalone handles are drained on every app tick while an app runs.
    """
    return _ChannelDescriptor(type_hint)


class _DerivedDescriptor:
    def __init__(self, fn, *deps, type_hint=None):
        self.fn = fn
        self.deps = deps  # attribute names (str) or handles resolved later
        self.type_hint = type_hint
        self.name = None

    def __set_name__(self, owner, name):
        self.name = name

    def _allocate(self, instance):
        cache = instance.__dict__.setdefault("_prism_fields", {})
        if self.name in cache:
            return cache[self.name]
        # resolve deps: str -> getattr(instance, name)
        dep_handles = [
            getattr(instance, d) if isinstance(d, str) else d for d in self.deps
        ]
        # build call_fn from fn's signature; also build a same-shape probe that
        # calls fn directly (no weakref indirection needed — instance is in scope)
        import inspect
        import weakref as _wr

        sig = inspect.signature(self.fn)
        n_params = len(sig.parameters)
        n_deps = len(dep_handles)

        if n_params == 1 and n_deps >= 1:
            # self-style: fn(instance) — break Model->slot->py_fn->instance cycle via weakref
            wref = _wr.ref(instance)
            orig = self.fn
            call_fn = lambda _w=wref, _f=orig: _f(_w()) if _w() is not None else None  # type: ignore[no-untyped-call]
            probe_fn = lambda: orig(instance)  # type: ignore[no-untyped-call]
        elif n_params == n_deps and n_deps > 0:
            # break cycle: capture weakref + dep names (strings) not handles
            wref2 = _wr.ref(instance)
            dep_names = tuple(d if isinstance(d, str) else None for d in self.deps)
            if all(n is not None for n in dep_names):
                orig2 = self.fn
                call_fn = (
                    lambda _w=wref2, _f=orig2, _ns=dep_names: _f(
                        *[getattr(_w(), n).value for n in _ns]
                    )
                    if _w() is not None
                    else None
                )  # type: ignore[no-untyped-call]
            else:
                # fallback: captures handles strongly (rare)
                call_fn = lambda: self.fn(*[h.value for h in dep_handles])  # type: ignore[no-untyped-call]
            probe_fn = lambda: self.fn(*[h.value for h in dep_handles])  # type: ignore[no-untyped-call]
        else:
            call_fn = self.fn
            probe_fn = self.fn

        if self.type_hint is not None:
            kind = _kind_of(self.type_hint, "derived")
        else:
            try:
                probe = probe_fn()
            except Exception as exc:
                raise TypeError(
                    f"derived '{self.name}': probe call raised {exc!r}; "
                    "pass type_hint=int|float|str|bool to skip probing"
                ) from exc
            kind = _kind_of(probe, "derived")

        h = getattr(instance, f"_add_derived_{kind}_internal")(call_fn, *dep_handles)  # type: ignore[attr-defined]
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)

    def observe(self, instance, callback):
        return self._allocate(instance).observe(callback)

    def get(self, instance):
        return self._allocate(instance).value


def derived(fn=None, *deps, type_hint=None):
    """Read-only; computed on the logic thread.

    Descriptor factory: @derived('a','b') or derived(lambda self: ..., 'a').

    Usage:
      class M(Model):
        a = field(1)
        b = field(2)
        total = derived(lambda self: self.a.value + self.b.value, 'a', 'b')
      # or
      class M(Model):
        a = field(1)
        total = derived(lambda self: self.a.value*2, 'a', type_hint=0)
    When used as @derived('a') decorator on method, method is compute.
    ``type_hint`` accepts either a sample value (``type_hint=0.0``) or a
    bare type (``type_hint=float``) — both select the same ``float`` kind.
    """
    if fn is not None and callable(fn) and not deps and type_hint is None:
        # called as @derived without args -> fn is the function, no deps yet (must be supplied elsewhere)
        # treat as decorator waiting for deps: return descriptor with fn and no deps
        return _DerivedDescriptor(fn)
    if callable(fn):
        return _DerivedDescriptor(fn, *deps, type_hint=type_hint)
    # called as derived('a','b') -> fn is first dep string, need decorator
    # so fn is actually dep name
    all_deps = (fn,) + deps if fn is not None else deps

    def decorator(func):
        return _DerivedDescriptor(func, *all_deps, type_hint=type_hint)

    return decorator


class _ListDescriptor:
    def __init__(self, default=None):
        self.default = list(default) if default is not None else []
        self.name = None

    def __set_name__(self, owner, name):
        self.name = name

    def _allocate(self, instance):
        cache = instance.__dict__.setdefault("_prism_fields", {})
        if self.name in cache:
            return cache[self.name]
        # infer type from first element
        vals = self.default
        if not vals:
            h = instance._add_list_str_internal([])  # default empty str list
        else:
            kind = _kind_of(vals[0], "list_field")
            if kind == "bool":
                # bool lists map to int list due to vector<bool> proxy constraints
                h = instance._add_list_int_internal([int(v) for v in vals])
            else:
                h = getattr(instance, f"_add_list_{kind}_internal")(vals)
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)

    def observe_insert(self, instance, callback):
        return self._allocate(instance).observe_insert(callback)

    def observe_remove(self, instance, callback):
        return self._allocate(instance).observe_remove(callback)

    def observe_update(self, instance, callback):
        return self._allocate(instance).observe_update(callback)

    # alias generic observe to update
    def observe(self, instance, callback):
        return self.observe_update(instance, callback)


def list_field(default=None):
    """Mutations may be called from any thread (dispatched to the logic thread)."""
    return _ListDescriptor(default)


class _PlotDescriptor:
    def __init__(self):
        self.name = None

    def __set_name__(self, owner, name):
        self.name = name

    def _allocate(self, instance):
        cache = instance.__dict__.setdefault("_prism_fields", {})
        if self.name in cache:
            return cache[self.name]
        h = instance._add_plot_internal()  # type: ignore[attr-defined]
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)


_TREE_METHODS = (
    "root_count",
    "root_at",
    "child_count",
    "child_at",
    "label",
    "has_children",
)


def _tree_missing_methods(obj: object) -> list[str]:
    return [m for m in _TREE_METHODS if not hasattr(obj, m)]


def is_tree_source(obj: object) -> bool:
    """Return ``True`` if *obj* conforms to :class:`TreeSource` (``isinstance`` check).

    Exists to make ``@runtime_checkable`` actually exercised — and as a
    public helper for user code / tests.
    """
    return isinstance(obj, TreeSource)  # type: ignore[arg-type]


def is_table_source(obj: object) -> bool:
    """Return ``True`` if *obj* conforms to :class:`TableSource`."""
    return isinstance(obj, TableSource)  # type: ignore[arg-type]


class _TreeDescriptor:
    def __init__(self, source=None):
        # source can be dict {id: {label, children:[...], attrs:{}}} or object with methods
        self.source = source
        self.name = None

    def __set_name__(self, owner, name):
        self.name = name

    def _allocate(self, instance):
        cache = instance.__dict__.setdefault("_prism_fields", {})
        if self.name in cache:
            return cache[self.name]
        # allow source to be callable returning dict/object, or direct
        src = (
            self.source()
            if callable(self.source) and not isinstance(self.source, dict)
            else self.source
        )
        if src is None:
            src = {}
        # Exercise @runtime_checkable: warn when a non-dict source looks
        # intentionally tree-like but is missing methods — catches typos
        # (e.g. ``childs_at``) that C++ would otherwise silently fallback to
        # empty/leaf (prism_ext.cpp:373-414).
        if not isinstance(src, dict):
            try:
                if not is_tree_source(src):
                    missing = _tree_missing_methods(src)
                    # only warn when partially implements (has some tree methods but not all)
                    has_some = len(missing) < len(_TREE_METHODS) and len(missing) > 0
                    has_any = any(hasattr(src, m) for m in _TREE_METHODS)
                    if has_some or (has_any and missing):
                        import warnings

                        warnings.warn(
                            f"tree source {type(src).__name__!r} missing TreeSource methods {missing}; "
                            "C++ will fallback to defaults (empty/leaf) — check for typos",
                            UserWarning,
                            stacklevel=4,
                        )
            except TypeError:
                # isinstance() against a runtime_checkable Protocol can raise
                # TypeError for objects with unusual attribute access; the
                # check is a best-effort diagnostic warning, not required
                # for correctness, so skip it rather than fail allocation.
                pass
        h = instance._add_tree_internal(src)  # type: ignore[attr-defined]
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)


def plot_field():
    """Mutations may be called from any thread (dispatched to the logic thread).

    Descriptor for PlotModel — use as `plot = prism.plot_field()` then `self.plot.add_series(xs, ys)`.
    """
    return _PlotDescriptor()


TreeSourceLike = TreeSource | dict  # type: ignore[valid-type]
TreeFieldSource = TreeSourceLike | _Callable[[], TreeSourceLike] | None  # type: ignore[valid-type]


def tree_field(source: TreeFieldSource = None):  # type: ignore[assignment]
    """Descriptor for TreeController — ``tree = prism.tree_field(source)``.

    ``source`` may be a ``TreeSource`` (``Protocol`` — duck-typed), a plain
    dict ``{id: {label, children:[...], attrs:{}}}``, a zero-arg callable
    returning either (``lambda: make_source()`` / factory), or ``None``
    (empty tree). The factory form is evaluated lazily at
    ``_TreeDescriptor._allocate`` (``__init__.py:804-808``). The
    ``TreeSource`` protocol lives in ``prism.TreeSource`` — inheriting is
    optional, structural matching is enough, but inheriting gives IDE
    support.
    """
    return _TreeDescriptor(source)  # type: ignore[arg-type]


def validator_for(type_hint):
    """Build a pydantic TypeAdapter validator for Annotated types.

    Example:
        from typing import Annotated
        from pydantic import Field as PydanticField
        Vol = Annotated[float, PydanticField(ge=0, le=1)]
        class M(prism.Model):
            volume = prism.field(0.5, validator=prism.validator_for(Vol))
    """
    try:
        from pydantic import TypeAdapter

        ta = TypeAdapter(type_hint)
        return ta.validate_python
    except Exception as e:  # pragma: no cover
        raise RuntimeError(f"validator_for requires pydantic: {e}") from e


class Model(_ModelBase):
    def observe(self, descriptor, callback):
        """May be called from any thread; the callback itself fires on the logic thread.

        Instance convenience for non-string observe: m.observe(M.volume, cb).
        """
        return descriptor.observe(self, callback)

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        ann = getattr(cls, "__annotations__", {})
        for name, hint in list(ann.items()):
            if get_origin(hint) is not Annotated:
                continue
            try:
                v = validator_for(hint)
            except RuntimeError:
                # validator_for raises RuntimeError when pydantic is missing
                # or hint isn't a valid pydantic type; treat as "no
                # auto-validator" for this annotation.
                v = None
            cur = cls.__dict__.get(name, None)
            if isinstance(cur, (_FieldDescriptor, _SharedDescriptor)):
                if cur.validator is None and v is not None:
                    cur.validator = v
            elif isinstance(cur, _ListDescriptor):
                continue
            else:
                # auto-create field from Annotated + plain default (transparent Annotated)
                # e.g. `count: Annotated[int, Field(ge=0)] = 0` without prism.field()
                default = cur
                base = get_args(hint)[0] if get_args(hint) else None
                # handle bare annotation without default
                if default is None and name not in cls.__dict__:
                    if base is int:
                        default = 0
                    elif base is float:
                        default = 0.0
                    elif base is str:
                        default = ""
                    elif base is bool:
                        default = False
                    elif get_origin(base) is not list:
                        raise TypeError(
                            f"{cls.__name__}.{name}: Annotated[{base!r}, ...] has "
                            "no recognized scalar default; give an explicit "
                            "default or use prism.field(...)"
                        )
                # detect list
                origin = get_origin(base) if base is not None else None
                if origin is list:
                    default = default if isinstance(default, list) else []
                    descr: _FieldDescriptor | _ListDescriptor = _ListDescriptor(default)
                    setattr(cls, name, descr)
                    descr.__set_name__(cls, name)
                else:
                    # scalar field – default must already be resolved above
                    if default is None:
                        raise TypeError(
                            f"{cls.__name__}.{name}: no explicit default for "
                            "Annotated field; give one (e.g. `= 0`) or use "
                            "prism.field(...)"
                        )
                    descr2 = _FieldDescriptor(default, validator=v)
                    setattr(cls, name, descr2)
                    descr2.__set_name__(cls, name)

    def __init__(self, **kwargs):
        super().__init__()
        # register for atexit cleanup before nanobind leak check
        try:
            _all_models.add(self)  # type: ignore[name-defined]
        except TypeError:
            # self doesn't support weak references (rare, exotic subclass);
            # atexit cleanup then simply won't cover this instance.
            pass
        # Allocate descriptors eagerly so view ordering matches class definition order
        # Eager allocation here is what makes the check-then-act in _allocate safe:
        # no other thread can see the instance before __init__ returns.
        for cls in reversed(type(self).__mro__):
            for name, attr in cls.__dict__.items():
                if isinstance(
                    attr,
                    (
                        _FieldDescriptor,
                        _SharedDescriptor,
                        _ChannelDescriptor,
                        _DerivedDescriptor,
                        _ListDescriptor,
                        _PlotDescriptor,
                        _TreeDescriptor,
                    ),
                ):
                    attr._allocate(self)
        # Apply kwargs overrides
        for k, v in kwargs.items():
            setattr(self, k, v)
        # If subclass overrides view(), register trampoline. Use weakref to break
        # Model -> _c_model -> py_view_cb -> bound method -> Model cycle (would leak).
        for cls in type(self).__mro__:
            if cls in (Model, _ModelBase, object):
                continue
            if "view" in cls.__dict__:
                import weakref

                fn = cls.__dict__["view"]
                wr = weakref.ref(self)

                def _tramp(vb, _wr=wr, _fn=fn):  # type: ignore[no-untyped-def]
                    inst = _wr()
                    if inst is None:
                        return
                    return _fn(inst, vb)

                self._set_view_callback(_tramp)  # type: ignore[attr-defined]
                break


class _TransactionCtx:
    def __enter__(self):
        _txn_begin()
        return self

    def __exit__(self, exc_type, exc, tb):
        if exc_type is None:
            _txn_commit()
        else:
            _txn_abort()
        return False


def transaction():
    """Buffers writes per calling thread; flushes as one batch on exit, no rollback.

    ``with prism.transaction():`` — field writes made inside the block are
    buffered on the calling thread and applied together when the block
    exits normally. If the block raises, the buffered writes are simply
    discarded (never applied) — there is no rollback of state outside
    the block.
    """
    return _TransactionCtx()


def run(model, title="PRISM App"):
    """Blocks the calling thread until the window closes; releases the GIL.

    Starts the app event loop for *model* and returns once the window
    closes, so other Python threads can run while it blocks.
    """
    return _run(model, title)


def _run_headless(model, delay_ms=100):
    """Blocks the calling thread until the headless app exits; releases the GIL.

    Test-only variant of :func:`run` that uses a headless backend and
    quits after *delay_ms*.
    """
    return _run_headless_impl(model, delay_ms)
