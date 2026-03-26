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
