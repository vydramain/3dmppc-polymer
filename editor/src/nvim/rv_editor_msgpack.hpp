#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rv_editor
{

// The msgpack subset nvim's RPC needs (docs/adr/0005-code-editor-nvim.md):
// encoding arrays, maps, strings, integers, booleans and nil; decoding every
// type nvim sends. No external dependency.

struct rv_editor_mpack
{
    enum class rv_editor_mpack_type : uint8_t
    {
        nil,
        boolean,
        integer, // signed or unsigned, held as int64_t (nvim ids and counts fit)
        real,
        string,  // str and bin alike
        array,
        map,
        ext,     // nvim's Buffer/Window/Tabpage handles: `ext_type` and the id in `i`
    };

    rv_editor_mpack_type type = rv_editor_mpack_type::nil;
    bool b = false;
    int64_t i = 0;
    double d = 0.0;
    int8_t ext_type = 0;
    std::string s;
    std::vector<rv_editor_mpack> items;  // array, or a map's values
    std::vector<rv_editor_mpack> keys;   // a map's keys, one per value in `items`

    bool is(rv_editor_mpack_type t) const { return type == t; }
    // Map lookup by string key; nullptr when absent.
    const rv_editor_mpack *get(std::string_view key) const;
};

// Appends one encoded value to `out`.
class rv_editor_mpack_writer
{
public:
    explicit rv_editor_mpack_writer(std::string &out) : out_(out) {}

    void nil();
    void boolean(bool v);
    void integer(int64_t v);
    void string(std::string_view v);
    void array(uint32_t count); // the next `count` values are its items
    void map(uint32_t count);   // the next 2 * `count` values are its keys and values

private:
    void put(uint8_t byte) { out_.push_back(static_cast<char>(byte)); }
    void put_be(uint64_t v, int bytes);
    std::string &out_;
};

// Decodes one value from the front of `data`. Returns the bytes it used, 0 when
// `data` does not yet hold a whole value, or -1 when it is not msgpack.
// Nesting deeper than 64 levels is refused as malformed.
ptrdiff_t rv_editor_mpack_read(std::string_view data, rv_editor_mpack &value);

} // namespace rv_editor
