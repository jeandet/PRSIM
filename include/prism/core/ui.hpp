#pragma once

#include <prism/core/app.hpp>
#include <prism/core/backend.hpp>
#include <prism/core/exec.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/layout.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace prism {

template <typename State>
using UpdateFn = std::function<void(State&, const InputEvent&)>;

template <typename State>
class Ui {
public:
    const State* operator->() const { return state_; }
    const State& state() const { return *state_; }

    Frame& frame() {
        if (node_stack_.empty())
            return *frame_;
        flush_leaf();
        return node_frame_;
    }

    template <typename F>
    void row(F&& children) {
        begin_container(LayoutNode::Kind::Row);
        children();
        end_container();
    }

    template <typename F>
    void column(F&& children) {
        begin_container(LayoutNode::Kind::Column);
        children();
        end_container();
    }

    void spacer() {
        flush_leaf();
        LayoutNode sp;
        sp.kind = LayoutNode::Kind::Spacer;
        sp.id = next_id_++;
        current_children().push_back(std::move(sp));
    }

    std::shared_ptr<const SceneSnapshot> take_snapshot(int w, int h, uint64_t version) {
        if (root_.has_value()) {
            flush_leaf();
            auto& root = *root_;
            LayoutAxis axis = (root.kind == LayoutNode::Kind::Row)
                ? LayoutAxis::Horizontal : LayoutAxis::Vertical;
            layout_measure(root, axis);
            layout_arrange(root, Rect{Point{X{0}, Y{0}}, Size{Width{static_cast<float>(w)}, Height{static_cast<float>(h)}}});

            auto snap = std::make_shared<SceneSnapshot>();
            snap->version = version;
            layout_flatten(root, *snap);
            return snap;
        }
        return AppAccess::take_snapshot(*frame_, version);
    }

private:
    const State* state_;
    Frame* frame_;
    Frame node_frame_;
    std::optional<LayoutNode> root_;
    std::vector<LayoutNode*> node_stack_;
    WidgetId next_id_ = 0;

    Ui(const State& s, Frame& f) : state_(&s), frame_(&f) {}

    void begin_container(LayoutNode::Kind kind) {
        flush_leaf();
        if (!root_) {
            root_ = LayoutNode{};
            root_->kind = kind;
            root_->id = next_id_++;
            node_stack_.push_back(&*root_);
        } else {
            auto& parent = current_children();
            parent.push_back(LayoutNode{});
            auto& node = parent.back();
            node.kind = kind;
            node.id = next_id_++;
            node_stack_.push_back(&node);
        }
    }

    void end_container() {
        flush_leaf();
        node_stack_.pop_back();
    }

    void flush_leaf() {
        if (node_stack_.empty()) return;
        DrawList& dl = node_frame_.dl_;
        if (dl.empty()) return;
        LayoutNode leaf;
        leaf.kind = LayoutNode::Kind::Leaf;
        leaf.id = next_id_++;
        leaf.draws = std::move(dl);
        dl.clear();
        current_children().push_back(std::move(leaf));
    }

    std::vector<LayoutNode>& current_children() {
        return node_stack_.back()->children;
    }

    template <typename S>
    friend void app(Backend, BackendConfig, S,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
    template <typename S>
    friend void app(Backend, BackendConfig,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
    template <typename S>
    friend void app(std::string_view, S,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
    template <typename S>
    friend void app(std::string_view,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
};

template <typename State>
void app(Backend backend, BackendConfig cfg, State initial,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
    stdexec::run_loop loop;
    auto sched = loop.get_scheduler();

    State state = std::move(initial);
    Frame frame;
    int w = cfg.width, h = cfg.height;
    uint64_t version = 0;

    auto publish = [&] {
        AppAccess::reset(frame, w, h);
        Ui<State> ui(state, frame);
        view(ui);
        backend.submit(ui.take_snapshot(w, h, ++version));
        backend.wake();
    };

    std::thread backend_thread([&] {
        backend.run([&](const InputEvent& ev) {
            exec::start_detached(
                stdexec::schedule(sched)
                | stdexec::then([&, ev] {
                    if (std::holds_alternative<WindowClose>(ev)) {
                        loop.finish();
                        return;
                    }
                    if (auto* resize = std::get_if<WindowResize>(&ev)) {
                        w = resize->width;
                        h = resize->height;
                    }
                    if (update) { update(state, ev); }
                    publish();
                })
            );
        });
    });

    backend.wait_ready();
    publish();
    loop.run();

    backend.quit();
    backend_thread.join();
}

template <typename State>
void app(Backend backend, BackendConfig cfg,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
    app<State>(std::move(backend), cfg, State{}, std::move(view), std::move(update));
}

template <typename State>
void app(std::string_view title, State initial,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
    BackendConfig cfg{.title = title.data(), .width = 800, .height = 600};
    app<State>(Backend::software(cfg), cfg, std::move(initial), std::move(view), std::move(update));
}

template <typename State>
void app(std::string_view title,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
    app<State>(title, State{}, std::move(view), std::move(update));
}

} // namespace prism
