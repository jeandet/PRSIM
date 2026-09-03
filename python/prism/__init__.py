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
    _request_quit,
    _set_error_handler,
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
    "headless",
    "App",
    "on_error",
    "worker",
    "Worker",
]


import weakref as _wr_mod
import atexit as _atexit_mod
import contextlib as _contextlib_mod
import threading as _threading_mod
import time as _time_mod
import traceback as _traceback_mod

from . import _prism_ext

# Fire-and-forget keepalive: each observe*() binding in prism_ext.cpp appends the returned
# Connection to handle.__dict__["_prism_keepalive"] (handles are nanobind objects with
# dynamic_attr + weakref) and registers the handle in _prism_ext._observed_handles, so a
# `handle.observe(cb)` call with no assignment still keeps firing. That keepalive list forms
# a real reference cycle — handle -> Connection -> (nanobind keep_alive<0,1>, invisible to
# the cyclic GC) -> handle — that Python's GC can never break on its own; atexit must
# explicitly disconnect it before interpreter teardown. _all_models covers Model-owned
# handles, _observed_handles additionally covers standalone ones (FieldInt etc. not
# attached to any Model). A handle can appear in both; _disconnect_keepalive is idempotent.
_all_models: _wr_mod.WeakSet = _wr_mod.WeakSet()


def _disconnect_keepalive(handle):
    lst = None
    try:
        lst = handle.__dict__.get("_prism_keepalive")  # type: ignore[attr-defined]
    except (AttributeError, TypeError):
        lst = None
    if lst is None:
        try:
            lst = getattr(handle, "_prism_keepalive", None)
        except Exception:
            lst = None
    if not lst:
        return
    for conn in list(lst):
        try:
            conn.disconnect()
        except Exception:
            pass
    try:
        lst.clear()
    except Exception:
        pass


def _disconnect_model_observers(model):
    """Disconnect every observer keepalive reachable from *model*, without
    touching its ``_prism_fields`` cache.

    This is the half of ``_clear_model_observers`` that's safe to run while
    *model* is still in active use (e.g. from ``run()``'s ``finally``): it
    breaks the Model -> handle -> Connection -> ... -> Model reference cycle
    an observer closure that captures the model creates (invisible to
    nanobind's leak check, same class of bug as the pre-Task-14 observe()
    self-cycle), but keeps each handle object alive and cached so
    ``m.field.value`` still reads/writes the *same* underlying slot after
    the app has closed. Clearing ``_prism_fields`` here instead would make
    the next ``m.field`` re-``_allocate()`` a brand-new slot at its
    descriptor default — a silent reset back to the constructor's default,
    not just "stale data" — which is worse than leaving the cache in place.
    """
    d = getattr(model, "__dict__", {})
    fields = d.get("_prism_fields", {})
    for h in list(fields.values()):
        _disconnect_keepalive(h)
    _disconnect_keepalive(model)  # future: model may carry its own _prism_keepalive directly


