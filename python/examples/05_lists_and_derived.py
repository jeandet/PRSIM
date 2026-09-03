"""05_lists_and_derived.py — List<T>, derived, and observers.

Shows:
  - prism.list_field([...]) push/erase/set + observe_insert/remove/update
  - prism.derived with multiple deps (string Annotated style also works)
  - transaction for batch list+field updates

Run:
  PYTHONPATH=build/python python python/examples/05_lists_and_derived.py
"""

import prism


class TodoApp(prism.Model):
    # scalar fields
    new_item = prism.field("")
    filter_text = prism.field("")
    counter = prism.field(0)

    # list fields — type inferred from first element
    items = prism.list_field(["buy milk", "write docs"])
    # derived over scalar deps (List<T> not yet supported as Derived dep);
    # deps are the descriptors themselves, not string names
    summary = prism.derived(lambda self: f"{self.counter.value} items", counter)
    filter_len = prism.derived(lambda self: len(self.filter_text.value), filter_text)

    def view(self, vb):
        vb.vstack(self.new_item, self.filter_text, self.counter)
        vb.list(self.items)
        vb.widget(self.summary)
        vb.widget(self.filter_len)


m = TodoApp()
print(
    f"initial items: {m.items.to_list()} summary={m.summary.value} filter_len={m.filter_len.value}"
)

# observe list mutations
ins = []
rem = []
m.items.observe_insert(lambda idx, val: ins.append((idx, val)))
m.items.observe_remove(lambda idx: rem.append(idx))

m.items.push("test python api")
print(f"after push: {m.items.to_list()} inserted={ins}")

m.items.erase(0)
print(f"after erase(0): {m.items.to_list()} removed={rem}")

# derived recomputes automatically
m.counter.value = 5
m.filter_text.value = "hello"
print(
    f"derived after updates: summary={m.summary.value} filter_len={m.filter_len.value}"
)

# batch via transaction
with prism.transaction():
    m.items.push("inside txn 1")
    m.items.push("inside txn 2")
    m.new_item.value = "hello"

print(f"after txn: {m.items.to_list()} new_item={m.new_item.value}")

prism.run(m, title="Lists + Derived — Python")
