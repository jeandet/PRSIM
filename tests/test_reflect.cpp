#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/reflect.hpp>
#include <prism/core/field.hpp>

#include <string>
#include <vector>

struct Simple {
    prism::Field<int> x{1};
    prism::Field<std::string> name{"hi"};
};

struct Nested {
    Simple inner;
    prism::Field<bool> flag{false};
};

struct Empty {};

struct NotAModel {
    int raw_value = 42;
};

TEST_CASE("is_field_v detects Field<T>") {
    CHECK(prism::is_field_v<prism::Field<int>>);
    CHECK(prism::is_field_v<prism::Field<std::string>>);
    CHECK_FALSE(prism::is_field_v<int>);
    CHECK_FALSE(prism::is_field_v<std::string>);
}

#if __cpp_impl_reflection
TEST_CASE("for_each_field visits all Field<T> members") {
    Simple s;
    int count = 0;
    prism::for_each_field(s, [&](auto&) { ++count; });
    CHECK(count == 2);
}

TEST_CASE("for_each_field gives access to field value") {
    Simple s;
    std::vector<int> values;
    prism::for_each_field(s, [&](auto& field) {
        using F = std::remove_cvref_t<decltype(field)>;
        if constexpr (std::is_same_v<F, prism::Field<int>>)
            values.push_back(field.get());
    });
    CHECK(values.size() == 1);
    CHECK(values[0] == 1);
}

TEST_CASE("is_component_v detects structs containing Field<T>") {
    CHECK(prism::is_component_v<Simple>);
    CHECK(prism::is_component_v<Nested>);
    CHECK_FALSE(prism::is_component_v<int>);
    CHECK_FALSE(prism::is_component_v<Empty>);
    CHECK_FALSE(prism::is_component_v<NotAModel>);
}

TEST_CASE("for_each_member visits fields and sub-components") {
    Nested n;
    int fields = 0;
    int components = 0;
    prism::for_each_member(n, [&](auto& member) {
        using M = std::remove_cvref_t<decltype(member)>;
        if constexpr (prism::is_field_v<M>)
            ++fields;
        else if constexpr (prism::is_component_v<M>)
            ++components;
    });
    CHECK(fields == 1);      // flag
    CHECK(components == 1);  // inner
}
#endif // __cpp_impl_reflection

#include <prism/core/state.hpp>

struct ModelWithState {
    prism::Field<int> visible{0};
    prism::State<int> hidden{0};
};

TEST_CASE("is_state_v detects State<T>") {
    CHECK(prism::is_state_v<prism::State<int>>);
    CHECK(prism::is_state_v<prism::State<std::string>>);
    CHECK_FALSE(prism::is_state_v<prism::Field<int>>);
    CHECK_FALSE(prism::is_state_v<int>);
}

#if __cpp_impl_reflection
TEST_CASE("is_component_v is true for struct with State + Field members") {
    CHECK(prism::is_component_v<ModelWithState>);
}

TEST_CASE("for_each_field skips State members") {
    ModelWithState model;
    int count = 0;
    prism::for_each_field(model, [&](auto&) { ++count; });
    CHECK(count == 1);
}
#endif // __cpp_impl_reflection
