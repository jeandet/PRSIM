#include <prism/prism.hpp>
#include <prism/render/svg_export.hpp>

#include <fmt/format.h>
#include <array>
#include <cmath>
#include <fstream>
#include <string>

// --- Domain types ---

enum class WaveShape { Sine, Square, Triangle, Sawtooth };

static float wave_value(WaveShape shape, float phase) {
    constexpr float pi = 3.14159265f;
    switch (shape) {
        case WaveShape::Sine:
            return std::sin(2.f * pi * phase);
        case WaveShape::Square:
            return phase < 0.5f ? 1.f : -1.f;
        case WaveShape::Triangle:
            return 4.f * std::abs(phase - 0.5f) - 1.f;
        case WaveShape::Sawtooth:
            return 2.f * phase - 1.f;
    }
    return 0.f;
}

static float rms_value(WaveShape shape) {
    switch (shape) {
        case WaveShape::Sine:     return 1.f / std::sqrt(2.f);
        case WaveShape::Square:   return 1.f;
        case WaveShape::Triangle: return 1.f / std::sqrt(3.f);
        case WaveShape::Sawtooth: return 1.f / std::sqrt(3.f);
    }
    return 0.f;
}

// --- Waveform drawing (shared by canvas + SVG export) ---

struct WaveParams {
    WaveShape shape;
    float freq;
    float amp;
    bool harmonics;
};

static void draw_waveform(prism::DrawList& dl, prism::Rect bounds, WaveParams p,
                           prism::Color bg, prism::Color track,
                           prism::Color curve, prism::Color marker) {
    auto w = bounds.extent.w.raw();
    auto h = bounds.extent.h.raw();
    float cy = h * 0.5f;

    dl.rounded_rect(bounds, bg, 6.f);

    dl.line(prism::Point{prism::X{0}, prism::Y{cy}},
            prism::Point{prism::X{w}, prism::Y{cy}},
            track, 1.f);

    int steps = std::max(2, static_cast<int>(w));
    auto sample = [&](float x_pos) -> float {
        float norm = x_pos / w;
        float phase = std::fmod(p.freq * norm, 1.f);
        float y_val = p.amp * wave_value(p.shape, phase);
        if (p.harmonics) {
            y_val += p.amp * 0.33f * wave_value(p.shape, std::fmod(3.f * p.freq * norm, 1.f));
            y_val += p.amp * 0.2f  * wave_value(p.shape, std::fmod(5.f * p.freq * norm, 1.f));
            y_val = std::clamp(y_val, -1.f, 1.f);
        }
        return cy - y_val * h * 0.45f;
    };

    std::vector<prism::Point> pts(static_cast<size_t>(steps));
    for (int i = 0; i < steps; ++i) {
        float x = static_cast<float>(i);
        pts[static_cast<size_t>(i)] = {prism::X{x}, prism::Y{sample(x)}};
    }
    dl.polyline(pts, curve, 2.f);

    constexpr int marker_every = 50;
    for (int i = 0; i < steps; i += marker_every) {
        float x = static_cast<float>(i);
        float y = sample(x);
        if (std::abs(y - cy) > h * 0.3f)
            dl.circle(prism::Point{prism::X{x}, prism::Y{y}}, 3.f, marker);
    }
}

// --- Waveform editor (Tab 1) ---

struct Waveform {
    prism::Field<WaveShape> shape{WaveShape::Sine};
    prism::Field<prism::Slider<>> frequency{{.value = 2.0, .min = 0.1, .max = 10.0}};
    prism::Field<prism::Slider<double, prism::Orientation::Vertical>> amplitude{
        {.value = 0.8, .min = 0.0, .max = 1.0}};
    prism::Field<prism::Checkbox> harmonics{{.checked = false, .label = "Show harmonics"}};
    prism::Field<prism::Label<>> stats{{""}};
    prism::Field<prism::Button> export_svg{{"Export SVG"}};

