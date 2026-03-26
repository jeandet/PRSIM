#include <prism/prism.hpp>

int main() {
    prism::App app({.title = "Hello PRISM", .width = 800, .height = 600});

    app.run([](prism::Frame& frame) {
        // Background
        frame.filled_rect(
            {0, 0, static_cast<float>(frame.width()), static_cast<float>(frame.height())},
            prism::Color::rgba(30, 30, 40));

        // Centered colored rectangle
        float rw = 200, rh = 100;
        float rx = (frame.width() - rw) / 2;
        float ry = (frame.height() - rh) / 2;
        frame.filled_rect({rx, ry, rw, rh}, prism::Color::rgba(0, 120, 215));

        // Smaller accent rect
        frame.filled_rect({rx + 20, ry + 20, 60, 30}, prism::Color::rgba(255, 180, 0));
    });
}
