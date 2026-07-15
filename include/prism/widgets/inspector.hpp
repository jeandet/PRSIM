#pragma once

#include <prism/widgets/field_mirror.hpp>
#include <prism/core/shared.hpp>
#include <prism/app/widget_tree.hpp>

#if __cpp_impl_reflection

namespace prism::inspector {
using namespace prism::core;

/// Inspector<T> reflects a Shared<T> value into a FieldMirror<T> for UI display.
///
/// WARNING: Lifetime requirement — `source` must outlive this Inspector. The
/// constructor registers a callback capturing `this` via source->observe(...).
/// Shared<T>::observe() is fire-and-forget with no way to disconnect on
/// destruction. If an Inspector<T> is destroyed while its `source` Shared<T>
/// stays alive, a later source.set(...) + drain would invoke the callback on
/// freed memory. Do not copy or move — copies would multiply this hazard.
template <typename T>
struct Inspector {
    // source_ is a pointer, not a reference: WidgetTree::check_unplaced_fields (debug builds)
    // reflects over Model's own members to warn about unplaced Field/Derived/Shared fields.
    // A `Shared<T>&` member matches that reflection walk and hits a GCC identifier_of()
    // limitation on this template instantiation; a raw pointer member doesn't match
    // is_shared_v<M>, so the walk skips it entirely. Verified with a minimal repro.
    explicit Inspector(Shared<T>& source) : source_(&source) {
        mirror_.sync_from(source_->get());
        mirror_.for_each_leaf([this](auto& field) {
            field.observe([this](const auto&) { push_local(); });
        });
        source_->observe([this](const T& v) {
            SyncGuard guard(syncing_);
            mirror_.sync_from(v);
        });
    }

    Inspector(const Inspector&) = delete;
    Inspector& operator=(const Inspector&) = delete;

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        mirror_.view(vb);
    }

    void drain() { source_->drain_notifications(); }

    [[nodiscard]] FieldMirror<T>& mirror() { return mirror_; }
    [[nodiscard]] const FieldMirror<T>& mirror() const { return mirror_; }

private:
    struct SyncGuard {
        bool& flag;
        explicit SyncGuard(bool& f) : flag(f) { flag = true; }
        ~SyncGuard() { flag = false; }
    };
    Shared<T>* source_;
    FieldMirror<T> mirror_;
    bool syncing_ = false;

    void push_local() {
        if (syncing_) return;
        source_->set(mirror_.build());
    }
};

} // namespace prism::inspector

#endif // __cpp_impl_reflection
