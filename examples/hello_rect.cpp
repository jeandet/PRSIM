#include <prism/prism.hpp>

#include <SDL3/SDL_keycode.h>

#include <array>

struct State {
    uint8_t color_index = 0;
};

static constexpr std::array colors = {
    prism::Color::rgba(0, 120, 215),
    prism::Color::rgba(215, 50, 50),
    prism::Color::rgba(50, 180, 50),
    prism::Color::rgba(200, 150, 0),
};

int main() {
    prism::app<State>("Interactive PRISM", State{},
        [](auto& ui) {
            ui.row([&] {
                // Left sidebar
                ui.frame().filled_rect({0, 0, 200, 100},
                    prism::Color::rgba(50, 50, 60));

                ui.spacer();

                // Right panel with colored rect
                ui.column([&] {
                    ui.frame().filled_rect({0, 0, 300, 150},
                        colors[ui->color_index]);
                    ui.spacer();
                    ui.frame().filled_rect({0, 0, 300, 80},
                        prism::Color::rgba(40, 40, 50));
                });
            });
        },
        [](State& s, const prism::InputEvent& ev) {
            if (auto* click = std::get_if<prism::MouseButton>(&ev);
                click && click->pressed) {
                s.color_index = (s.color_index + 1) % colors.size();
            }
        }
    );
}
