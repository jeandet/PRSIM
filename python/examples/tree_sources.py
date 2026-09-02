"""tree_sources.py — Shared TreeSource implementations for the tree/dashboard examples.

Demonstrates:
  - prism.TreeSource Protocol implemented over a plain dict (DictTreeSource)
  - prism.TreeSource implemented over the real filesystem, lazily (FsTreeSource)

Not runnable on its own — imported by 07_file_tree.py and 08_dashboard.py.
The script directory is on sys.path when either of those is run directly,
so ``import tree_sources`` resolves without any importlib gymnastics.
"""

import pathlib

import prism


class DictTreeSource(prism.TreeSource):
    """Minimal TreeSource over a nested dict: {id: {label, children:[ids], attrs:{}}}"""

    def __init__(self, data: dict, roots: list[str]):
        self._data = data
        self._roots = roots
        # stable id = hash of string key
        self._id_of = {k: hash(k) for k in data}
        self._key_of = {hash(k): k for k in data}

    # TreeStorage protocol — these run on logic thread with GIL held (C++ calls with GIL)
    def root_count(self):
        return len(self._roots)

    def root_at(self, i):
        return hash(self._roots[i])

    def child_count(self, nid):
        k = self._key_of.get(nid)
        if k is None:
            return 0
        node = self._data.get(k, {})
        return len(node.get("children", []))

    def child_at(self, nid, i):
        k = self._key_of.get(nid, "")
        node = self._data.get(k, {})
        child_key = node["children"][i]
        return hash(child_key)

    def label(self, nid):
        k = self._key_of.get(nid)
        if k is None:
            return str(nid)
        node = self._data.get(k, {})
        return node.get("label", k)

    def has_children(self, nid):
        k = self._key_of.get(nid, "")
        node = self._data.get(k, {})
        return len(node.get("children", [])) > 0

    def attributes(self, nid):
        k = self._key_of.get(nid, "")
        node = self._data.get(k, {})
        return node.get("attrs", {})


# Example data — mirrors showcase_tree.cpp: Sensors/Device hierarchy
TREE_DATA = {
    "Device": {
        "label": "Device (Controller)",
        "children": ["Sensors", "Firmware"],
        "attrs": {"name": "Controller", "firmware": "12"},
    },
    "Sensors": {
        "label": "Sensors",
        "children": ["Battery", "Bus"],
        "attrs": {"count": "2"},
    },
    "Battery": {
        "label": "battery_v = 3.7V",
        "children": [],
        "attrs": {"value": "3.7", "unit": "V"},
    },
    "Bus": {
        "label": "bus_v = 12.1V",
        "children": [],
        "attrs": {"value": "12.1", "unit": "V"},
    },
    "Firmware": {"label": "firmware = 12", "children": [], "attrs": {"version": "12"}},
}


class FsTreeSource(prism.TreeSource):
    """Live filesystem source — shows real directory tree (lazy)."""

    def __init__(self, root: pathlib.Path):
        self.root = pathlib.Path(root)
        # map id -> Path
        self._by_id: dict[int, pathlib.Path] = {}

    def _id(self, p: pathlib.Path) -> int:
        # stable hash by path string
        h = hash(str(p.resolve()))
        self._by_id[h] = p
        return h

    def _path(self, nid: int) -> pathlib.Path:
        return self._by_id.get(nid, self.root)

    def root_count(self):
        return 1

    def root_at(self, i):
        return self._id(self.root)

    def child_count(self, nid):
        p = self._path(nid)
        if not p.is_dir():
            return 0
        try:
            return len(list(p.iterdir()))
        except Exception:
            return 0

    def child_at(self, nid, i):
        p = self._path(nid)
        children = sorted(p.iterdir(), key=lambda x: (not x.is_dir(), x.name.lower()))
        return self._id(children[i])

    def label(self, nid):
        return self._path(nid).name or str(self._path(nid))

    def has_children(self, nid):
        return self._path(nid).is_dir()

    def attributes(self, nid):
        p = self._path(nid)
        try:
            st = p.stat()
            if p.is_dir():
                n = len(list(p.iterdir()))
                return {"type": "dir", "entries": str(n)}
            else:
                return {
                    "type": "file",
                    "size": str(st.st_size),
                    "modified": str(int(st.st_mtime)),
                }
        except Exception as e:
            return {"error": str(e)}
