#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/types.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {} namespace prism::ui {} namespace prism::app {} namespace prism::plot {} namespace prism { using namespace core; using namespace render; using namespace input; using namespace ui; using namespace app; using namespace plot; }


using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
using namespace prism::literals;

TEST_CASE("Scalar construction and raw()") {
    constexpr X x{10.f};
    static_assert(x.raw() == 10.f);
    CHECK(x.raw() == 10.f);
}

TEST_CASE("Scalar same-tag comparison") {
    CHECK(X{5.f} == X{5.f});
    CHECK(X{3.f} < X{5.f});
    CHECK(X{5.f} > X{3.f});
    CHECK(X{5.f} != X{3.f});
}

TEST_CASE("X - X = DX") {
    constexpr auto result = X{10.f} - X{3.f};
    static_assert(std::same_as<decltype(result), const DX>);
    CHECK(result.raw() == doctest::Approx(7.f));
}

TEST_CASE("Y - Y = DY") {
    auto result = Y{20.f} - Y{5.f};
    static_assert(std::same_as<decltype(result), DY>);
    CHECK(result.raw() == doctest::Approx(15.f));
}

TEST_CASE("X + DX = X") {
    constexpr auto result = X{10.f} + DX{5.f};
    static_assert(std::same_as<decltype(result), const X>);
    CHECK(result.raw() == doctest::Approx(15.f));
}

TEST_CASE("X - DX = X") {
    auto result = X{10.f} - DX{3.f};
    static_assert(std::same_as<decltype(result), X>);
    CHECK(result.raw() == doctest::Approx(7.f));
}

TEST_CASE("Y + DY = Y") {
    auto result = Y{10.f} + DY{5.f};
    static_assert(std::same_as<decltype(result), Y>);
    CHECK(result.raw() == doctest::Approx(15.f));
}

TEST_CASE("DX + DX = DX") {
    auto result = DX{3.f} + DX{7.f};
    static_assert(std::same_as<decltype(result), DX>);
    CHECK(result.raw() == doctest::Approx(10.f));
}

TEST_CASE("DX - DX = DX") {
    auto result = DX{10.f} - DX{3.f};
    static_assert(std::same_as<decltype(result), DX>);
    CHECK(result.raw() == doctest::Approx(7.f));
}

TEST_CASE("DX * float = DX") {
    auto result = DX{5.f} * 3.f;
    static_assert(std::same_as<decltype(result), DX>);
    CHECK(result.raw() == doctest::Approx(15.f));
}

TEST_CASE("float * DX = DX") {
    auto result = 3.f * DX{5.f};
    static_assert(std::same_as<decltype(result), DX>);
    CHECK(result.raw() == doctest::Approx(15.f));
}

TEST_CASE("DX / float = DX") {
    auto result = DX{15.f} / 3.f;
    static_assert(std::same_as<decltype(result), DX>);
    CHECK(result.raw() == doctest::Approx(5.f));
}

TEST_CASE("-DX = DX") {
    auto result = -DX{5.f};
    static_assert(std::same_as<decltype(result), DX>);
    CHECK(result.raw() == doctest::Approx(-5.f));
}

TEST_CASE("Width + Width = Width") {
    auto result = Width{10.f} + Width{20.f};
    static_assert(std::same_as<decltype(result), Width>);
    CHECK(result.raw() == doctest::Approx(30.f));
}

TEST_CASE("Width - Width = Width") {
    auto result = Width{30.f} - Width{10.f};
    static_assert(std::same_as<decltype(result), Width>);
    CHECK(result.raw() == doctest::Approx(20.f));
}

TEST_CASE("Width * float = Width") {
    auto result = Width{10.f} * 2.f;
    static_assert(std::same_as<decltype(result), Width>);
    CHECK(result.raw() == doctest::Approx(20.f));
}

TEST_CASE("Width / float = Width") {
    auto result = Width{20.f} / 2.f;
    static_assert(std::same_as<decltype(result), Width>);
    CHECK(result.raw() == doctest::Approx(10.f));
}

TEST_CASE("Height * float = Height") {
    auto result = Height{10.f} * 1.4f;
    static_assert(std::same_as<decltype(result), Height>);
    CHECK(result.raw() == doctest::Approx(14.f));
}

TEST_CASE("Forbidden operations do not compile") {
    // Check via traits — no expression-level requires to avoid GCC hard errors on failed operator lookup
    static_assert(!Addable<XTag, XTag>);
    static_assert(!Scalable<XTag>);
    static_assert(!Addable<XTag, WidthTag>);
    static_assert(!Addable<WidthTag, DXTag>);
    static_assert(!Addable<XTag, YTag>);
    static_assert(!Addable<DXTag, DYTag>);
    // Cross-tag comparison is structurally impossible: X and Y are different types
    static_assert(!std::convertible_to<float, X>);
}

