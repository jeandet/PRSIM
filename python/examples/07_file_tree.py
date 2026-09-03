"""07_file_tree.py — tree_field() browsing an in-memory hierarchy.

Demonstrates:
  - prism.tree_field(source) over a prism.TreeSource-shaped object
  - vb.tree(ctrl) — virtual-list rows + detail panel, lazy child expansion
  - DictTreeSource/FsTreeSource live in tree_sources.py (shared with 08_dashboard.py)

Run:
  PYTHONPATH=builddir/python python3 python/examples/07_file_tree.py
"""

import prism
from tree_sources import TREE_DATA, DictTreeSource


class Browser(prism.Model):
    tree = prism.tree_field(DictTreeSource(TREE_DATA, roots=["Device"]))
    status = prism.field("Click rows, use arrows to expand/collapse and navigate")


m = Browser()
print(f"initial rows: {len(m.tree.rows())}")
prism.run(m, title="Tree Browser — Python")
