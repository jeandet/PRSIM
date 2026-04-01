#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/draw_list.hpp>

namespace {
prism::Rect R(float x, float y, float w, float h) {
    return {prism::Point{prism::X{x}, prism::Y{y}}, prism::Size{prism::Width{w}, prism::Height{h}}};
}
prism::Point P(float x, float y) {
    return {prism::X{x}, prism::Y{y}};
}
prism::Size S(float w, float h) {
    return {prism::Width{w}, prism::Height{h}};
}
}

TEST_CASE("DrawList basic commands")
{
    prism::DrawList dl;
    CHECK(dl.empty());

    dl.filled_rect(R(0, 0, 100, 50), prism::Color::rgba(255, 0, 0));
    dl.text("hello", P(50, 25), 14.0f, prism::Color::rgba(0, 0, 0));
    dl.rect_outline(R(0, 0, 100, 50), prism::Color::rgba(0, 0, 0), 2.0f);

    CHECK(dl.size() == 3);
    CHECK(std::holds_alternative<prism::FilledRect>(dl.commands[0]));
    CHECK(std::holds_alternative<prism::TextCmd>(dl.commands[1]));
    CHECK(std::holds_alternative<prism::RectOutline>(dl.commands[2]));
}

TEST_CASE("DrawList clip push/pop")
{
    prism::DrawList dl;
    dl.clip_push(P(10.f, 10.f), S(80.f, 40.f));
    dl.filled_rect(R(10, 10, 60, 20), prism::Color::rgba(0, 255, 0));
    dl.clip_pop();

    CHECK(dl.size() == 3);
    CHECK(std::holds_alternative<prism::ClipPush>(dl.commands[0]));
    CHECK(std::holds_alternative<prism::ClipPop>(dl.commands[2]));
}

TEST_CASE("clip_push offsets filled_rect coordinates") {
    prism::DrawList dl;
    dl.clip_push(P(10.f, 20.f), S(80.f, 40.f));
    dl.filled_rect(R(0, 0, 30, 15), prism::Color::rgba(255, 0, 0));
    dl.clip_pop();

    auto& clip = std::get<prism::ClipPush>(dl.commands[0]);
    CHECK(clip.rect.origin.x.raw() == 10.f);
    CHECK(clip.rect.origin.y.raw() == 20.f);
    CHECK(clip.rect.extent.w.raw() == 80.f);
    CHECK(clip.rect.extent.h.raw() == 40.f);

    auto& fr = std::get<prism::FilledRect>(dl.commands[1]);
    CHECK(fr.rect.origin.x.raw() == 10.f);
    CHECK(fr.rect.origin.y.raw() == 20.f);
    CHECK(fr.rect.extent.w.raw() == 30.f);
    CHECK(fr.rect.extent.h.raw() == 15.f);
}

TEST_CASE("clip_push offsets text origin") {
    prism::DrawList dl;
    dl.clip_push(P(5.f, 10.f), S(100.f, 50.f));
    dl.text("hi", P(0, 0), 14.f, prism::Color::rgba(0, 0, 0));
    dl.clip_pop();

    auto& t = std::get<prism::TextCmd>(dl.commands[1]);
    CHECK(t.origin.x.raw() == 5.f);
    CHECK(t.origin.y.raw() == 10.f);
}

TEST_CASE("clip_push offsets rect_outline") {
    prism::DrawList dl;
    dl.clip_push(P(3.f, 7.f), S(50.f, 50.f));
    dl.rect_outline(R(0, 0, 20, 20), prism::Color::rgba(0, 0, 0), 1.f);
    dl.clip_pop();

    auto& ro = std::get<prism::RectOutline>(dl.commands[1]);
    CHECK(ro.rect.origin.x.raw() == 3.f);
    CHECK(ro.rect.origin.y.raw() == 7.f);
}

TEST_CASE("nested clip_push offsets compose additively") {
    prism::DrawList dl;
    dl.clip_push(P(10.f, 10.f), S(100.f, 100.f));
    dl.clip_push(P(5.f, 5.f), S(80.f, 80.f));
    dl.filled_rect(R(0, 0, 10, 10), prism::Color::rgba(255, 0, 0));
    dl.clip_pop();
    dl.filled_rect(R(0, 0, 10, 10), prism::Color::rgba(0, 255, 0));
    dl.clip_pop();

    auto& inner_clip = std::get<prism::ClipPush>(dl.commands[1]);
    CHECK(inner_clip.rect.origin.x.raw() == 15.f);
    CHECK(inner_clip.rect.origin.y.raw() == 15.f);

    auto& inner_rect = std::get<prism::FilledRect>(dl.commands[2]);
    CHECK(inner_rect.rect.origin.x.raw() == 15.f);
    CHECK(inner_rect.rect.origin.y.raw() == 15.f);

    auto& outer_rect = std::get<prism::FilledRect>(dl.commands[4]);
    CHECK(outer_rect.rect.origin.x.raw() == 10.f);
    CHECK(outer_rect.rect.origin.y.raw() == 10.f);
}

