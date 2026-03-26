#include <prism/core/draw_list.hpp>

#include <cassert>

void test_basic_commands()
{
    prism::DrawList dl;
    assert(dl.empty());

    dl.filled_rect({0, 0, 100, 50}, prism::Color::rgba(255, 0, 0));
    dl.text("hello", {50, 25}, 14.0f, prism::Color::rgba(0, 0, 0));
    dl.rect_outline({0, 0, 100, 50}, prism::Color::rgba(0, 0, 0), 2.0f);

    assert(dl.size() == 3);
    assert(std::holds_alternative<prism::FilledRect>(dl.commands[0]));
    assert(std::holds_alternative<prism::TextCmd>(dl.commands[1]));
    assert(std::holds_alternative<prism::RectOutline>(dl.commands[2]));
}

void test_clip_stack()
{
    prism::DrawList dl;
    dl.clip_push({10, 10, 80, 40});
    dl.filled_rect({20, 20, 60, 20}, prism::Color::rgba(0, 255, 0));
    dl.clip_pop();

    assert(dl.size() == 3);
    assert(std::holds_alternative<prism::ClipPush>(dl.commands[0]));
    assert(std::holds_alternative<prism::ClipPop>(dl.commands[2]));
}

void test_clear()
{
    prism::DrawList dl;
    dl.filled_rect({0, 0, 10, 10}, prism::Color::rgba(0, 0, 0));
    dl.clear();
    assert(dl.empty());
}

int main()
{
    test_basic_commands();
    test_clip_stack();
    test_clear();
}
