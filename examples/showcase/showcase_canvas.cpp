#include "showcase_common.hpp"

#include <cmath>
#include <numbers>

struct Waveform {
    prism::Field<prism::Slider<>> frequency{{.value = 3.0, .min = 0.5, .max = 8.0}};

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode& node) {
        auto& t = *node.theme;
        float w = bounds.extent.w.raw();
        float h = bounds.extent.h.raw();
        float cy = h * 0.5f;
        float freq = static_cast<float>(frequency.get().value);

        dl.rounded_rect(bounds, t.surface, 6.f);
        dl.line({prism::X{0}, prism::Y{cy}}, {prism::X{w}, prism::Y{cy}}, t.track, 1.f);

        int steps = static_cast<int>(w);
        std::vector<prism::Point> pts(static_cast<size_t>(steps));
        for (int i = 0; i < steps; ++i) {
            float x = static_cast<float>(i);
            float phase = freq * x / w;
            float y = cy - 0.4f * h * std::sin(2.f * std::numbers::pi_v<float> * phase);
            pts[static_cast<size_t>(i)] = {prism::X{x}, prism::Y{y}};
        }
        dl.polyline(pts, t.accent, 2.f);
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.canvas(*this).depends_on(frequency);
        vb.widget(frequency);
    }
};

int main(int argc, char* argv[]) {
    Waveform model;
    return showcase(argc, argv, model, 400, 250);
}
