# model_tree_browser Example

A filesystem browser: point PRISM's tree widget at a directory and get lazy expand/collapse
navigation with a detail panel, all from ~20 lines of model code plus one hand-written adapter.

<p align="center"><img src="../../doc/screenshots/model_tree_browser.svg" alt="model_tree_browser screenshot" width="600"/></p>

*(Screenshot: the root expanded one level, showing this repository's `examples/` directory and
the detail panel for the selected entry — see "Headless capture" below for how that expansion
happens in a static screenshot.)*

## Overview

PRISM's tree widget (`prism::TreeController` + `vb.tree(...)`) has three tiers for supplying
data, from richest-supported to most manual:

1. **Tier 3** — a reflectable struct tree, no adapter code at all.
2. **Tier 2** — hand-implement the `TreeStorage` concept over data that isn't a struct (a
   filesystem, a database, any existing model). **This example.**
3. **Tier 1** — build a `prism::ui::TreeSource` (the underlying `std::function`-based vtable)
   directly, for the most control.

A filesystem is the canonical case for Tier 2: directories don't know their children ahead of
time, so the adapter enumerates them lazily, only when a node is actually expanded.

## Walkthrough

**The adapter** (`file_tree_source.hpp`) satisfies `TreeStorage` — `root_count`/`root_at`,
`child_count`/`child_at`, `label`, `has_children`, plus the optional `icon`/`attributes` hooks —
over `std::filesystem`:

```cpp
class FileTreeSource {
public:
    explicit FileTreeSource(std::filesystem::path root) : root_(std::move(root)) {
        cache_path(root_);
    }

    size_t root_count() const { return 1; }
    prism::ui::TreeNodeId root_at(size_t) const { return path_id(root_); }
    size_t child_count(prism::ui::TreeNodeId id) const { return children_of(id).size(); }
    ...
};
```

`children_of()` — called only from `child_count`/`child_at` — is where the laziness lives: it
runs `std::filesystem::directory_iterator` on demand, not up front. `TreeNodeId` is just a hash
of the path (`path_id()`), with a `by_id_` map on the side to recover the actual
`std::filesystem::path` when the widget asks for a label or attributes.

`attributes()` feeds the tree's detail panel — type (file/directory), size or entry count, and
last-modified time — shown whenever a row is selected:

```cpp
std::vector<std::pair<std::string, std::string>> attributes(prism::ui::TreeNodeId id) const {
    ...
    attrs.emplace_back("type", is_dir ? "directory" : "file");
    if (is_dir) attrs.emplace_back("entries", fmt::to_string(children_of(id).size()));
    else        attrs.emplace_back("size", fmt::format("{} bytes", size));
    attrs.emplace_back("modified", fmt::format("{:%Y-%m-%d %H:%M:%S}", ...));
    ...
}
```

**The model** is almost nothing — `wrap_tree_storage()` bridges the hand-written adapter into a
`TreeSource`, and the view is a single call:

```cpp
struct BrowserModel {
    FileTreeSource source;
    prism::TreeController ctrl{prism::wrap_tree_storage(source)};

    void view(prism::WidgetTree::ViewBuilder& vb) { vb.tree(ctrl); }
};
```

Compare this to `model_system_monitor`'s tree tab, which reflects over a `std::vector<ProcessInfo>`
that's already in memory (no lazy enumeration needed) — same `TreeController`, different
`TreeStorage` implementation underneath.

**Headless capture.** `TreeController` starts fully collapsed (every node's expand state defaults
to `false`), so a single static frame of a freshly-constructed browser would show just one line:
the root. The headless branch instead drives the *same public API* a real mouse click would use —
`ctrl.on_row_clicked(0, ctrl.rows[0])` — to expand the root before capturing, and points the
browser at this repo's own `examples/` directory (rather than whatever directory the build
happens to run from) so the screenshot is both informative and reproducible:

```cpp
if (argc >= 2) {
    BrowserModel model{argc >= 3 ? std::filesystem::path(argv[2])
                                 : std::filesystem::current_path()};
    if (model.ctrl.rows.size() > 0)
        model.ctrl.on_row_clicked(0, model.ctrl.rows[0]);
    return showcase(argc, argv, model, 900, 600);
}
```

## Key concepts

- `TreeStorage` concept + `wrap_tree_storage()` — Tier 2 of the tree widget's API; see the root README's [Quick Tour](../../README.md#quick-tour) for Tier 3 (reflected structs) by comparison.
- Lazy child enumeration — children are only ever asked for when a node is expanded, which matters for data sources where enumerating is expensive (a filesystem, a remote API).
- `TreeController::rows` / `on_row_clicked()` — the same public surface the tree widget itself calls on a real click; there's no separate "expand" API, which is exactly what the headless branch above relies on.

## Building and running

```bash
ninja -C builddir examples/model_tree_browser/model_tree_browser
./builddir/examples/model_tree_browser/model_tree_browser
```

## See also

- [`model_system_monitor`](../model_system_monitor/) — the same `TreeController` over an in-memory process list instead of the filesystem.
