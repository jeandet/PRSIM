#include <prism/prism.hpp>

#include <cmath>
#include <iostream>
#include <string>

enum class Theme { Light, Dark, System };

struct Settings {
    prism::Field<std::string> username{"jeandet"};
    prism::Field<bool> dark_mode{true};
    prism::Field<prism::Checkbox> notifications{{.checked = true, .label = "Enable notifications"}};
    prism::Field<prism::Slider<>> volume{{.value = 0.7}};
    prism::Field<prism::TextField<>> search{{.value = "", .placeholder = "Search..."}};
    prism::Field<Theme> theme{Theme::Dark};
    prism::Field<prism::Password<>> api_key{{.placeholder = "API key"}};
};

struct Waveform {
    prism::Field<prism::Slider<>> frequency{{.value = 2.0, .min = 0.5, .max = 10.0}};
    prism::Field<prism::Slider<>> amplitude{{.value = 0.8, .min = 0.0, .max = 1.0}};

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        auto w = bounds.extent.w.raw();
        auto h = bounds.extent.h.raw();

        // Background
        dl.filled_rect(bounds, prism::Color::rgba(20, 22, 30));

        // Center line
        float cy = h * 0.5f;
        dl.filled_rect(
            prism::Rect{prism::Point{prism::X{0}, prism::Y{cy}},
                        prism::Size{prism::Width{w}, prism::Height{1}}},
            prism::Color::rgba(60, 60, 80));

        // Sine wave as vertical bars
        float freq = static_cast<float>(frequency.get().value);
        float amp = static_cast<float>(amplitude.get().value);
        int steps = std::max(1, static_cast<int>(w / 3));
        float bar_w = w / static_cast<float>(steps);

        for (int i = 0; i < steps; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(steps);
            float y_val = amp * std::sin(2.0f * 3.14159265f * freq * t);
            float bar_h = std::abs(y_val) * h * 0.45f;
            float bar_y = y_val > 0 ? cy - bar_h : cy;

            auto green = static_cast<uint8_t>(80 + 175 * std::abs(y_val));
            dl.filled_rect(
                prism::Rect{
                    prism::Point{prism::X{static_cast<float>(i) * bar_w}, prism::Y{bar_y}},
                    prism::Size{prism::Width{std::max(bar_w - 1, 1.0f)},
                                prism::Height{bar_h}}},
                prism::Color::rgba(0, green, 80));
        }
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.widget(frequency);
            vb.widget(amplitude);
        });
        vb.canvas(*this).depends_on(frequency).depends_on(amplitude);
    }
};

struct Dashboard {
    Settings settings;
    Waveform waveform;
    prism::Field<prism::Label<>> status{{"All systems go"}};
    prism::Field<prism::TextArea<>> notes{{.placeholder = "Notes...", .rows = 4}};
    prism::Field<prism::Button> increment{{"Increment"}};
    prism::Field<int> counter{0};
    prism::State<int> request_count{0};
};

int main() {
    Dashboard dashboard;
    std::vector<prism::Connection> connections;

    prism::model_app("PRISM Model Dashboard", dashboard, [&](prism::AppContext& ctx) {
        auto sched = ctx.scheduler();

        // Cross-component wiring: volume change updates status label (on app thread)
        connections.push_back(
            dashboard.settings.volume.on_change()
            | prism::on(sched)
            | prism::then([&](const prism::Slider<>& s) {
                  std::cout << "Volume: " << s.value << "\n";
                  if (s.value > 0.9)
                      dashboard.status.set({"Volume is very high!"});
              })
        );

        // Button click increments counter
        connections.push_back(
            dashboard.increment.on_change()
            | prism::on(sched)
            | prism::then([&](const prism::Button&) {
                  dashboard.counter.set(dashboard.counter.get() + 1);
                  std::cout << "Counter: " << dashboard.counter.get() << "\n";
              })
        );

        dashboard.settings.dark_mode.observe([](const bool& v) {
            std::cout << "Dark mode: " << (v ? "ON" : "OFF") << "\n";
        });

        dashboard.settings.notifications.observe([](const prism::Checkbox& cb) {
            std::cout << "Notifications: " << (cb.checked ? "ON" : "OFF") << "\n";
        });
    });
}
