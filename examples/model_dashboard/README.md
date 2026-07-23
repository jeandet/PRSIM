# model_dashboard

The fullest tour of the widget set, built as a three-tab signal generator:

- **Waveform** — shape dropdown, frequency/amplitude sliders, a `canvas()` escape hatch drawing
  the live waveform (shared between the on-screen render and SVG export), and a stats label kept
  in sync via `.on_change()` connections.
- **Data** — a scrollable `table()` over `SignalTable`, a plain struct-less "computed columns"
  source that samples the current waveform per row.
- **Export** — a text field, button, `TextArea`, and a `List<std::string>` export log; the
  export button drives a `canvas()`-drawn progress bar through `prism::animate()` with a spring.

Demonstrates: `TabBar`/`vb.tabs()`, the canvas escape hatch (`.depends_on()` for repaint), a
custom table source, `Animation<T>`/`prism::animate()`, `List<T>`, and SVG export
(`prism::to_svg()`) from a `DrawList` built outside the normal render pass.

## Run

```bash
ninja -C builddir examples/model_dashboard/model_dashboard
./builddir/examples/model_dashboard/model_dashboard
```
