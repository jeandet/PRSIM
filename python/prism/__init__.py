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
    Connection,
    is_logic_thread,
    run as _run,
    _txn_begin,
    _txn_commit,
    _txn_abort,
    _run_headless as _run_headless_impl,
    _is_running,
)

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
    "Connection",
    "is_logic_thread",
    "run",
]


class _FieldDescriptor:
    def __init__(self, default, kind=None, meta=None):
        self.default = default
        self.kind = kind
        self.meta = meta or {}
        self.name = None

    def __set_name__(self, owner, name):
        self.name = name

    def _allocate(self, instance):
        cache = instance.__dict__.setdefault("_prism_fields", {})
        if self.name in cache:
            return cache[self.name]
        # allocate via Model's internal add_* (no keep_alive cycle — Model owns slots)
        # bool is subclass of int, check bool first
        if isinstance(self.default, bool):
            h = instance._add_bool_internal(self.default)
        elif isinstance(self.default, int):
            h = instance._add_int_internal(self.default)
        elif isinstance(self.default, float):
            h = instance._add_float_internal(self.default)
        elif isinstance(self.default, str):
            h = instance._add_str_internal(self.default)
        else:
            h = instance._add_int_internal(int(self.default))
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)

    def __set__(self, instance, value):
        h = self._allocate(instance)
        h.value = value


def field(default):
    return _FieldDescriptor(default)


def slider(default, min=0.0, max=1.0):
    return _FieldDescriptor(
        float(default), kind="slider", meta={"min": float(min), "max": float(max)}
    )


def checkbox(default, label=None):
    return _FieldDescriptor(
        bool(default),
        kind="checkbox",
        meta={"label": label} if label is not None else {},
    )


class _SharedDescriptor:
    def __init__(self, default):
        self.default = default
        self.name = None

    def __set_name__(self, owner, name):
        self.name = name

    def _allocate(self, instance):
        cache = instance.__dict__.setdefault("_prism_fields", {})
        if self.name in cache:
            return cache[self.name]
        if isinstance(self.default, bool):
            h = instance._add_shared_bool_internal(self.default)
        elif isinstance(self.default, int):
            h = instance._add_shared_int_internal(self.default)
        elif isinstance(self.default, float):
            h = instance._add_shared_float_internal(self.default)
        elif isinstance(self.default, str):
            h = instance._add_shared_str_internal(self.default)
        else:
            h = instance._add_shared_int_internal(int(self.default))
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)

    def __set__(self, instance, value):
        h = self._allocate(instance)
        h.value = value


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
        th = self.type_hint
        if isinstance(th, bool):
            h = instance._add_channel_bool_internal()
        elif isinstance(th, int):
            h = instance._add_channel_int_internal()
        elif isinstance(th, float):
            h = instance._add_channel_float_internal()
        elif isinstance(th, str):
            h = instance._add_channel_str_internal()
        else:
            h = instance._add_channel_int_internal()
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)


def shared(default):
    return _SharedDescriptor(default)


