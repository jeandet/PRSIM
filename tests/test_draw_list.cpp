#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/draw_list.hpp>

TEST_CASE("DrawList basic commands")
{
    prism::DrawList dl;
    CHECK(dl.empty());

    dl.filled_rect({0, 0, 100, 50}, prism::Color::rgba(255, 0, 0));
    dl.text("hello", {50, 25}, 14.0f, prism::Color::rgba(0, 0, 0));
    dl.rect_outline({0, 0, 100, 50}, prism::Color::rgba(0, 0, 0), 2.0f);

    CHECK(dl.size() == 3);
    CHECK(std::holds_alternative<prism::FilledRect>(dl.commands[0]));
    CHECK(std::holds_alternative<prism::TextCmd>(dl.commands[1]));
    CHECK(std::holds_alternative<prism::RectOutline>(dl.commands[2]));
}

TEST_CASE("DrawList clip push/pop")
{
    prism::DrawList dl;
    dl.clip_push({10, 10, 80, 40});
    dl.filled_rect({20, 20, 60, 20}, prism::Color::rgba(0, 255, 0));
    dl.clip_pop();

    CHECK(dl.size() == 3);
    CHECK(std::holds_alternative<prism::ClipPush>(dl.commands[0]));
    CHECK(std::holds_alternative<prism::ClipPop>(dl.commands[2]));
}

TEST_CASE("DrawList clear")
{
    prism::DrawList dl;
    dl.filled_rect({0, 0, 10, 10}, prism::Color::rgba(0, 0, 0));
    dl.clear();
    CHECK(dl.empty());
}

TEST_CASE("bounding_box of empty draw list is zero rect") {
    prism::DrawList dl;
    auto bb = dl.bounding_box();
    CHECK(bb.x == 0);
    CHECK(bb.y == 0);
    CHECK(bb.w == 0);
    CHECK(bb.h == 0);
}

TEST_CASE("bounding_box encompasses all commands") {
    prism::DrawList dl;
    dl.filled_rect({10, 20, 100, 50}, prism::Color::rgba(255, 0, 0));
    dl.filled_rect({50, 0, 30, 200}, prism::Color::rgba(0, 255, 0));
    auto bb = dl.bounding_box();
    CHECK(bb.x == 10);
    CHECK(bb.y == 0);
    CHECK(bb.w == 100);    // max_x=110, min_x=10 -> 100
    CHECK(bb.h == 200);    // max_y=200, min_y=0 -> 200
}

TEST_CASE("bounding_box handles rect_outline") {
    prism::DrawList dl;
    dl.rect_outline({5, 5, 90, 40}, prism::Color::rgba(0, 0, 0), 2.f);
    auto bb = dl.bounding_box();
    CHECK(bb.x == 5);
    CHECK(bb.y == 5);
    CHECK(bb.w == 90);
    CHECK(bb.h == 40);
}
