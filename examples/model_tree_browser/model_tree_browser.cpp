#include <prism/prism.hpp>
#include "file_tree_source.hpp"
#include "../showcase/showcase_common.hpp"

#include <filesystem>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

struct BrowserModel {
    FileTreeSource source;
    prism::TreeController ctrl{prism::wrap_tree_storage(source)};

    explicit BrowserModel(std::filesystem::path root = std::filesystem::current_path())
        : source(std::move(root)) {}

    void view(prism::WidgetTree::ViewBuilder& vb) { vb.tree(ctrl); }
};

int main(int argc, char* argv[]) {
    if (argc >= 2) {
        // Browse a fixed, meaningful directory for the screenshot rather than whatever
        // cwd the build happens to invoke this from -- and expand the root once (via the
        // same public API a real row click uses) so the capture shows more than a single
        // collapsed line.
        BrowserModel model{argc >= 3 ? std::filesystem::path(argv[2])
                                     : std::filesystem::current_path()};
        if (model.ctrl.rows.size() > 0)
            model.ctrl.on_row_clicked(0, model.ctrl.rows[0]);
        return showcase(argc, argv, model, 900, 600);
    }

    BrowserModel model;
    prism::model_app({.title = "PRISM Tree Browser -- Filesystem", .width = 900, .height = 600,
                       .decoration = prism::DecorationMode::Custom},
                      model);
}
