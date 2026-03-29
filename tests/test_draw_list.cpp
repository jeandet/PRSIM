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
    dl.clip_push({10.f, 10.f}, {80.f, 40.f});
    dl.filled_rect({10, 10, 60, 20}, prism::Color::rgba(0, 255, 0));
    dl.clip_pop();

    CHECK(dl.size() == 3);
    CHECK(std::holds_alternative<prism::ClipPush>(dl.commands[0]));
    CHECK(std::holds_alternative<prism::ClipPop>(dl.commands[2]));
}

TEST_CASE("clip_push offsets filled_rect coordinates") {
    prism::DrawList dl;
    dl.clip_push({10.f, 20.f}, {80.f, 40.f});
    dl.filled_rect({0, 0, 30, 15}, prism::Color::rgba(255, 0, 0));
    dl.clip_pop();

    auto& clip = std::get<prism::ClipPush>(dl.commands[0]);
    CHECK(clip.rect.x == 10.f);
    CHECK(clip.rect.y == 20.f);
    CHECK(clip.rect.w == 80.f);
    CHECK(clip.rect.h == 40.f);

    auto& fr = std::get<prism::FilledRect>(dl.commands[1]);
    CHECK(fr.rect.x == 10.f);
    CHECK(fr.rect.y == 20.f);
    CHECK(fr.rect.w == 30.f);
    CHECK(fr.rect.h == 15.f);
}

TEST_CASE("clip_push offsets text origin") {
    prism::DrawList dl;
    dl.clip_push({5.f, 10.f}, {100.f, 50.f});
    dl.text("hi", {0, 0}, 14.f, prism::Color::rgba(0, 0, 0));
    dl.clip_pop();

    auto& t = std::get<prism::TextCmd>(dl.commands[1]);
    CHECK(t.origin.x == 5.f);
    CHECK(t.origin.y == 10.f);
}

TEST_CASE("clip_push offsets rect_outline") {
    prism::DrawList dl;
    dl.clip_push({3.f, 7.f}, {50.f, 50.f});
    dl.rect_outline({0, 0, 20, 20}, prism::Color::rgba(0, 0, 0), 1.f);
    dl.clip_pop();

    auto& ro = std::get<prism::RectOutline>(dl.commands[1]);
    CHECK(ro.rect.x == 3.f);
    CHECK(ro.rect.y == 7.f);
}

TEST_CASE("nested clip_push offsets compose additively") {
    prism::DrawList dl;
    dl.clip_push({10.f, 10.f}, {100.f, 100.f});
    dl.clip_push({5.f, 5.f}, {80.f, 80.f});
    dl.filled_rect({0, 0, 10, 10}, prism::Color::rgba(255, 0, 0));
    dl.clip_pop();
    dl.filled_rect({0, 0, 10, 10}, prism::Color::rgba(0, 255, 0));
    dl.clip_pop();

    auto& inner_clip = std::get<prism::ClipPush>(dl.commands[1]);
    CHECK(inner_clip.rect.x == 15.f);
    CHECK(inner_clip.rect.y == 15.f);

    auto& inner_rect = std::get<prism::FilledRect>(dl.commands[2]);
    CHECK(inner_rect.rect.x == 15.f);
    CHECK(inner_rect.rect.y == 15.f);

    auto& outer_rect = std::get<prism::FilledRect>(dl.commands[4]);
    CHECK(outer_rect.rect.x == 10.f);
    CHECK(outer_rect.rect.y == 10.f);
}

TEST_CASE("no clip_push means no offset") {
    prism::DrawList dl;
    dl.filled_rect({5, 5, 10, 10}, prism::Color::rgba(255, 0, 0));
    dl.text("hi", {3, 7}, 14.f, prism::Color::rgba(0, 0, 0));

    auto& fr = std::get<prism::FilledRect>(dl.commands[0]);
    CHECK(fr.rect.x == 5.f);
    CHECK(fr.rect.y == 5.f);

    auto& t = std::get<prism::TextCmd>(dl.commands[1]);
    CHECK(t.origin.x == 3.f);
    CHECK(t.origin.y == 7.f);
}

TEST_CASE("clip_pop restores previous offset") {
    prism::DrawList dl;
    dl.clip_push({10.f, 10.f}, {100.f, 100.f});
    dl.text("inside", {0, 0}, 14.f, prism::Color::rgba(0, 0, 0));
    dl.clip_pop();
    dl.text("outside", {0, 0}, 14.f, prism::Color::rgba(0, 0, 0));

    auto& inside = std::get<prism::TextCmd>(dl.commands[1]);
    CHECK(inside.origin.x == 10.f);
    CHECK(inside.origin.y == 10.f);

    auto& outside = std::get<prism::TextCmd>(dl.commands[3]);
    CHECK(outside.origin.x == 0.f);
    CHECK(outside.origin.y == 0.f);
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
