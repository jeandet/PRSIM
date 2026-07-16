#pragma once

#include <prism/app/event_routing.hpp>
#include <prism/app/window.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/ui/window_chrome.hpp>

#include <memory>
#include <unordered_map>

namespace prism::app {
using namespace prism::ui;

class WindowRegistry {
public:
    struct Entry {
        Window* window;
        std::unique_ptr<WidgetTree> tree;
        std::shared_ptr<const SceneSnapshot> current_snap;
        int width = 0;
        int height = 0;
        uint64_t version = 0;
    };

    template <typename Model>
    WindowId add(Window& window, Model& model) {
        WindowId id = window.id();
        Entry e;
        e.window = &window;
        e.tree = std::make_unique<WidgetTree>(model);
        auto [w, h] = window.size();
        if (window.decoration_mode() == DecorationMode::Custom)
            h -= static_cast<int>(WindowChrome::title_bar_h.raw());
        e.width = w;
        e.height = h;
        entries_.emplace(id, std::move(e));
        return id;
    }

    void remove(WindowId id) { entries_.erase(id); }

    [[nodiscard]] Entry* find(WindowId id) {
        auto it = entries_.find(id);
        return it != entries_.end() ? &it->second : nullptr;
    }

    template <typename Fn>
    void for_each(Fn&& fn) {
        for (auto& [id, entry] : entries_) fn(id, entry);
    }

    template <typename Fn>
    void for_each_dirty(Fn&& fn) {
        for (auto& [id, entry] : entries_)
            if (entry.tree->any_dirty()) fn(id, entry);
    }

private:
    std::unordered_map<WindowId, Entry> entries_;
};

} // namespace prism::app
