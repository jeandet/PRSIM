# hello_rect

Uses PRISM's second entry point, `prism::app<State>` — retained layout with manual composition,
as opposed to the model-driven `prism::model_app` used by every other example. There's no
`Field<T>`/reflection involved: the view lambda calls `ui.row()`/`ui.column()`/`ui.spacer()`
directly, and the update lambda mutates `State` by hand in response to raw `InputEvent`s.

A header bar, a sidebar of three nav items, a content area, and a footer — pressing `1`/`2`/`3`
highlights the corresponding nav item.

## Run

```bash
ninja -C builddir examples/hello_rect/hello_rect
./builddir/examples/hello_rect/hello_rect
```
