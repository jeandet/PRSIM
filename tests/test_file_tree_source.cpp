#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/file_tree_source.hpp"

#include <filesystem>
#include <fstream>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {

// Isolated scratch directory: one known file (6 bytes) and one empty subdirectory,
// so attributes() has a deterministic size/entry-count to assert against.
struct TempDir {
    std::filesystem::path path
        = std::filesystem::temp_directory_path() / "prism_file_tree_source_test";

    TempDir() {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path / "subdir");
        std::ofstream(path / "hello.txt") << "hello!"; // 6 bytes, no trailing newline
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};

prism::TreeNodeId child_named(const FileTreeSource& src, prism::TreeNodeId parent,
                               std::string_view name) {
    size_t n = src.child_count(parent);
    for (size_t i = 0; i < n; ++i) {
        auto id = src.child_at(parent, i);
        if (src.label(id) == name) return id;
    }
    FAIL("no child named ", name);
    return 0;
}

} // namespace

TEST_CASE("FileTreeSource.attributes() reports type and size for a file") {
    TempDir tmp;
    FileTreeSource src(tmp.path);
    auto file_id = child_named(src, src.root_at(0), "hello.txt");

    auto attrs = src.attributes(file_id);

    REQUIRE(attrs.size() >= 2);
    CHECK(attrs[0] == std::pair<std::string, std::string>{"type", "file"});
    CHECK(attrs[1] == std::pair<std::string, std::string>{"size", "6 bytes"});
}

TEST_CASE("FileTreeSource.attributes() reports type and entry count for a directory") {
    TempDir tmp;
    FileTreeSource src(tmp.path);
    auto dir_id = child_named(src, src.root_at(0), "subdir");

    auto attrs = src.attributes(dir_id);

    REQUIRE(attrs.size() >= 2);
    CHECK(attrs[0] == std::pair<std::string, std::string>{"type", "directory"});
    CHECK(attrs[1] == std::pair<std::string, std::string>{"entries", "0"});
}

TEST_CASE("wrap_tree_storage wires FileTreeSource::attributes into TreeSource") {
    TempDir tmp;
    FileTreeSource src(tmp.path);
    auto tree_source = prism::wrap_tree_storage(src);

    REQUIRE(tree_source.attributes);
    auto attrs = tree_source.attributes(tree_source.root_at(0));
    REQUIRE(!attrs.empty());
    CHECK(attrs[0] == std::pair<std::string, std::string>{"type", "directory"});
}
