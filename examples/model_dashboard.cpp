#include <prism/prism.hpp>

#include <fmt/format.h>
#include <array>
#include <cmath>
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

// --- Waveform editor (Tab 1) ---

struct Waveform {
    prism::Field<WaveShape> shape{WaveShape::Sine};
    prism::Field<prism::Slider<>> frequency{{.value = 2.0, .min = 0.1, .max = 10.0}};
    prism::Field<prism::Slider<double, prism::Orientation::Vertical>> amplitude{
        {.value = 0.8, .min = 0.0, .max = 1.0}};
    prism::Field<prism::Checkbox> harmonics{{.checked = false, .label = "Show harmonics"}};
    prism::Field<prism::Label<>> stats{{""}};

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        auto w = bounds.extent.w.raw();
        auto h = bounds.extent.h.raw();
        float cy = h * 0.5f;
        auto sh = shape.get();
        float freq = static_cast<float>(frequency.get().value);
        float amp = static_cast<float>(amplitude.get().value);
        bool show_harm = harmonics.get().checked;

        dl.filled_rect(bounds, prism::Color::rgba(20, 22, 30));

        // Center line
        dl.filled_rect(
            prism::Rect{prism::Point{prism::X{0}, prism::Y{cy}},
                        prism::Size{prism::Width{w}, prism::Height{1}}},
            prism::Color::rgba(60, 60, 80));

        int steps = std::max(2, static_cast<int>(w));
        auto sample = [&](int i) -> float {
            float t = static_cast<float>(i) / static_cast<float>(steps);
            float phase = std::fmod(freq * t, 1.f);
            float y_val = amp * wave_value(sh, phase);
            if (show_harm) {
                y_val += amp * 0.33f * wave_value(sh, std::fmod(3.f * freq * t, 1.f));
                y_val += amp * 0.2f  * wave_value(sh, std::fmod(5.f * freq * t, 1.f));
                y_val = std::clamp(y_val, -1.f, 1.f);
            }
            return cy - y_val * h * 0.45f;
        };

        auto color = prism::Color::rgba(0, 220, 100);
        constexpr float thickness = 2.f;
        float prev_y = sample(0);
        for (int i = 1; i < steps; ++i) {
            float x0 = static_cast<float>(i - 1);
            float x1 = static_cast<float>(i);
            float y0 = prev_y;
            float y1 = sample(i);
            prev_y = y1;

            float min_y = std::min(y0, y1);
            float max_y = std::max(y0, y1);
            float seg_h = std::max(max_y - min_y, thickness);
            dl.filled_rect(
                prism::Rect{
                    prism::Point{prism::X{x0}, prism::Y{min_y - thickness * 0.5f}},
                    prism::Size{prism::Width{x1 - x0 + 1.f}, prism::Height{seg_h + thickness}}},
                color);
        }
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

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        dl.filled_rect(bounds, prism::Color::rgba(40, 42, 54));
        float fill_w = bounds.extent.w.raw() * std::clamp(progress.get(), 0.f, 1.f);
        if (fill_w > 0.f) {
            dl.filled_rect(
                prism::Rect{bounds.origin,
                            prism::Size{prism::Width{fill_w}, bounds.extent.h}},
                prism::Color::rgba(0, 160, 220));
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
