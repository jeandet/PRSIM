#pragma once

#include <prism/prism.hpp>
#include <prism/app/capturing_backend.hpp>
#include <prism/render/svg_export.hpp>

#include <fstream>
#include <iostream>
#include <string>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

// Run model_app headlessly, capture snapshot, write SVG to argv[1].
template <typename Model>
int showcase(int argc, char* argv[], Model& model, int w = 400, int h = 300) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <output.svg>\n";
        return 1;
    }

    std::shared_ptr<const prism::SceneSnapshot> snap;
    auto backend = prism::Backend{std::make_unique<prism::CapturingBackend>(snap)};
    auto& window = backend.create_window({.title = "showcase", .width = w, .height = h});
    prism::model_app(backend, window, model);

    if (!snap) {
        std::cerr << "No snapshot captured\n";
        return 1;
    }

    std::ofstream out(argv[1]);
    out << prism::to_svg(*snap, static_cast<float>(w), static_cast<float>(h));
    return 0;
}
