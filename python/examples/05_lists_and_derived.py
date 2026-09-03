"""05_lists_and_derived.py — list_field() mutations + derived() over multiple deps.

Demonstrates:
  - prism.list_field() push/erase + observe_insert/observe_remove
  - prism.derived() recomputing from descriptor deps
  - prism.transaction() batching a list push with a field write

Run:
  PYTHONPATH=builddir/python python3 python/examples/05_lists_and_derived.py
"""

import prism


class TodoApp(prism.Model):
    new_item = prism.field("")
    filter_text = prism.field("")
    counter = prism.field(0)
    items = prism.list_field(["buy milk", "write docs"])
    summary = prism.derived(lambda self: f"{self.counter.value} items", counter)
    filter_len = prism.derived(lambda self: len(self.filter_text.value), filter_text)

    def view(self, vb):
        vb.vstack(self.new_item, self.filter_text, self.counter)
        vb.list(self.items)
        vb.widget(self.summary)
        vb.widget(self.filter_len)


m = TodoApp()
print(f"initial items={m.items.to_list()} summary={m.summary.value}")

inserted = []
removed = []
m.items.observe_insert(lambda idx, val: inserted.append((idx, val)))
m.items.observe_remove(lambda idx: removed.append(idx))

m.items.push("test python api")
m.items.erase(0)
print(f"after push+erase: items={m.items.to_list()} inserted={inserted} removed={removed}")

m.counter.value = 5
m.filter_text.value = "hello"
print(f"derived after updates: summary={m.summary.value} filter_len={m.filter_len.value}")

with prism.transaction():
    m.items.push("inside txn")
    m.new_item.value = "hello"

print(f"after txn: items={m.items.to_list()} new_item={m.new_item.value}")

prism.run(m, title="Lists + Derived — Python")
