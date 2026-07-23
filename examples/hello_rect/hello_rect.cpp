#include <prism/prism.hpp>
#include <prism/app/capturing_backend.hpp>
#include <prism/render/svg_export.hpp>

#include <SDL3/SDL_keycode.h>
#include <fstream>
#include <iostream>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}



struct State {
    int selected_panel = 0;
};

static constexpr auto bg       = prism::Color::rgba(30, 30, 40);
static constexpr auto sidebar  = prism::Color::rgba(45, 45, 55);
static constexpr auto header   = prism::Color::rgba(0, 120, 215);
static constexpr auto footer   = prism::Color::rgba(50, 50, 60);
static constexpr auto accent   = prism::Color::rgba(0, 180, 120);
static constexpr auto muted    = prism::Color::rgba(60, 60, 75);
static constexpr auto panel_bg = prism::Color::rgba(38, 38, 50);

namespace {
prism::Rect R(float x, float y, float w, float h) {
    return {prism::Point{prism::X{x}, prism::Y{y}}, prism::Size{prism::Width{w}, prism::Height{h}}};
}
}

void nav_item(auto& ui, float h, prism::Color c) {
    ui.frame().filled_rect(R(4, 4, 192, h - 8), c);
}

void content_card(auto& ui, float w, float h, prism::Color c) {
    ui.frame().filled_rect(R(8, 8, w - 16, h - 16), c);
}

int main(int argc, char* argv[]) {
    auto view = [](auto& ui) {
        // Root: full-window column (header / body / footer)
        ui.column([&] {
            // Header bar
            ui.frame().filled_rect(R(0, 0, 100, 48), header);

            // Body: sidebar | content
            ui.row([&] {
                // Sidebar: fixed-width column of nav items
                ui.column([&] {
                    nav_item(ui, 50, ui->selected_panel == 0 ? accent : muted);
                    nav_item(ui, 50, ui->selected_panel == 1 ? accent : muted);
                    nav_item(ui, 50, ui->selected_panel == 2 ? accent : muted);
                    ui.spacer();
                    nav_item(ui, 40, sidebar);
                });

                // Content area: stretches to fill
                ui.spacer();
            });

            // Footer bar
            ui.frame().filled_rect(R(0, 0, 100, 32), footer);
        });
    };

    auto update = [](State& s, const prism::InputEvent& ev) {
        if (auto* key = std::get_if<prism::KeyPress>(&ev)) {
            switch (key->key) {
            case SDLK_1: s.selected_panel = 0; break;
            case SDLK_2: s.selected_panel = 1; break;
            case SDLK_3: s.selected_panel = 2; break;
            default: break;
            }
        }
    };

    // Headless SVG capture (`hello_rect <output.svg>`): app<State>() has its own
    // Backend&/Window&-injectable overload, mirroring model_app's -- there's no shared
    // showcase() helper for it since its shape (State + view + update, not a single
    // .view()-able model object) doesn't match model_app's.
    if (argc >= 2) {
        std::shared_ptr<const prism::SceneSnapshot> snap;
        auto backend = prism::Backend{std::make_unique<prism::CapturingBackend>(snap)};
        auto& window = backend.create_window({.title = "hello_rect", .width = 800, .height = 500});
        prism::app::app<State>(backend, window, State{}, view, update);

        if (!snap) {
            std::cerr << "No snapshot captured\n";
            return 1;
        }
        std::ofstream(argv[1]) << prism::to_svg(*snap, 800.f, 500.f);
        return 0;
    }

    prism::app::app<State>("PRISM Layout Demo", State{}, view, update);
}
