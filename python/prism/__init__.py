from ._prism_ext import (
    Model as _ModelBase,
    FieldInt,
    FieldFloat,
    FieldStr,
    FieldBool,
    BoundInt,
    BoundFloat,
    BoundStr,
    BoundBool,
    Connection,
    is_logic_thread,
    run as _run,
)

__all__ = [
    "Model",
    "field",
    "slider",
    "checkbox",
    "FieldInt",
    "FieldFloat",
    "FieldStr",
    "FieldBool",
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


class Model(_ModelBase):
    def __init__(self, **kwargs):
        super().__init__()
        # Allocate descriptors eagerly so view ordering matches class definition order
        for cls in reversed(type(self).__mro__):
            for name, attr in cls.__dict__.items():
                if isinstance(attr, _FieldDescriptor):
                    attr._allocate(self)
        # Apply kwargs overrides
        for k, v in kwargs.items():
            if hasattr(type(self), k) and isinstance(
                getattr(type(self), k), _FieldDescriptor
            ):
                setattr(self, k, v)
            else:
                setattr(self, k, v)


def run(model, title="PRISM App"):
    return _run(model, title)