    WaveParams current_params() const {
        return {shape.get(),
                static_cast<float>(frequency.get().value),
                static_cast<float>(amplitude.get().value),
                harmonics.get().checked};
    }

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode& node) {
        auto& t = *node.theme;
        draw_waveform(dl, bounds, current_params(), t.surface, t.track, t.accent, t.accent_hover);
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.widget(amplitude);
            vb.canvas(*this)
                .depends_on(frequency).depends_on(amplitude)
                .depends_on(shape).depends_on(harmonics);
        });
        vb.widget(frequency);
        vb.hstack([&] {
            vb.widget(shape);
            vb.widget(harmonics);
            vb.widget(export_svg);
        });
        vb.widget(stats);
    }
};

// --- Signal data table (Tab 2) ---

struct SignalTable {
    static constexpr size_t N = 50;
    const Waveform& wf;

    size_t column_count() const { return 5; }
    size_t row_count() const { return N; }
    std::string_view header(size_t c) const {
        static constexpr std::array<const char*, 5> h = {
            "Sample", "Time", "Value", "Abs", "Phase"};
        return h[c];
    }
    std::string cell_text(size_t r, size_t c) const {
        auto sh = wf.shape.get();
        float freq = static_cast<float>(wf.frequency.get().value);
        float amp = static_cast<float>(wf.amplitude.get().value);
        float t = static_cast<float>(r) / static_cast<float>(N);
        float phase = std::fmod(freq * t, 1.f);
        float val = amp * wave_value(sh, phase);
        switch (c) {
            case 0: return fmt::to_string(r);
            case 1: return fmt::format("{:.3f}", t);
            case 2: return fmt::format("{:+.4f}", val);
            case 3: return fmt::format("{:.4f}", std::abs(val));
            case 4: return fmt::format("{:.1f}", std::fmod(360.0f * freq * t, 360.0f));
            default: return "";
        }
    }
};

// --- Export progress bar ---

struct ProgressBar {
    prism::Field<float> progress{0.f};

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode& node) {
        auto& t = *node.theme;
        dl.rounded_rect(bounds, t.surface, 4.f);
        dl.rounded_rect(bounds, t.border, 4.f, 1.f);
        float fill_w = bounds.extent.w.raw() * std::clamp(progress.get(), 0.f, 1.f);
        if (fill_w > 0.f) {
            dl.rounded_rect(
                prism::Rect{bounds.origin,
                            prism::Size{prism::Width{fill_w}, bounds.extent.h}},
                t.accent_hover, 4.f);
        }
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.canvas(*this).depends_on(progress);
    }
};

// --- Root model ---

struct SignalGenerator {
    Waveform waveform;
    SignalTable signal_table{.wf = waveform};
    ProgressBar export_progress;

    prism::Field<prism::TextField<>> filename{{.value = "signal_001", .placeholder = "Filename..."}};
    prism::Field<prism::Button> generate{{"Export"}};
    prism::Field<prism::TextArea<>> notes{{.placeholder = "Annotations...", .rows = 4}};
    prism::List<std::string> export_log;
    prism::Field<prism::TabBar<>> tabs;

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.tabs(tabs, [&] {
            vb.tab("Waveform", [&](prism::WidgetTree::ViewBuilder& tvb) {
                tvb.component(waveform);
            });
            vb.tab("Data", [&](prism::WidgetTree::ViewBuilder& tvb) {
                tvb.scroll([&] {
                    tvb.table(signal_table)
                        .depends_on(waveform.frequency)
                        .depends_on(waveform.amplitude)
                        .depends_on(waveform.shape);
                });
            });
            vb.tab("Export", [&](prism::WidgetTree::ViewBuilder& tvb) {
                tvb.vstack(filename, generate);
                tvb.component(export_progress);
                tvb.widget(notes);
                tvb.list(export_log);
            });
        });
    }
};

// --- Wiring ---