TEST_CASE("no clip_push means no offset") {
    prism::DrawList dl;
    dl.filled_rect(R(5, 5, 10, 10), prism::Color::rgba(255, 0, 0));
    dl.text("hi", P(3, 7), 14.f, prism::Color::rgba(0, 0, 0));

    auto& fr = std::get<prism::FilledRect>(dl.commands[0]);
    CHECK(fr.rect.origin.x.raw() == 5.f);
    CHECK(fr.rect.origin.y.raw() == 5.f);

    auto& t = std::get<prism::TextCmd>(dl.commands[1]);
    CHECK(t.origin.x.raw() == 3.f);
    CHECK(t.origin.y.raw() == 7.f);
}

TEST_CASE("clip_pop restores previous offset") {
    prism::DrawList dl;
    dl.clip_push(P(10.f, 10.f), S(100.f, 100.f));
    dl.text("inside", P(0, 0), 14.f, prism::Color::rgba(0, 0, 0));
    dl.clip_pop();
    dl.text("outside", P(0, 0), 14.f, prism::Color::rgba(0, 0, 0));

    auto& inside = std::get<prism::TextCmd>(dl.commands[1]);
    CHECK(inside.origin.x.raw() == 10.f);
    CHECK(inside.origin.y.raw() == 10.f);

    auto& outside = std::get<prism::TextCmd>(dl.commands[3]);
    CHECK(outside.origin.x.raw() == 0.f);
    CHECK(outside.origin.y.raw() == 0.f);
}

TEST_CASE("DrawList clear")
{
    prism::DrawList dl;
    dl.filled_rect(R(0, 0, 10, 10), prism::Color::rgba(0, 0, 0));
    dl.clear();
    CHECK(dl.empty());
}

TEST_CASE("bounding_box of empty draw list is zero rect") {
    prism::DrawList dl;
    auto bb = dl.bounding_box();
    CHECK(bb.origin.x.raw() == 0);
    CHECK(bb.origin.y.raw() == 0);
    CHECK(bb.extent.w.raw() == 0);
    CHECK(bb.extent.h.raw() == 0);
}

TEST_CASE("bounding_box encompasses all commands") {
    prism::DrawList dl;
    dl.filled_rect(R(10, 20, 100, 50), prism::Color::rgba(255, 0, 0));
    dl.filled_rect(R(50, 0, 30, 200), prism::Color::rgba(0, 255, 0));
    auto bb = dl.bounding_box();
    CHECK(bb.origin.x.raw() == 10);
    CHECK(bb.origin.y.raw() == 0);
    CHECK(bb.extent.w.raw() == 100);    // max_x=110, min_x=10 -> 100
    CHECK(bb.extent.h.raw() == 200);    // max_y=200, min_y=0 -> 200
}

TEST_CASE("bounding_box handles rect_outline") {
    prism::DrawList dl;
    dl.rect_outline(R(5, 5, 90, 40), prism::Color::rgba(0, 0, 0), 2.f);
    auto bb = dl.bounding_box();
    CHECK(bb.origin.x.raw() == 5);
    CHECK(bb.origin.y.raw() == 5);
    CHECK(bb.extent.w.raw() == 90);
    CHECK(bb.extent.h.raw() == 40);
}

TEST_CASE("DrawList rounded_rect command") {
    prism::DrawList dl;
    dl.rounded_rect(R(10, 20, 100, 50), prism::Color::rgba(255, 0, 0), 8.f);
    REQUIRE(dl.size() == 1);
    auto& rr = std::get<prism::RoundedRect>(dl.commands[0]);
    CHECK(rr.rect.origin.x.raw() == 10.f);
    CHECK(rr.rect.origin.y.raw() == 20.f);
    CHECK(rr.rect.extent.w.raw() == 100.f);
    CHECK(rr.rect.extent.h.raw() == 50.f);
    CHECK(rr.radius == 8.f);
    CHECK(rr.thickness == 0.f);
}

TEST_CASE("DrawList rounded_rect stroke") {
    prism::DrawList dl;
    dl.rounded_rect(R(0, 0, 50, 50), prism::Color::rgba(0, 0, 0), 5.f, 2.f);
    auto& rr = std::get<prism::RoundedRect>(dl.commands[0]);
    CHECK(rr.thickness == 2.f);
}

TEST_CASE("clip_push offsets rounded_rect") {
    prism::DrawList dl;
    dl.clip_push(P(10.f, 20.f), S(200.f, 200.f));
    dl.rounded_rect(R(0, 0, 50, 30), prism::Color::rgba(0, 0, 0), 4.f);
    dl.clip_pop();
    auto& rr = std::get<prism::RoundedRect>(dl.commands[1]);
    CHECK(rr.rect.origin.x.raw() == 10.f);
    CHECK(rr.rect.origin.y.raw() == 20.f);
}
