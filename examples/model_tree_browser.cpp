#include <prism/prism.hpp>
#include <prism/ui/tree.hpp>

#include <filesystem>
#include <unordered_map>
#include <vector>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace fs = std::filesystem;

// Tier 2 hand-written TreeSource adapter over std::filesystem: TreeNodeId is a hash of the
// path, children are enumerated lazily (only when a directory is expanded), matching the
// TreeStorage concept from include/prism/ui/tree.hpp.
class FileTreeSource {
public:
    explicit FileTreeSource(fs::path root) : root_(std::move(root)) {
        cache_path(root_);
    }

    size_t root_count() const { return 1; }
    prism::TreeNodeId root_at(size_t) const { return path_id(root_); }

    size_t child_count(prism::TreeNodeId id) const { return children_of(id).size(); }
    prism::TreeNodeId child_at(prism::TreeNodeId id, size_t i) const {
        return path_id(children_of(id)[i]);
    }
    std::string label(prism::TreeNodeId id) const { return by_id_.at(id).filename().string(); }
    bool has_children(prism::TreeNodeId id) const {
        std::error_code ec;
        return fs::is_directory(by_id_.at(id), ec);
    }
    std::optional<std::string> icon(prism::TreeNodeId id) const {
        std::error_code ec;
        return fs::is_directory(by_id_.at(id), ec) ? std::optional<std::string>{"\xef\x81\xbb"}  // Nerd Font folder
                                                    : std::optional<std::string>{"\xef\x85\x9b"}; // Nerd Font file
    }

private:
    prism::TreeNodeId path_id(const fs::path& p) const {
        auto id = static_cast<prism::TreeNodeId>(std::filesystem::hash_value(p));
        by_id_.emplace(id, p);
        return id;
    }
    void cache_path(const fs::path& p) const { path_id(p); }

    std::vector<fs::path> children_of(prism::TreeNodeId id) const {
        auto it = by_id_.find(id);
        if (it == by_id_.end()) return {};
        std::vector<fs::path> out;
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(it->second, ec)) {
            out.push_back(entry.path());
            cache_path(entry.path());
        }
        return out;
    }

    fs::path root_;
    mutable std::unordered_map<prism::TreeNodeId, fs::path> by_id_;
};

struct BrowserModel {
    FileTreeSource source{fs::current_path()};
    prism::TreeController ctrl{prism::wrap_tree_storage(source)};

    void view(prism::WidgetTree::ViewBuilder& vb) { vb.tree(ctrl); }
};

int main() {
    BrowserModel model;
    prism::model_app({.title = "PRISM Tree Browser -- Filesystem"}, model);
}
