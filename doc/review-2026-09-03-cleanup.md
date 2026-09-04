# Cleanup review — 2026-09-03

Read-only audit, four parallel scopes (C++ core/app, C++ UI/render, Python API, Python
examples), judged against the owner's global clean-code rules (KISS, functional over
imperative, uniform abstraction levels, no comment-decorated blocks, pragmatic
factorization, `simplify:` markers) plus project conventions in AGENTS.md.

Rank legend: HIGH = clear win, low risk, small diff / MEDIUM = worth doing, more
invasive / LOW = taste.

Status: fully implemented across 2026-09-03/04 (batches ending in b95f06e), except
finding texts kept verbatim below for the record. Two deliberate deviations: the
`headless=` rename landed as `headless_seconds` with a new `until=` predicate
(examples #3 folded in), and core #12's second half was already done by the time the
batch reached it.

---

## 1. C++ core + app (`include/prism/core/`, `include/prism/app/`, `src/`)

1. **HIGH — Triplicated List-signal adapters** — `app/view_builder.hpp:349-370, 423-441,
   479-490`. The `vlist_on_insert/remove/update` blocks are verbatim copies in `list()`,
   `tree()`, `table(List<T>&)`; `vlist_unbind_row` is byte-identical in `list()`/`tree()`.
   Extract `template <typename T> void wire_list_signals(Node&, List<T>&)` and a free
   `unbind_vlist_row(WidgetNode&)`; each call site drops ~25 lines.
2. **HIGH — Duplicated dirty-connect plumbing** — `app/widget_tree.hpp:523-626`. The
   on_change + dependencies connect block repeats for leaves, containers, and Table; the
   three `vlist_on_*` connectors are one statement written three times. Extract
   `connect_simple_dirty(Node&, WidgetNode&)`; loop the three connectors over a small array.
3. **HIGH — Scroll-math recomputed in six places** — `app/widget_tree.hpp:83, 111, 127,
   216, 820, 1117`. `max_off{max(0, content_h - viewport_h)}` ×6, and the reveal-a-row
   clamp written twice (`scroll_row_into_view` 117-133, table keyboard handler 1166-1177).
   Add pure free functions `max_scroll()` / `reveal_offset()` to
   `widget_tree_traversal.hpp`.
4. **HIGH — Bare `namespace detail` under `prism::core`** — `core/error_hub.hpp:12`.
   Violates the hard project naming rule (collides with `prism::ui::detail` under
   `using namespace`). Rename to `error_hub_detail`. (See also UI-layer finding 1.)
5. **HIGH — Wrapper-apply ternary hand-written 3×** — `app/model_app.hpp:143, 251, 392`,
   though `AppContext::run_wrapped` (79-82) already exists. Route all three through it.
6. **MEDIUM — `materialize_table` is 170 lines, four abstraction levels** —
   `app/widget_tree.hpp:1013-1182`. Range math + index/pool maintenance + per-cell
   painting + a 70-line input lambda in one function. Split into `recycle_rows()`,
   `paint_table_row()`, `render_table_header()`, `wire_table_input()`.
7. **MEDIUM — Tabs special case must stay in sync with the generic exclusion set** —
   `app/widget_tree.hpp:504-518`. Assign `edit_state` per kind in the if-chain, then one
   `if (kind != VirtualList && kind != Table)` child loop; the Tabs case disappears.
8. **MEDIUM — `model_app` dispatch lambda mixes orchestration with routing** —
   `app/model_app.hpp:359-390`. Add `widget_detail::route_event(tree, snap, ev)` to
   `event_routing.hpp` absorbing the mouse/key/text chain.
9. **MEDIUM — Unindex teardown triple open-coded 3×** — `app/widget_tree.hpp:906-913,
   972-979, 1025-1031`. Extract `unindex_node(WidgetNode&)` (index_/parent_map_/
   focus_order_ erasures + `connections.clear()`); drift between copies leaks stale ids.
10. **MEDIUM — `AppContext::post` nests the CAS-reschedule drain two lambdas deep** —
    `app/model_app.hpp:118-145`. Move the drain loop into a private `drain_queue()`.
11. **MEDIUM — SDL event cases repeat lock/find/decoration/adjust-Y five times** —
    `src/backends/software_backend.cpp:211-321`. Extract `find_window(WindowId)` and
    `client_y(WindowId, float)`.
12. **LOW — Tick path duplicates the already-factored mutation tail** —
    `app/model_app.hpp:241-250 vs 260-266, 387-389`. `do_tick` can delegate to
    `*mutation_drain_publish`; line 387 can call `publish_dirty()`.
13. **LOW — `update_hover`/`set_pressed`/`set_focused`/`clear_focus` open-code the same
    flip-flag-and-dirty twice each** — `app/widget_tree.hpp:146-167, 295-316`. One private
    `set_visual_flag(WidgetId, bool WidgetVisualState::*, bool)` collapses all four.
14. **LOW — on_change→Node::on_change adapter lambda written 3×** —
    `app/view_builder.hpp:70-75, 286-289, 625-628`. Free `adapt_on_change(Observable&)`.
15. **LOW — `set_decoration_mode`/`set_resizable` both do save-size/destroy/recreate** —
    `src/backends/sdl_window.cpp:144-165`. Extract `recreate_preserving_size()`.

Checked and clean: `build_snapshot` (comments are genuine why-docs, `do_layout` already
extracted), `update_canvas_bounds`, the App/app/model_app skeleton similarity (three
distinct API tiers), `AppContext`'s ctor pair, `TransactionGuard`, `mpsc_queue`,
`connection.hpp`, `channel.hpp`.

## 2. C++ UI/render (`include/prism/ui/`, `delegates/`, `widgets/`, `render/`, `input/`, `backends/`)

1. **HIGH — Bare `namespace detail` under module namespaces, 8 headers** —
   `ui/layout.hpp:45,76,270`, `ui/delegate.hpp:269,278`, `ui/table.hpp:65,146`,
   `delegates/text_delegates.hpp:13`, `delegates/tabs_delegates.hpp:10`,
   `delegates/dropdown_delegates.hpp:10`, `widgets/debug/tree_inspector.hpp:52`,
   `render/svg_export.hpp:11`. Same collision hazard as core finding 4.
   Rename to module-named namespaces (`layout_detail`, `delegate_detail`, …);
   `tree.hpp:149`'s `detail_tree` is the established pattern. Mechanical, no behavior
   change.
2. **HIGH — `layout_flatten()` is 212 lines, four abstraction levels** —
   `ui/layout.hpp:311-522`. Extract `flatten_table` / `flatten_scrollable` / `flatten_leaf`
   free functions; leave `layout_flatten` as kind dispatch + tail recursion. The
   snapshot-cache invariant comment (461-471) is load-bearing and easier to protect once
   isolated.
3. **MEDIUM — Scrollbar thumb geometry duplicated, hit regions inconsistent** —
   `ui/layout.hpp:385-400 vs 439-455`. Same thumb math twice; Table pushes the full track
   rect into `overlay_geometry`, Scroll only the thumb. Extract `scrollbar::emit_thumb()`
   and unify.
4. **MEDIUM — Focus-ring + background preamble repeated across 9+5 delegate sites** —
   `ui/delegate.hpp:298,322,340,390,405,430,443,524,532,584`,
   `delegates/text_delegates.hpp:56,246`, `delegates/dropdown_delegates.hpp:49`,
   `delegates/tabs_delegates.hpp:65`. Add `draw_focus_ring(dl, w, h, t)` and
   `fill_widget_bg(dl, node, h)` next to `draw_check_box`.
5. **MEDIUM — Activation-event pattern written 3× (and already drifting)** —
   `ui/delegate.hpp:410-415, 448-454, 588-593`. Extract
   `bool is_activation_event(const InputEvent&)`.
6. **MEDIUM — Draw-command translation visitor written twice** — `ui/layout.hpp:272-297
   vs 340-352`. Extract `translate_cmd(DrawCmd&, DX, DY)`.
7. **MEDIUM — Four identical `get_X_state`/`ensure_X_state` pairs** —
   `delegates/text_delegates.hpp:15-23,211-219`, `delegates/dropdown_delegates.hpp:17-25`,
   `delegates/tabs_delegates.hpp:12-20`. Add `template<typename S> const S&
   WidgetNode::peek() const` (static-default fallback) and call `get_or_create<S>()`
   directly; delete all eight functions.
8. **LOW — `node_leaf`/`node_readonly_leaf` share a verbatim fallback-record lambda and
   debug-name block** — `ui/widget_node.hpp:137-149 & 172-180 vs 197-206 & 218-226`.
9. **LOW — `layout_arrange` Table/VirtualList branches near-identical** —
   `ui/layout.hpp:172-202`. Merge into `arrange_uniform_rows()`.
10. **LOW — `current_offset()` application repeated in all 8 DrawList push methods** —
    `render/draw_list.hpp:84-153`. Private `translated(Point)`/`translated(Rect)` helpers.
11. **LOW — `localize_mouse` repeats copy-subtract-return 3×** —
    `input/input_event.hpp:58-76`. One `std::visit` with `if constexpr (requires {
    e.position; })` covers all variants and future ones.
12. **LOW — `to_svg(SceneSnapshot)` overloads duplicate scene emission** —
    `render/svg_export.hpp:203-207 vs 226-230`.
13. **LOW — Stale diff-positioning comment** — `ui/layout.hpp:54` (`// after `DrawList
    draws;``). Delete.
14. **LOW — `PlotPanel` merged-view-transform block duplicated** —
    `widgets/plot.hpp:456-460 vs 470-474`.

Checked and clean: `Widget<TextField/Password/TextArea>::get/ensure_edit_state`
forwarders (public test API — keep), `Widget<TreeRow>` vs debug `NodeRow` mirror
(deliberate), `Animation`/`TransitionGuard` `tick_with` pairs (meaningfully different).

## 3. Python API (`python/prism/`, `python/src/`)

1. **HIGH — Malformed plot colors silently ignored; parser triplicated** —
   `python/src/prism_ext.cpp:725-730, 762-767, 1399-1404`. A non-`#rrggbb` string is
   silently dropped to the default; `"#zzzzzz"` throws on the logic thread and lands in
   `on_error`, never at the caller. Extract one `parse_series_color()` that validates on
   the calling thread and throws `nb::value_error`.
2. **HIGH — `Worker.__enter__` doesn't start the worker** — `python/prism/__init__.py:
   1419-1424`. `with prism.Worker(fn):` is a silent no-op (only the `worker()` factory
   calls `start()`). Start in `__enter__` behind a `_started` flag, or drop the CM from
   the raw class.
3. **HIGH — Dead first branch in `derived()`** — `python/prism/__init__.py:680-685`.
   Strict subset of the next branch with identical result; its comment is wrong. Delete.
4. **MEDIUM — Single-dep value-style `derived` silently reinterpreted as self-style** —
   `python/prism/__init__.py:603-620`. `derived(lambda v: v*2, 'a')` passes the *Model*,
   not `a.value`; with `type_hint` set it fails silently via `on_error` forever. Document
   the precedence or drop the unreachable value-style branch.
5. **MEDIUM — `run(headless=...)` kwarg shadows the `headless` context manager** —
   `python/prism/__init__.py:1125, 1290-1293` (already needed the `_headless_ctx` alias
   workaround). Rename kwarg to `headless_seconds`.
6. **MEDIUM — List handles return default values on out-of-range reads** —
   `python/src/prism_ext.cpp:1312-1315, 1357-1361`. `lst.get(i)` is synchronous, so OOB
   should raise `nb::index_error`, not return `T{}`. Optionally add `__len__`/`__getitem__`.
7. **MEDIUM — `TableSource`/`is_table_source` missing from `__all__`** —
   `python/prism/__init__.py:111, 835` vs 141-170 (the `TreeSource` twins are exported).
8. **MEDIUM — `rows()` returns anonymous dicts** — `python/src/prism_ext.cpp:912-924,
   2308-2318`. Return a `TreeRow` namedtuple with the same five fields.
9. **MEDIUM — `Model(**kwargs)` silently accepts typo'd field names** —
   `python/prism/__init__.py:1042-1043`. Validate keys against the descriptor set, raise
   `TypeError`.
10. **MEDIUM — `ViewBuilder` binding methods and most bound handles lack docstrings;
    `hstack`/`vstack` dual calling convention is C++-comment-only** —
    `python/src/prism_ext.cpp:2320-2346`.
11. **LOW — Slider `range` is a read-only property plus a separate `set_range()`** —
    `python/src/prism_ext.cpp:2258-2268`. Make it `def_prop_rw` taking a (min, max) tuple.
12. **LOW — Unsupported-type errors raised as `RuntimeError` instead of `TypeError`** —
    `python/src/prism_ext.cpp:1544, 1569, 1575, 1581, 1587`.
13. **LOW — `list_field()` type inference undocumented; bool lists silently become int
    lists** — `python/prism/__init__.py:740-748`. Two docstring sentences.
14. **LOW — Seven descriptor classes repeat the same `__set_name__`/`__get__`/
    cache-preamble boilerplate** — `python/prism/__init__.py:393-923`. Tiny shared
    `_Descriptor` base.

Checked and clean: deprecated class-level `observe` shims (proper DeprecationWarnings),
slider orientation double-validation (defense in depth), `min`/`max` param names
(matplotlib-style convention), async `ListHandle.erase`/`set` OOB no-op (forced by the
posted-closure design — only the *sync* `get()` is finding 6). Keepalive/GC machinery is
heavily commented but justified; error-hub routing thorough; public Python functions have
consistently strong docstrings.

## 4. Python examples (`python/examples/`)

The 2026-09-03-morning audit's fixes (up to `9821ac2`) are genuinely in — manual threads,
pump loops, class-level `observe`, duplicate tree sources, `_main()` wrappers all gone.

1. **HIGH — Dead attribute `_id_of`** — `python/examples/tree_sources.py:24`. Never read;
   every method calls `hash(k)` directly. Delete the line.
2. **HIGH — Hand-rolled `nonlocal ticks` ticker duplicates the `field.add()` idiom** —
   `11_error_handling.py:44-51`, `12_asyncio_bridge.py:73-80`. `02_mixer.py:27` already
   teaches `prism.worker(lambda: m.count.add(1), ...)`; identical value sequence. Replace.
3. **MEDIUM — `--headless` + `wait_until` + assert boilerplate belongs in the API** —
   `10_worker_pool_plot.py:102-112`, `12_asyncio_bridge.py:82-90` (11 solves it in one
   line via `prism.run(..., headless=...)`). Add an `until=` predicate to `run()`'s
   headless path so 10/12 collapse to the same one-liner.
4. **LOW — `"--headless" in sys.argv` re-evaluated per use** — `10:102,111`, `12:82,89`.
   Bind once (moot if #3 lands).
5. **LOW — `DictTreeSource` mixes two missing-key defenses** — `tree_sources.py:34-62`.
   Standardize on the `if k is None` early-return form in all five methods.
6. **LOW — `07_file_tree.py:5` docstring advertises `vb.tree(ctrl)` the example never
   calls** — reword (the explicit call is in `08_dashboard.py:31`).
7. **LOW — `06_live_plot.py:25` derived field named `title` collides conceptually with
   `prism.run(m, title=...)`** — rename to `caption`/`heading`.

Checked and clean: list-form `replace_series` in 06 (deliberate), GIL-enabled prints
(two 3-line copies — fine), script-style vs `main()`-guard split (deliberate), the
busy-poll reaper and pure-Python FFT in 10 (documented no-numpy choice).

---

## Suggested work order

1. Mechanical, zero-risk: `detail`-namespace renames (core #4, UI #1), dead-code deletions
   (Python #3, examples #1), `run_wrapped` routing (core #5).
2. Small extractions with tests already covering the paths: view_builder signal adapters
   (core #1), dirty-connect plumbing (core #2), scroll math (core #3), activation-event
   helper (UI #5), focus-ring/bg helpers (UI #4 — just touched by 9d0e78c).
3. Python API correctness: color parser (Python #1), `Worker.__enter__` (#2), list OOB
   `index_error` (#6) — each with a reproducer test first.
4. Bigger structural: `layout_flatten` split (UI #2), `materialize_table` split (core #6)
   — do these alone, one at a time, full suite after each.
