# model_plot

The smallest model-driven example. `PlotDemo` holds two sliders (frequency, amplitude), a
checkbox, and a `prism::plot::PlotModel`; `update_data()` recomputes a sine (and optionally
cosine) series and feeds it to the plot. Each control's `.observe()` callback, wired in the
`model_app` setup lambda, just calls `update_data()` again.

Demonstrates: `PlotModel` as a `canvas()` target with `.depends_on(...)` on its reactive fields
(`x_range`, `y_range`, `view`, `cursor`, `revision`), and driving a canvas purely from
`Field<Slider<>>`/`Field<Checkbox>` changes.

## Run

```bash
ninja -C builddir examples/model_plot/model_plot
./builddir/examples/model_plot/model_plot
```
