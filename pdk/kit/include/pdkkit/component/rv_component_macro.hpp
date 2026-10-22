#pragma once

// The describing half of a component type: the defaults, the builders, and the macros that give a type its
// ecs_component_desc. It needs no scene reader or logger. Write PDKKIT_COMPONENT at global scope with T qualified
// (example_cpp_pdkkit::spin), before the first ecs_component_register<T> of T in each translation unit.

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "pdkkit/component/rv_component_capacity.hpp"
#include "pdkkit/component/rv_component_desc.hpp"

namespace rv_pdkkit
{

// Specialised by PDKKIT_COMPONENT; the primary template is undefined, so an unregistered type does not compile.
template <typename T>
struct ecs_component_traits;

// The C++ type a member of each field type must have.
template <ecs_field_type Type>
struct ecs_field_cpp;

template <>
struct ecs_field_cpp<ecs_field_type::int32> {
    using type = int32_t;
};

template <>
struct ecs_field_cpp<ecs_field_type::fixed> {
    using type = rv_pdklib::fx;
};

template <>
struct ecs_field_cpp<ecs_field_type::fixed3> {
    using type = rv_pdklib::fx[ecs_array_items];
};

template <>
struct ecs_field_cpp<ecs_field_type::angle3> {
    using type = rv_pdklib::fx[ecs_array_items];
};

template <>
struct ecs_field_cpp<ecs_field_type::color> {
    using type = rv_color;
};

template <>
struct ecs_field_cpp<ecs_field_type::string> {
    using type = char[ecs_value_capacity];
};

template <>
struct ecs_field_cpp<ecs_field_type::model> {
    using type = char[ecs_value_capacity];
};

// Default of a field, one helper per type; each builds the union member its type names.
constexpr ecs_field_default ecs_default_int32(int32_t value)
{
    return ecs_field_default{ .int32 = value };
}

// A Q20.12 raw value, as the scene's fixed text parses to.
constexpr ecs_field_default ecs_default_fixed(rv_pdklib::fx raw)
{
    return ecs_field_default{ .fixed = raw };
}

constexpr ecs_field_default ecs_default_fixed3(rv_pdklib::fx x, rv_pdklib::fx y, rv_pdklib::fx z)
{
    return ecs_field_default{ .fx3 = { x, y, z } };
}

constexpr ecs_field_default ecs_default_angle3(rv_pdklib::fx x, rv_pdklib::fx y, rv_pdklib::fx z)
{
    return ecs_default_fixed3(x, y, z);
}

constexpr ecs_field_default ecs_default_color(uint8_t r, uint8_t g, uint8_t b)
{
    return ecs_field_default{ .color = rv_color{ r, g, b } };
}

// Text of a string or model default; N counts the NUL, so the text fits ecs_value_capacity.
template <std::size_t N>
constexpr ecs_field_default ecs_default_string(const char (&text)[N])
{
    static_assert(N <= static_cast<std::size_t>(ecs_value_capacity), "default text does not fit ecs_value_capacity");
    ecs_field_default def{ .text = {} };
    for (std::size_t i = 0; i < N; ++i) {
        def.text[i] = text[i];
    }
    return def;
}

template <std::size_t N>
constexpr ecs_field_default ecs_default_model(const char (&text)[N])
{
    return ecs_default_string(text);
}

// Copies a literal name, its NUL included, into a name array of a description.
template <std::size_t N>
constexpr void ecs_name_copy(char (&dst)[ecs_name_capacity], const char (&src)[N])
{
    static_assert(N <= static_cast<std::size_t>(ecs_name_capacity), "name does not fit ecs_name_capacity");
    for (std::size_t i = 0; i < N; ++i) {
        dst[i] = src[i];
    }
}

// A field of a description. Member is the member's declared type, which must be the one its field type names; its
// size must also match the table row.
template <ecs_field_type Type, typename Member, std::size_t N>
constexpr ecs_field_desc ecs_field_make(const char (&name)[N], std::size_t offset, const ecs_field_default &def)
{
    static_assert(std::is_same_v<Member, typename ecs_field_cpp<Type>::type>, "member type does not match the field type");
    static_assert(sizeof(Member) == ecs_field_type_of(Type)->size, "member size does not match the field type's size");
    ecs_field_desc field{};
    ecs_name_copy(field.name, name);
    field.type = Type;
    field.offset = static_cast<uint32_t>(offset);
    field.def = def;
    return field;
}

// A component's description: its name and fields, with the count checked against the capacity.
template <std::size_t N, typename... Fields>
constexpr ecs_component_desc ecs_component_build(const char (&name)[N], const Fields &...fields)
{
    static_assert(sizeof...(fields) <= static_cast<std::size_t>(ecs_component_field_capacity),
        "component has more fields than ecs_component_field_capacity");
    ecs_component_desc desc{};
    ecs_name_copy(desc.name, name);
    desc.field_count = static_cast<int64_t>(sizeof...(fields));
    int64_t index = 0;
    ((desc.fields[index++] = fields), ...);
    return desc;
}

// Describes T: a traits specialisation whose desc has the name and the fields, each at offsetof its member.
// Fields: PDKKIT_FIELD(member, type, default...), where type is an ecs_field_type name (int32, fixed, fixed3, ...).
#define PDKKIT_COMPONENT(T, NAME, ...)                                         \
    template <>                                                                \
    struct rv_pdkkit::ecs_component_traits<T> {                                \
        using owner = T;                                                       \
        static constexpr ::rv_pdkkit::ecs_component_desc desc =                \
            ::rv_pdkkit::ecs_component_build(NAME __VA_OPT__(, ) __VA_ARGS__); \
    }

#define PDKKIT_FIELD(member, type, ...)                                                              \
    ::rv_pdkkit::ecs_field_make<::rv_pdkkit::ecs_field_type::type, decltype(owner::member)>(#member, \
        offsetof(owner, member),                                                                     \
        ::rv_pdkkit::ecs_default_##type(__VA_ARGS__))

} // namespace rv_pdkkit
