# model_tree_browser

A filesystem browser built on the tree widget's Tier 2 API: `FileTreeSource`
(`file_tree_source.hpp`) hand-implements the `TreeStorage` concept over `std::filesystem`,
enumerating a directory's children lazily (only when it's expanded) and reporting per-node
attributes (type, size or entry count, last-modified time) for the tree's detail panel.

The model itself is minimal — `BrowserModel` wraps the source in a `TreeController` via
`prism::wrap_tree_storage()` and places it with a single `vb.tree(ctrl)`.

Demonstrates: the hand-written `TreeStorage` tier (for data that isn't a reflectable struct),
lazy child enumeration, and the tree's built-in detail panel.

## Run

```bash
ninja -C builddir examples/model_tree_browser/model_tree_browser
./builddir/examples/model_tree_browser/model_tree_browser
```
