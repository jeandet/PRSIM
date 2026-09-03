#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {

// Counts its own record() calls into a caller-owned int, so each TEST_CASE gets an
// independent counter with no shared/static state to reset between cases.
struct CountingValue {
    int tick = 0;
    int* record_calls = nullptr;
    bool operator==(const CountingValue& o) const { return tick == o.tick; }
};

} // namespace

namespace prism::ui {

template <>
struct Widget<CountingValue> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;

    static void record(DrawList& dl, const Field<CountingValue>& field, WidgetNode& node) {
        if (int* n = field.get().record_calls) ++*n;
        auto w = delegate_detail::widget_w(node);
        dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, delegate_detail::widget_h(node)), Color::rgba(0, 0, 0));
    }

    static void handle_input(Field<CountingValue>&, const InputEvent&, WidgetNode&) {}
};

} // namespace prism::ui

namespace {

struct CounterModel {
    int trigger_calls = 0;
    int sibling_calls = 0;
    prism::Field<CountingValue> trigger{CountingValue{0, &trigger_calls}};
    prism::Field<CountingValue> sibling{CountingValue{0, &sibling_calls}};

    void view(prism::WidgetTree::ViewBuilder& vb) { vb.vstack(trigger, sibling); }
};

} // namespace

TEST_CASE("republishing for one dirty widget does not re-record an untouched sibling") {
    CounterModel model;
    prism::WidgetTree tree(model);
    // Bootstrap publish: WidgetNode::canvas_bounds starts at (0,0), which always differs
    // from the real allocated size, so a re-record here is genuinely needed regardless.
    (void)tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();
    int sibling_before = model.sibling_calls;

    model.trigger.set(CountingValue{model.trigger.get().tick + 1, &model.trigger_calls});
    (void)tree.build_snapshot(400, 300, 2);
    tree.clear_dirty();

    // sibling was never dirty and its allocated size hasn't changed -- it must not pay for
    // a re-record just because `trigger` forced this window to republish.
    CHECK(model.sibling_calls == sibling_before);
}

TEST_CASE("republishing a dirty widget whose size is unchanged records it exactly once, not twice") {
    CounterModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();
    int trigger_before = model.trigger_calls;

    model.trigger.set(CountingValue{model.trigger.get().tick + 1, &model.trigger_calls});
    (void)tree.build_snapshot(400, 300, 2);
    tree.clear_dirty();

    // refresh_dirty()'s pre-layout call already produced correct output at the widget's
    // (unchanged) allocated size -- update_canvas_bounds must not redundantly re-record it.
    CHECK(model.trigger_calls == trigger_before + 1);
}

TEST_CASE("a genuine resize still re-records an untouched sibling") {
    CounterModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();
    int sibling_before = model.sibling_calls;

    // Widening the viewport changes every child's allocated width in a plain vstack --
    // proves the skip above doesn't defeat the real case update_canvas_bounds exists for.
    (void)tree.build_snapshot(800, 300, 2);
    tree.clear_dirty();

    CHECK(model.sibling_calls == sibling_before + 1);
}
