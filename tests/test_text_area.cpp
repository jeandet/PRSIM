#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/widget_tree.hpp>

// --- wrap_lines tests ---

TEST_CASE("wrap_lines: empty string produces one empty span") {
    float cw = prism::char_width(14.f);
    float text_w = 200.f - 2 * 4.f;  // widget_w - 2*padding
    auto lines = prism::detail::wrap_lines("", text_w, cw);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].start == 0);
    CHECK(lines[0].length == 0);
}

TEST_CASE("wrap_lines: short string fits in one line") {
    float cw = prism::char_width(14.f);
    float text_w = 200.f - 2 * 4.f;
    auto lines = prism::detail::wrap_lines("hello", text_w, cw);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].start == 0);
    CHECK(lines[0].length == 5);
}

TEST_CASE("wrap_lines: newline splits into two lines") {
    float cw = prism::char_width(14.f);
    float text_w = 200.f - 2 * 4.f;
    auto lines = prism::detail::wrap_lines("ab\ncd", text_w, cw);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].start == 0);
    CHECK(lines[0].length == 2);
    CHECK(lines[1].start == 3);  // after the \n
    CHECK(lines[1].length == 2);
}

TEST_CASE("wrap_lines: trailing newline produces empty last line") {
    float cw = prism::char_width(14.f);
    float text_w = 200.f - 2 * 4.f;
    auto lines = prism::detail::wrap_lines("ab\n", text_w, cw);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].length == 2);
    CHECK(lines[1].start == 3);
    CHECK(lines[1].length == 0);
}

TEST_CASE("wrap_lines: consecutive newlines produce empty lines") {
    float cw = prism::char_width(14.f);
    float text_w = 200.f - 2 * 4.f;
    auto lines = prism::detail::wrap_lines("a\n\nb", text_w, cw);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].length == 1);  // "a"
    CHECK(lines[1].length == 0);  // empty
    CHECK(lines[2].length == 1);  // "b"
}

TEST_CASE("wrap_lines: long line wraps at max_chars") {
    float cw = prism::char_width(14.f);
    // Force wrap at 5 chars: text_w = 5 * cw
    float text_w = 5.f * cw;
    auto lines = prism::detail::wrap_lines("abcdefghij", text_w, cw);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].start == 0);
    CHECK(lines[0].length == 5);
    CHECK(lines[1].start == 5);
    CHECK(lines[1].length == 5);
}

TEST_CASE("wrap_lines: exact fit does not wrap") {
    float cw = prism::char_width(14.f);
    float text_w = 5.f * cw;
    auto lines = prism::detail::wrap_lines("abcde", text_w, cw);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].length == 5);
}

TEST_CASE("wrap_lines: mixed newlines and wrapping") {
    float cw = prism::char_width(14.f);
    float text_w = 3.f * cw;  // wrap at 3 chars
    auto lines = prism::detail::wrap_lines("abcde\nfg", text_w, cw);
    // "abcde" wraps to "abc" + "de", then "fg"
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].length == 3);  // "abc"
    CHECK(lines[1].length == 2);  // "de"
    CHECK(lines[2].length == 2);  // "fg"
}

// --- cursor helper tests ---

TEST_CASE("cursor_to_line_col: cursor at start") {
    float cw = prism::char_width(14.f);
    float text_w = 200.f - 2 * 4.f;
    auto lines = prism::detail::wrap_lines("hello\nworld", text_w, cw);
    auto [line, col] = prism::detail::cursor_to_line_col(0, lines);
    CHECK(line == 0);
    CHECK(col == 0);
}

TEST_CASE("cursor_to_line_col: cursor mid first line") {
    float cw = prism::char_width(14.f);
    float text_w = 200.f - 2 * 4.f;
    auto lines = prism::detail::wrap_lines("hello\nworld", text_w, cw);
    auto [line, col] = prism::detail::cursor_to_line_col(3, lines);
    CHECK(line == 0);
    CHECK(col == 3);
}

TEST_CASE("cursor_to_line_col: cursor at start of second line (after newline)") {
    float cw = prism::char_width(14.f);
    float text_w = 200.f - 2 * 4.f;
    auto lines = prism::detail::wrap_lines("hello\nworld", text_w, cw);
    // cursor=6 is after "hello\n" -> start of "world"
    auto [line, col] = prism::detail::cursor_to_line_col(6, lines);
    CHECK(line == 1);
    CHECK(col == 0);
}

TEST_CASE("cursor_to_line_col: cursor at end of text") {
    float cw = prism::char_width(14.f);
    float text_w = 200.f - 2 * 4.f;
    auto lines = prism::detail::wrap_lines("hello\nworld", text_w, cw);
    // cursor=11 is at end of "world"
    auto [line, col] = prism::detail::cursor_to_line_col(11, lines);
    CHECK(line == 1);
    CHECK(col == 5);
}

TEST_CASE("cursor_to_line_col: cursor on wrapped line") {
    float cw = prism::char_width(14.f);
    float text_w = 3.f * cw;  // wrap at 3
    auto lines = prism::detail::wrap_lines("abcde", text_w, cw);
    // lines: "abc"(0,3) "de"(3,2)
    auto [line, col] = prism::detail::cursor_to_line_col(4, lines);
    CHECK(line == 1);
    CHECK(col == 1);
}

TEST_CASE("line_col_to_cursor: round-trip") {
    float cw = prism::char_width(14.f);
    float text_w = 200.f - 2 * 4.f;
    auto lines = prism::detail::wrap_lines("hello\nworld", text_w, cw);

    for (size_t cursor = 0; cursor <= 11; ++cursor) {
        auto [line, col] = prism::detail::cursor_to_line_col(cursor, lines);
        size_t back = prism::detail::line_col_to_cursor(line, col, lines);
        CHECK(back == cursor);
    }
}

TEST_CASE("line_col_to_cursor: clamps col to line length") {
    float cw = prism::char_width(14.f);
    float text_w = 200.f - 2 * 4.f;
    auto lines = prism::detail::wrap_lines("ab\ncd", text_w, cw);
    // Line 0 has length 2; asking col=10 should clamp to 2
    size_t cursor = prism::detail::line_col_to_cursor(0, 10, lines);
    CHECK(cursor == 2);
}

TEST_CASE("line_col_to_cursor: empty line returns start") {
    float cw = prism::char_width(14.f);
    float text_w = 200.f - 2 * 4.f;
    auto lines = prism::detail::wrap_lines("a\n\nb", text_w, cw);
    // Line 1 is empty (start=2, length=0)
    size_t cursor = prism::detail::line_col_to_cursor(1, 0, lines);
    CHECK(cursor == 2);
}
