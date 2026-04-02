#include <prism/prism.hpp>
#include <prism/widgets/plot.hpp>

#include <cmath>
#include <numbers>
#include <vector>

struct PlotDemo {
    prism::Field<prism::Slider<>> frequency{{.value = 2.0, .min = 0.1, .max = 10.0}};
    prism::Field<prism::Slider<>> amplitude{{.value = 1.0, .min = 0.1, .max = 5.0}};
    prism::Field<prism::Checkbox> show_cos{{.checked = false, .label = "Show cosine"}};
    prism::plot::PlotModel plot;

    void update_data()
    {
        double f = frequency.get().value;
        double a = amplitude.get().value;
        constexpr int N = 500;

        std::vector<double> xs(N), ys_sin(N), ys_cos(N);
        for (int i = 0; i < N; ++i) {
            double t = static_cast<double>(i) / (N - 1) * 4.0 * std::numbers::pi;
            xs[i] = t;
            ys_sin[i] = a * std::sin(f * t);
            ys_cos[i] = a * std::cos(f * t);
        }

        auto colors = prism::plot::default_series_colors(prism::default_theme());

        plot.clear_series();
        plot.add_series(prism::plot::XYData{xs, ys_sin},
                        prism::plot::SeriesStyle{colors[0], 2.f});
        if (show_cos.get().checked) {
            plot.add_series(prism::plot::XYData{std::move(xs), std::move(ys_cos)},
                            prism::plot::SeriesStyle{colors[2], 2.f});
        }
        plot.x_label.set("Time (rad)");
        plot.y_label.set("Amplitude");
        plot.notify();
    }

    void view(prism::WidgetTree::ViewBuilder& vb)
    {
        vb.canvas(plot)
            .depends_on(plot.x_range)
            .depends_on(plot.y_range)
            .depends_on(plot.view)
            .depends_on(plot.cursor)
            .depends_on(plot.revision);
        vb.widget(frequency);
        vb.widget(amplitude);
        vb.widget(show_cos);
    }
};

int main()
{
    PlotDemo demo;
    demo.update_data();

    prism::model_app({.title = "Plot Demo", .width = 800, .height = 600},
                     demo, [&](prism::AppContext& /*ctx*/) {
        demo.frequency.observe([&](const prism::Slider<>&) { demo.update_data(); });
        demo.amplitude.observe([&](const prism::Slider<>&) { demo.update_data(); });
        demo.show_cos.observe([&](const prism::Checkbox&) { demo.update_data(); });
    });
}