def channel(type_hint=0):
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
        dep_handles = []
        for d in self.deps:
            if isinstance(d, str):
                dep_handles.append(getattr(instance, d))
            else:
                dep_handles.append(d)
        # infer type from probe call if not hinted; also build wrapped call_fn
        import inspect

        sig = inspect.signature(self.fn)
        n_params = len(sig.parameters)
        n_deps = len(dep_handles)
        # collect current dep values for probe of (a,b) style
        def _dep_vals():
            vals = []
            for h in dep_handles:
                try:
                    vals.append(h.value)
                except Exception:
                    vals.append(0)
            return vals

        import weakref as _wr

        if n_params == 1 and n_deps >= 1:
            # self-style: fn(instance) — break Model->slot->py_fn->instance cycle via weakref
            wref = _wr.ref(instance)
            orig = self.fn
            probe_fn = lambda: orig(wref()) if wref() is not None else orig(instance)  # type: ignore[no-untyped-call]
            call_fn = lambda _w=wref, _f=orig: _f(_w()) if _w() is not None else None  # type: ignore[no-untyped-call]
            try:
                probe = probe_fn()
            except Exception:
                probe = 0
        elif n_params == n_deps and n_deps > 0:
            vals = _dep_vals()
            probe_fn = lambda: self.fn(*vals)  # type: ignore[no-untyped-call]
            # break cycle: capture weakref + dep names (strings) not handles
            wref2 = _wr.ref(instance)
            dep_names = tuple(d if isinstance(d, str) else None for d in self.deps)
            has_str_names = all(n is not None for n in dep_names)
            if has_str_names:
                orig2 = self.fn
                call_fn = lambda _w=wref2, _f=orig2, _ns=dep_names: _f(*[getattr(_w(), n).value for n in _ns]) if _w() is not None else None  # type: ignore[no-untyped-call]
            else:
                # fallback: captures handles strongly (rare)
                call_fn = lambda: self.fn(*[h.value for h in dep_handles])  # type: ignore[no-untyped-call]
            try:
                probe = probe_fn()
            except Exception:
                probe = vals[0] if vals else 0
        else:
            # try no-arg
            try:
                probe = self.fn()  # type: ignore[no-untyped-call]
                call_fn = self.fn
            except Exception:
                # fallback: try with dep vals
                vals = _dep_vals()
                try:
                    probe = self.fn(*vals)  # type: ignore[no-untyped-call]
                    call_fn = lambda: self.fn(*[h.value for h in dep_handles])  # type: ignore[no-untyped-call]
                except Exception:
                    probe = 0
                    call_fn = self.fn
        th = self.type_hint
        if th is None:
            th = probe
        if isinstance(th, bool):
            h = instance._add_derived_bool_internal(call_fn, *dep_handles)  # type: ignore[attr-defined]
        elif isinstance(th, int):
            h = instance._add_derived_int_internal(call_fn, *dep_handles)  # type: ignore[attr-defined]
        elif isinstance(th, float):
            h = instance._add_derived_float_internal(call_fn, *dep_handles)  # type: ignore[attr-defined]
        elif isinstance(th, str):
            h = instance._add_derived_str_internal(call_fn, *dep_handles)  # type: ignore[attr-defined]
        else:
            # fallback int
            h = instance._add_derived_int_internal(call_fn, *dep_handles)  # type: ignore[attr-defined]
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)


def derived(fn=None, *deps, type_hint=None):
    """Descriptor factory: @derived('a','b') or derived(lambda self: ..., 'a').

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
        elif isinstance(vals[0], bool):
            # bool lists map to int list due to vector<bool> proxy constraints
            h = instance._add_list_int_internal([int(v) for v in vals])
        elif isinstance(vals[0], int):
            h = instance._add_list_int_internal(vals)
        elif isinstance(vals[0], float):
            h = instance._add_list_float_internal(vals)
        elif isinstance(vals[0], str):
            h = instance._add_list_str_internal(vals)
        else:
            h = instance._add_list_str_internal([str(x) for x in vals])
        cache[self.name] = h
        return h

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self._allocate(instance)


def list_field(default=None):
    return _ListDescriptor(default)


class Model(_ModelBase):
    def __init__(self, **kwargs):
        super().__init__()
        # Allocate descriptors eagerly so view ordering matches class definition order
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
                    ),
                ):
                    attr._allocate(self)
        # Apply kwargs overrides
        for k, v in kwargs.items():
            if hasattr(type(self), k) and isinstance(
                getattr(type(self), k), (_FieldDescriptor, _SharedDescriptor)
            ):
                setattr(self, k, v)
            else:
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
    return _TransactionCtx()


def run(model, title="PRISM App"):
    return _run(model, title)


def _run_headless(model, delay_ms=100):
    return _run_headless_impl(model, delay_ms)