static std::string shape_name(WaveShape s) {
    switch (s) {
        case WaveShape::Sine:     return "sine";
        case WaveShape::Square:   return "square";
        case WaveShape::Triangle: return "triangle";
        case WaveShape::Sawtooth: return "sawtooth";
    }
    return "?";
}

int main() {
    SignalGenerator app;
    std::vector<prism::Connection> connections;
    prism::Animation<float> export_anim;
    int export_count = 0;

    prism::model_app({.title = "PRISM Signal Generator", .width = 1024, .height = 768,
                       .decoration = prism::DecorationMode::Custom},
                     app, [&](prism::AppContext& ctx) {
        auto sched = ctx.scheduler();

        // Update stats label when waveform params change.
        // Note: update_stats is local to this setup closure, so then() callbacks
        // must capture it BY VALUE — capturing by [&] would dangle after setup returns.
        auto update_stats = [&app] {
            auto sh = app.waveform.shape.get();
            float amp = static_cast<float>(app.waveform.amplitude.get().value);
            float freq = static_cast<float>(app.waveform.frequency.get().value);
            float rms = amp * rms_value(sh);
            app.waveform.stats.set({fmt::format("Pk-Pk: {:.2f}  RMS: {:.3f}  f={:.1f} Hz",
                          2.f * amp, rms, freq)});
        };

        connections.push_back(
            app.waveform.frequency.on_change()
            | prism::on(sched)
            | prism::then([update_stats](const prism::Slider<>&) { update_stats(); })
        );
        connections.push_back(
            app.waveform.amplitude.on_change()
            | prism::on(sched)
            | prism::then([update_stats](const prism::Slider<double, prism::Orientation::Vertical>&) {
                  update_stats();
              })
        );
        connections.push_back(
            app.waveform.shape.on_change()
            | prism::on(sched)
            | prism::then([update_stats](const WaveShape&) { update_stats(); })
        );

        update_stats();

        // SVG export button: render waveform to SVG file
        connections.push_back(
            app.waveform.export_svg.on_change()
            | prism::on(sched)
            | prism::then([&app](const prism::Button&) {
                  prism::DrawList dl;
                  prism::Rect bounds{prism::Point{prism::X{0}, prism::Y{0}},
                                     prism::Size{prism::Width{800}, prism::Height{300}}};
                  draw_waveform(dl, bounds, app.waveform.current_params(),
                                prism::Color::rgba(30, 30, 46),
                                prism::Color::rgba(88, 91, 112),
                                prism::Color::rgba(137, 180, 250),
                                prism::Color::rgba(166, 209, 255));
                  auto svg = prism::to_svg(dl);
                  auto fname = app.filename.get().value + ".svg";
                  std::ofstream(fname) << svg;
                  app.export_log.push_back("SVG exported -> " + fname);
              })
        );

        // Export button: log entry + animated progress bar.
        // Capture window/clock by pointer — they live inside model_app's stack frame.
        auto& win = ctx.window();
        auto& clock = ctx.clock();
        connections.push_back(
            app.generate.on_change()
            | prism::on(sched)
            | prism::then([&app, &export_count, &export_anim, &win, &clock](const prism::Button&) {
                  ++export_count;
                  auto sh = app.waveform.shape.get();
                  float freq = static_cast<float>(app.waveform.frequency.get().value);
                  float amp = static_cast<float>(app.waveform.amplitude.get().value);
                  auto fname = app.filename.get().value;

                  app.export_log.push_back(fmt::format(
                      "#{} {} {}Hz amp={} -> {}.wav",
                      export_count, shape_name(sh), freq, amp, fname));

                  win.set_title("Signal Generator — exported " + fname);

                  app.export_progress.progress.set(0.f);
                  export_anim = prism::animate(clock,
                      app.export_progress.progress, 1.f,
                      prism::SpringConfig{.spring = {.stiffness = 80.f, .damping = 10.f}});
              })
        );
    });
}
