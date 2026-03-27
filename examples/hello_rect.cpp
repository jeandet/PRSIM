#include <prism/prism.hpp>

#include <SDL3/SDL_keycode.h>

#include <array>

struct State {
    float x = 300, y = 250;
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
            auto& f = ui.frame();
            f.filled_rect(
                {0, 0, static_cast<float>(f.width()), static_cast<float>(f.height())},
                prism::Color::rgba(30, 30, 40));
            f.filled_rect({ui->x, ui->y, 200, 100}, colors[ui->color_index]);
        },
        [](State& s, const prism::InputEvent& ev) {
            if (auto* click = std::get_if<prism::MouseButton>(&ev);
                click && click->pressed) {
                s.color_index = (s.color_index + 1) % colors.size();
            }
            if (auto* key = std::get_if<prism::KeyPress>(&ev)) {
                constexpr float step = 20.f;
                switch (key->key) {
                case SDLK_RIGHT: s.x += step; break;
                case SDLK_LEFT:  s.x -= step; break;
                case SDLK_DOWN:  s.y += step; break;
                case SDLK_UP:    s.y -= step; break;
                default: break;
                }
            }
        }
    );
}
