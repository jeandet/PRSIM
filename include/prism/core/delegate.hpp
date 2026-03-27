#pragma once

#include <prism/core/draw_list.hpp>
#include <prism/core/field.hpp>
#include <prism/core/input_event.hpp>

#include <string>
#include <variant>

namespace prism {

// Primary template: default delegate for any Field<T>.
// Renders a label-only widget, ignores input.
template <typename T>
struct Delegate {
    static void record(DrawList& dl, const Field<T>& field) {
        auto label_text = std::string(field.label);
        dl.filled_rect({0, 0, 200, 30}, Color::rgba(50, 50, 60));
        dl.text(std::move(label_text), {4, 4}, 14, Color::rgba(220, 220, 220));
    }

    static void handle_input(Field<T>&, const InputEvent&) {}
};

// bool specialization: toggle widget
template <>
struct Delegate<bool> {
    static void record(DrawList& dl, const Field<bool>& field) {
        auto label_text = std::string(field.label);
        auto bg = field.get()
            ? Color::rgba(0, 120, 80)
            : Color::rgba(50, 50, 60);
        dl.filled_rect({0, 0, 200, 30}, bg);
        dl.text(std::move(label_text), {4, 4}, 14, Color::rgba(220, 220, 220));
    }

    static void handle_input(Field<bool>& field, const InputEvent& ev) {
        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed)
            field.set(!field.get());
    }
};

} // namespace prism
