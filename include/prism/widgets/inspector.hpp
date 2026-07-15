#pragma once

#include <prism/widgets/field_mirror.hpp>
#include <prism/core/shared.hpp>
#include <prism/app/widget_tree.hpp>

#if __cpp_impl_reflection

namespace prism::inspector {
using namespace prism::core;

template <typename T>
struct Inspector {
    // source_ is a pointer, not a reference: WidgetTree::check_unplaced_fields (debug builds)
    // reflects over Model's own members to warn about unplaced Field/Derived/Shared fields.
    // A `Shared<T>&` member matches that reflection walk and hits a GCC identifier_of()
    // limitation on this template instantiation; a raw pointer member doesn't match
    // is_shared_v<M>, so the walk skips it entirely. Verified with a minimal repro.
    explicit Inspector(Shared<T>& source) : source_(&source) {
        mirror_.sync_from(source_->get());
        source_->observe([this](const T& v) { mirror_.sync_from(v); });
    }

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        mirror_.view(vb);
    }

    [[nodiscard]] FieldMirror<T>& mirror() { return mirror_; }
    [[nodiscard]] const FieldMirror<T>& mirror() const { return mirror_; }

private:
    Shared<T>* source_;

public:
    FieldMirror<T> mirror_;
};

} // namespace prism::inspector

#endif // __cpp_impl_reflection