def _clear_model_observers(model):
    """Full interpreter-exit teardown: disconnect observers *and* drop the
    ``_prism_fields`` cache. Only safe once the model is genuinely done
    (process exiting) — see ``_disconnect_model_observers`` for the
    still-in-use-safe half of this, used by ``run()``/``_run_headless()``.
    """
    _disconnect_model_observers(model)
    d = getattr(model, "__dict__", {})
    fields = d.get("_prism_fields", {})
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
    try:
        _all_models.clear()
    except Exception:
        pass

    for h in list(_prism_ext._observed_handles):
        try:
            _disconnect_keepalive(h)
        except Exception:
            pass
    try:
        _prism_ext._observed_handles.clear()
    except Exception:
        pass
    # run()/_run_headless() already disconnect the model they were given
    # (see _disconnect_model_observers there) before returning, so a Model
    # left in a module global no longer trips nanobind's leak check once the
    # app closes — PyModel's tp_traverse/tp_clear (task 16) let the cyclic GC
    # find and break the Model -> slot -> callback -> Model cycle a
    # `derived()` field or a `view(self, vb)` override forms, so plain
    # top-level scripts no longer need `def _main(): m = ...`.


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
        # allocate via Model's internal add_* (no keep_alive cycle — Model owns slots)
        kind = _kind_of(self.default)
        h = getattr(instance, f"_add_{kind}_internal")(self.default)
        h.__dict__["_prism_name"] = self.name
        if self.validator is not None:
            # Installed here so the C++ .value setter / .set() can run it too
            # (prism_ext.cpp's apply_validator) — one validation path shared
            # by `m.x = v`, `m.x.value = v` and `m.x.set(v)`.
            h.__dict__["_prism_validator"] = self.validator
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)

    def __set__(self, instance, value):
        self._allocate(instance).value = value

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

    Float field; a ranged slider widget is not implemented yet. ``min``/``max``
    are accepted for API stability but are currently unused.
    """
    return _FieldDescriptor(float(default), validator=validator)


def checkbox(default, label=None, validator=None):
    """Value may be set from any thread (posted to the logic thread).

    Bool field. ``label`` is accepted for API stability but is currently unused.
    """
    return _FieldDescriptor(bool(default), validator=validator)


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
        h.__dict__["_prism_name"] = self.name
        if self.validator is not None:
            h.__dict__["_prism_validator"] = self.validator
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)

    def __set__(self, instance, value):
        self._allocate(instance).value = value

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

        h = getattr(instance, f"_add_derived_{kind}_internal")(call_fn, self.name, *dep_handles)  # type: ignore[attr-defined]
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
    ``_TreeDescriptor._allocate``. The ``TreeSource`` protocol lives in
    ``prism.TreeSource`` — inheriting is optional, structural matching is
    enough, but inheriting gives IDE support.
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
    except ImportError as e:
        raise RuntimeError(f"validator_for requires pydantic: {e}") from e
    ta = TypeAdapter(type_hint)
    return ta.validate_python


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
    try:
        return _run(model, title)
    finally:
        _stop_all_workers()
        _disconnect_model_observers(model)


def _run_headless(model, delay_ms=100):
    """Blocks the calling thread until the headless app exits; releases the GIL.

    Private primitive underlying :func:`headless`. Uses a headless backend
    that quits after *delay_ms* or as soon as ``_request_quit()`` is called
    from any thread — whichever comes first.
    """
    try:
        return _run_headless_impl(model, delay_ms)
    finally:
        _stop_all_workers()
        _disconnect_model_observers(model)


class App:
    """Any thread. Handle to a headless app started by :func:`headless`.

    ``wait_until``/``quit``/``is_running`` may be called from any thread;
    the predicate given to ``wait_until`` is polled on the calling thread,
    not the logic thread.
    """

    def __init__(self, thread):
        self._thread = thread

    @property
    def is_running(self):
        return _is_running()

    def wait_until(self, predicate, timeout=None, poll=0.005):
        """Any thread. Poll *predicate* until it's truthy.

        Raises ``TimeoutError`` if *timeout* seconds pass first. With
        *timeout* ``None`` (the default), waits indefinitely.
        """
        deadline = None if timeout is None else _time_mod.monotonic() + timeout
        while not predicate():
            if deadline is not None and _time_mod.monotonic() >= deadline:
                raise TimeoutError(f"condition not met within {timeout} s")
            _time_mod.sleep(poll)

    def quit(self):
        """Any thread. Requests the app close. Idempotent."""
        _request_quit()


@_contextlib_mod.contextmanager
def headless(model, *, timeout: float = 10.0):
    """Any thread. Runs *model* headless for the ``with`` block; releases the GIL.

    Starts :func:`_run_headless` on a background thread and blocks until
    the app is up, yielding an :class:`App` handle. *timeout* is the
    app's outer safety ceiling (seconds) — normally the block ends the app
    sooner via ``app.quit()`` (called automatically on exit) or
    ``app.wait_until(...)``. Exceptions raised inside the block propagate
    after the app is stopped and joined.
    """
    thread = _threading_mod.Thread(
        target=_run_headless, args=(model,), kwargs={"delay_ms": int(timeout * 1000)}, daemon=True
    )
    thread.start()
    while not _is_running():
        _time_mod.sleep(0.001)
    app = App(thread)
    try:
        yield app
    finally:
        app.quit()
        thread.join()


# Mirror of the handler installed via _set_error_handler: the C++ side has no
# getter, but Worker._run needs the current handler from plain Python threads
# that never go through prism::core's callback machinery. on_error() keeps
# both in sync.
_error_handler = None


def on_error(handler):
    """Any thread. Called on whichever thread raised — the logic thread for
    observer/derived callbacks, the worker's own thread for prism.worker()
    exceptions. Handlers must be thread-safe. None restores the default
    (traceback to stderr).
    """
    if handler is not None and not callable(handler):
        raise TypeError("on_error(): handler must be callable or None")
    global _error_handler
    _error_handler = handler
    _set_error_handler(handler)


def _report_worker_error(exc):
    if _error_handler is not None:
        _error_handler(exc)
    else:
        _traceback_mod.print_exception(type(exc), exc, exc.__traceback__)


# WeakSet so a Worker the caller never assigns still gets swept here without
# being kept alive by this set alone; run()/_run_headless() stop every live
# worker on exit so nothing outlives the app. _live_workers_lock guards both
# the add() in worker() and the snapshot in _stop_all_workers() — a WeakSet
# can raise "Set changed size during iteration" when a dead entry's weakref
# callback fires (on another thread's GC) while list(_live_workers) is
# iterating it.
_live_workers_lock = _threading_mod.Lock()
_live_workers: _wr_mod.WeakSet = _wr_mod.WeakSet()


def _register_worker(w):
    with _live_workers_lock:
        _live_workers.add(w)


def _stop_all_workers():
    with _live_workers_lock:
        workers = list(_live_workers)
    for w in workers:
        w.stop()


# Registered after _atexit_clear (line 269) so it runs first at interpreter exit
# (atexit is LIFO): workers must be stopped before _atexit_clear tears down
# observer keepalives they may still be calling into.
_atexit_mod.register(_stop_all_workers)


class Worker:
    """Any thread. Runs fn on a background thread; exceptions go to prism.on_error()."""

    def __init__(self, fn, *, interval=None, daemon=True, name=None):
        self._fn = fn
        self._interval = interval
        self._stop = _threading_mod.Event()
        self._thread = _threading_mod.Thread(target=self._run, daemon=daemon, name=name)

    def _run(self):
        try:
            if self._interval is None:
                self._fn(self._stop)
            else:
                # wait first: first call lands after one interval, and a
                # stop() during the wait exits immediately without a call.
                while not self._stop.wait(self._interval):
                    self._fn(self._stop)
        except Exception as exc:
            _report_worker_error(exc)

    @property
    def is_alive(self):
        return self._thread.is_alive()

    def start(self):
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        # Guards two cases: never started (Thread.join() raises RuntimeError
        # before start()) and already-finished (join is then a no-op anyway)
        # — makes stop() safe to call unconditionally, incl. from
        # _stop_all_workers() racing a worker() call still mid-start.
        if self._thread.is_alive():
            self._thread.join(timeout=1.0)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.stop()
        return False


def worker(fn, *, interval=None, daemon=True, name=None):
    """Any thread. Runs fn on a background thread; exceptions go to prism.on_error().

    Calls ``fn(stop: threading.Event)`` once if *interval* is None. With an
    *interval*, waits *interval* seconds (via ``stop.wait``) before each
    call — so the first call happens after one interval — and exits
    immediately once ``.stop()`` sets the event. Starts immediately and
    returns the :class:`Worker`; still-running workers are stopped when
    ``run()`` / ``_run_headless()`` return.
    """
    w = Worker(fn, interval=interval, daemon=daemon, name=name)
    w.start()
    _register_worker(w)
    return w
