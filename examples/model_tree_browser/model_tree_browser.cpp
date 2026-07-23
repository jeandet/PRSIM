#include <prism/prism.hpp>
#include "file_tree_source.hpp"

#include <filesystem>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

struct BrowserModel {
    FileTreeSource source{std::filesystem::current_path()};
    prism::TreeController ctrl{prism::wrap_tree_storage(source)};

    void view(prism::WidgetTree::ViewBuilder& vb) { vb.tree(ctrl); }
};

int main() {
    BrowserModel model;
    prism::model_app({.title = "PRISM Tree Browser -- Filesystem", .width = 900, .height = 600,
                       .decoration = prism::DecorationMode::Custom},
                      model);
}
