"""07_file_tree.py — Fancy Tree example (filesystem browser).

Demonstrates:
  - prism.tree_field(source) with ``prism.TreeSource`` Protocol
  - vb.tree(ctrl) — VirtualList + detail panel with handle splitter
  - Lazy child expansion (only expanded nodes queried)

See ``prism.TreeSource`` (``python/prism/__init__.py``) for the
structural protocol. Any object with the six required methods works;
inheriting from the Protocol is optional but gives type-checker
support.

``DictTreeSource``/``FsTreeSource``/``TREE_DATA`` live in
``tree_sources.py`` (shared with 08_dashboard.py).

Run:
  PYTHONPATH=build/python python python/examples/07_file_tree.py
"""

import prism
from tree_sources import TREE_DATA, DictTreeSource


class Browser(prism.Model):
    # pick one source:
    # file tree over in-memory demo data
    tree = prism.tree_field(DictTreeSource(TREE_DATA, roots=["Device"]))
    # uncomment for live filesystem:
    # tree = prism.tree_field(FsTreeSource(pathlib.Path(".")))
    status = prism.field("Click rows, use arrows ←→ to expand/collapse, ↑↓ to navigate")


m = Browser()
# detail/selected are accessible via controller's detail Field internally;
# for demo we just show row count after refresh
print("initial rows:", len(m.tree.rows()))  # type: ignore[attr-defined]
prism.run(m, title="Tree Browser — Python")
