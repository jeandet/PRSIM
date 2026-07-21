#pragma once

#include <prism/ui/tree.hpp>

#include <chrono>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Tier 2 hand-written TreeSource adapter over std::filesystem: TreeNodeId is a hash of the
// path, children are enumerated lazily (only when a directory is expanded), matching the
// TreeStorage concept from include/prism/ui/tree.hpp.
class FileTreeSource {
public:
    explicit FileTreeSource(std::filesystem::path root) : root_(std::move(root)) {
        cache_path(root_);
    }

    size_t root_count() const { return 1; }
    prism::ui::TreeNodeId root_at(size_t) const { return path_id(root_); }

    size_t child_count(prism::ui::TreeNodeId id) const { return children_of(id).size(); }
    prism::ui::TreeNodeId child_at(prism::ui::TreeNodeId id, size_t i) const {
        return path_id(children_of(id)[i]);
    }
    std::string label(prism::ui::TreeNodeId id) const { return by_id_.at(id).filename().string(); }
    bool has_children(prism::ui::TreeNodeId id) const {
        std::error_code ec;
        return std::filesystem::is_directory(by_id_.at(id), ec);
    }
    std::optional<std::string> icon(prism::ui::TreeNodeId id) const {
        std::error_code ec;
        return std::filesystem::is_directory(by_id_.at(id), ec)
                   ? std::optional<std::string>{"\xef\x81\xbb"}  // Nerd Font folder
                   : std::optional<std::string>{"\xef\x85\x9b"}; // Nerd Font file
    }

    // Feeds the tree widget's detail panel (TreeController::populate_detail) when this
    // node is selected.
    std::vector<std::pair<std::string, std::string>> attributes(prism::ui::TreeNodeId id) const {
        auto& path = by_id_.at(id);
        std::error_code ec;
        std::vector<std::pair<std::string, std::string>> attrs;

        bool is_dir = std::filesystem::is_directory(path, ec);
        attrs.emplace_back("type", is_dir ? "directory" : "file");
        if (is_dir) {
            attrs.emplace_back("entries", fmt::to_string(children_of(id).size()));
        } else {
            auto size = std::filesystem::file_size(path, ec);
            attrs.emplace_back("size", ec ? "?" : fmt::format("{} bytes", size));
        }

        auto mtime = std::filesystem::last_write_time(path, ec);
        if (!ec) {
            auto sys_time = std::chrono::clock_cast<std::chrono::system_clock>(mtime);
            attrs.emplace_back("modified",
                fmt::format("{:%Y-%m-%d %H:%M:%S}",
                            std::chrono::time_point_cast<std::chrono::seconds>(sys_time)));
        }
        return attrs;
    }

private:
    prism::ui::TreeNodeId path_id(const std::filesystem::path& p) const {
        auto id = static_cast<prism::ui::TreeNodeId>(std::filesystem::hash_value(p));
        by_id_.emplace(id, p);
        return id;
    }
    void cache_path(const std::filesystem::path& p) const { path_id(p); }

    std::vector<std::filesystem::path> children_of(prism::ui::TreeNodeId id) const {
        auto it = by_id_.find(id);
        if (it == by_id_.end()) return {};
        std::vector<std::filesystem::path> out;
        std::error_code ec;
        for (auto& entry : std::filesystem::directory_iterator(it->second, ec)) {
            out.push_back(entry.path());
            cache_path(entry.path());
        }
        return out;
    }

    std::filesystem::path root_;
    mutable std::unordered_map<prism::ui::TreeNodeId, std::filesystem::path> by_id_;
};
