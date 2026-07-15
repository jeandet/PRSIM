#pragma once

#include <prism/widgets/field_mirror.hpp>
#include <prism/core/shared.hpp>
#include <prism/app/widget_tree.hpp>

#if __cpp_impl_reflection

namespace prism::inspector {
using namespace prism::core;

template <typename T>
struct Inspector {
    explicit Inspector(Shared<T>& source) : source_(source) {
        mirror_.sync_from(source_.get());
        source_.observe([this](const T& v) { mirror_.sync_from(v); });
    }

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        mirror_.view(vb);
    }

    [[nodiscard]] FieldMirror<T>& mirror() { return mirror_; }
    [[nodiscard]] const FieldMirror<T>& mirror() const { return mirror_; }

private:
    Shared<T>& source_;

public:
    FieldMirror<T> mirror_;
};

} // namespace prism::inspector

#endif // __cpp_impl_reflection