TEST_CASE("User-defined literals") {
    auto x = 10.0_x;
    static_assert(std::same_as<decltype(x), X>);
    CHECK(x.raw() == doctest::Approx(10.f));

    auto w = 100.0_w;
    static_assert(std::same_as<decltype(w), Width>);
    CHECK(w.raw() == doctest::Approx(100.f));
}

TEST_CASE("Point - Point = Offset") {
    Point a{X{10.f}, Y{20.f}};
    Point b{X{3.f}, Y{5.f}};
    auto result = a - b;
    static_assert(std::same_as<decltype(result), Offset>);
    CHECK(result.dx.raw() == doctest::Approx(7.f));
    CHECK(result.dy.raw() == doctest::Approx(15.f));
}

TEST_CASE("Point + Offset = Point") {
    Point p{X{10.f}, Y{20.f}};
    Offset o{DX{5.f}, DY{3.f}};
    auto result = p + o;
    static_assert(std::same_as<decltype(result), Point>);
    CHECK(result.x.raw() == doctest::Approx(15.f));
    CHECK(result.y.raw() == doctest::Approx(23.f));
}

TEST_CASE("Point - Offset = Point") {
    Point p{X{10.f}, Y{20.f}};
    Offset o{DX{3.f}, DY{5.f}};
    auto result = p - o;
    static_assert(std::same_as<decltype(result), Point>);
    CHECK(result.x.raw() == doctest::Approx(7.f));
    CHECK(result.y.raw() == doctest::Approx(15.f));
}

TEST_CASE("Offset + Offset = Offset") {
    Offset a{DX{1.f}, DY{2.f}};
    Offset b{DX{3.f}, DY{4.f}};
    auto result = a + b;
    static_assert(std::same_as<decltype(result), Offset>);
    CHECK(result.dx.raw() == doctest::Approx(4.f));
    CHECK(result.dy.raw() == doctest::Approx(6.f));
}

TEST_CASE("Offset * float") {
    Offset o{DX{2.f}, DY{3.f}};
    auto result = o * 2.f;
    CHECK(result.dx.raw() == doctest::Approx(4.f));
    CHECK(result.dy.raw() == doctest::Approx(6.f));
}

TEST_CASE("Size + Size = Size") {
    Size a{Width{10.f}, Height{20.f}};
    Size b{Width{5.f}, Height{3.f}};
    auto result = a + b;
    static_assert(std::same_as<decltype(result), Size>);
    CHECK(result.w.raw() == doctest::Approx(15.f));
    CHECK(result.h.raw() == doctest::Approx(23.f));
}

TEST_CASE("Size * float") {
    Size s{Width{10.f}, Height{20.f}};
    auto result = s * 2.f;
    CHECK(result.w.raw() == doctest::Approx(20.f));
    CHECK(result.h.raw() == doctest::Approx(40.f));
}

TEST_CASE("Rect contains Point") {
    Rect r{Point{X{10.f}, Y{20.f}}, Size{Width{100.f}, Height{50.f}}};
    CHECK(r.contains(Point{X{50.f}, Y{40.f}}));
    CHECK_FALSE(r.contains(Point{X{5.f}, Y{40.f}}));
    CHECK_FALSE(r.contains(Point{X{50.f}, Y{75.f}}));
}

TEST_CASE("Rect center") {
    Rect r{Point{X{10.f}, Y{20.f}}, Size{Width{100.f}, Height{50.f}}};
    auto c = r.center();
    CHECK(c.x.raw() == doctest::Approx(60.f));
    CHECK(c.y.raw() == doctest::Approx(45.f));
}

TEST_CASE("Rect from_corners") {
    auto r = Rect::from_corners(Point{X{10.f}, Y{20.f}}, Point{X{110.f}, Y{70.f}});
    CHECK(r.origin.x.raw() == doctest::Approx(10.f));
    CHECK(r.origin.y.raw() == doctest::Approx(20.f));
    CHECK(r.extent.w.raw() == doctest::Approx(100.f));
    CHECK(r.extent.h.raw() == doctest::Approx(50.f));
}

TEST_CASE("Offset length") {
    Offset o{DX{3.f}, DY{4.f}};
    CHECK(o.length() == doctest::Approx(5.f));
}

TEST_CASE("Composite equality") {
    Point a{X{1.f}, Y{2.f}};
    Point b{X{1.f}, Y{2.f}};
    Point c{X{1.f}, Y{3.f}};
    CHECK(a == b);
    CHECK(a != c);

    Size s1{Width{10.f}, Height{20.f}};
    Size s2{Width{10.f}, Height{20.f}};
    CHECK(s1 == s2);
}

TEST_CASE("Progress strong type") {
    auto p = Progress{0.5f};
    CHECK(p.raw() == doctest::Approx(0.5f));

    auto p0 = Progress{0.f};
    auto p1 = Progress{1.f};
    CHECK(p0 < p1);
    CHECK(p0 == Progress{0.f});
}

TEST_CASE("EasedProgress strong type") {
    auto e = EasedProgress{0.75f};
    CHECK(e.raw() == doctest::Approx(0.75f));
}
