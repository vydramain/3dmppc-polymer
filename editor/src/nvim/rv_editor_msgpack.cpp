// msgpack: the encoder and the incremental decoder nvim's RPC stream needs.

#include "nvim/rv_editor_msgpack.hpp"

#include <cstring>

namespace rv_editor
{

namespace
{

using mtype = rv_editor_mpack::rv_editor_mpack_type;

// MessagePack spec, formats (https://github.com/msgpack/msgpack/blob/master/spec.md).
constexpr uint8_t mpack_positive_fixint_max = 0x7f;
constexpr uint8_t mpack_negative_fixint_min = 0xe0;
constexpr uint8_t mpack_fixmap_mask = 0xf0;
constexpr uint8_t mpack_fixmap_value = 0x80;
constexpr uint8_t mpack_fixmap_size_mask = 0x0f;
constexpr uint8_t mpack_fixarray_mask = 0xf0;
constexpr uint8_t mpack_fixarray_value = 0x90;
constexpr uint8_t mpack_fixarray_size_mask = 0x0f;
constexpr uint8_t mpack_fixstr_mask = 0xe0;
constexpr uint8_t mpack_fixstr_value = 0xa0;
constexpr uint8_t mpack_fixstr_size_mask = 0x1f;
constexpr uint8_t mpack_nil = 0xc0;
constexpr uint8_t mpack_false = 0xc2;
constexpr uint8_t mpack_true = 0xc3;
constexpr uint8_t mpack_bin8 = 0xc4;
constexpr uint8_t mpack_bin16 = 0xc5;
constexpr uint8_t mpack_bin32 = 0xc6;
constexpr uint8_t mpack_ext8 = 0xc7;
constexpr uint8_t mpack_ext16 = 0xc8;
constexpr uint8_t mpack_ext32 = 0xc9;
constexpr uint8_t mpack_float32 = 0xca;
constexpr uint8_t mpack_float64 = 0xcb;
constexpr uint8_t mpack_uint8 = 0xcc;
constexpr uint8_t mpack_uint16 = 0xcd;
constexpr uint8_t mpack_uint32 = 0xce;
constexpr uint8_t mpack_uint64 = 0xcf;
constexpr uint8_t mpack_int8 = 0xd0;
constexpr uint8_t mpack_int16 = 0xd1;
constexpr uint8_t mpack_int32 = 0xd2;
constexpr uint8_t mpack_int64 = 0xd3;
constexpr uint8_t mpack_fixext1 = 0xd4;
constexpr uint8_t mpack_fixext2 = 0xd5;
constexpr uint8_t mpack_fixext4 = 0xd6;
constexpr uint8_t mpack_fixext8 = 0xd7;
constexpr uint8_t mpack_fixext16 = 0xd8;
constexpr uint8_t mpack_str8 = 0xd9;
constexpr uint8_t mpack_str16 = 0xda;
constexpr uint8_t mpack_str32 = 0xdb;
constexpr uint8_t mpack_array16 = 0xdc;
constexpr uint8_t mpack_array32 = 0xdd;
constexpr uint8_t mpack_map16 = 0xde;
constexpr uint8_t mpack_map32 = 0xdf;

// Nesting depth limit: https://github.com/msgpack/msgpack/blob/master/spec.md.
constexpr int mpack_max_nesting_depth = 64;

// Starting depth for parsing inner msgpack values within ext payloads: allows one more level before limit.
constexpr int mpack_ext_inner_start_depth = mpack_max_nesting_depth - 1;

// Integer length widths in bytes: https://github.com/msgpack/msgpack/blob/master/spec.md.
constexpr int mpack_int8_bytes = 1;
constexpr int mpack_int16_bytes = 2;
constexpr int mpack_int32_bytes = 4;
constexpr int mpack_int64_bytes = 8;

// Floating-point format sizes in bytes: https://github.com/msgpack/msgpack/blob/master/spec.md.
constexpr int mpack_float32_bytes = 4;
constexpr int mpack_float64_bytes = 8;

// String/array/map length header sizes in bytes: https://github.com/msgpack/msgpack/blob/master/spec.md.
constexpr int mpack_len8_bytes = 1;
constexpr int mpack_len16_bytes = 2;
constexpr int mpack_len32_bytes = 4;

// Fixed extension type payload sizes in bytes: https://github.com/msgpack/msgpack/blob/master/spec.md.
constexpr int mpack_fixext1_bytes = 1;
constexpr int mpack_fixext2_bytes = 2;
constexpr int mpack_fixext4_bytes = 4;
constexpr int mpack_fixext8_bytes = 8;
constexpr int mpack_fixext16_bytes = 16;

// Bits per byte for big-endian encoding/decoding.
constexpr int bits_per_byte = 8;

// MessagePack spec, formats (encoding boundaries and limits).
constexpr int64_t mpack_negative_fixint_max_value = -32;
constexpr uint8_t mpack_fixstr_max = 31;
constexpr uint8_t mpack_fixarray_max = 15;
constexpr uint8_t mpack_fixmap_max = 15;
constexpr uint8_t mpack_uint8_max = 0xff;
constexpr uint16_t mpack_uint16_max = 0xffff;
constexpr uint32_t mpack_uint32_max = 0xffffffffu;

class rv_editor_mpack_cursor
{
public:
    explicit rv_editor_mpack_cursor(std::string_view data) : data_(data) {}

    bool need(size_t n) const { return n <= data_.size() - pos_; }
    size_t pos() const { return pos_; }

    uint64_t be(int bytes)
    {
        uint64_t v = 0;
        for (int k = 0; k < bytes; ++k) {
            v = (v << bits_per_byte) | static_cast<uint8_t>(data_[pos_++]);
        }
        return v;
    }
    uint8_t byte() { return static_cast<uint8_t>(data_[pos_++]); }
    std::string_view take(size_t n)
    {
        const std::string_view s = data_.substr(pos_, n);
        pos_ += n;
        return s;
    }

private:
    std::string_view data_;
    size_t pos_ = 0;
};

// 1 read, 0 incomplete, -1 malformed.
int rv_editor_mpack_value(rv_editor_mpack_cursor &c, rv_editor_mpack &v, int depth);

int rv_editor_mpack_blob(rv_editor_mpack_cursor &c, rv_editor_mpack &v, int len_bytes)
{
    if (!c.need(static_cast<size_t>(len_bytes))) {
        return 0;
    }
    const size_t n = c.be(len_bytes);
    if (!c.need(n)) {
        return 0;
    }
    v.type = mtype::string;
    v.s = std::string(c.take(n));
    return 1;
}

int rv_editor_mpack_array(rv_editor_mpack_cursor &c, rv_editor_mpack &v, size_t n, int depth)
{
    v.type = mtype::array;
    for (size_t k = 0; k < n; ++k) {
        rv_editor_mpack item;
        const int r = rv_editor_mpack_value(c, item, depth + 1);
        if (r <= 0) {
            return r;
        }
        v.items.push_back(std::move(item));
    }
    return 1;
}

int rv_editor_mpack_map(rv_editor_mpack_cursor &c, rv_editor_mpack &v, size_t n, int depth)
{
    v.type = mtype::map;
    for (size_t k = 0; k < n; ++k) {
        rv_editor_mpack key;
        rv_editor_mpack val;
        int r = rv_editor_mpack_value(c, key, depth + 1);
        if (r <= 0) {
            return r;
        }
        r = rv_editor_mpack_value(c, val, depth + 1);
        if (r <= 0) {
            return r;
        }
        v.keys.push_back(std::move(key));
        v.items.push_back(std::move(val));
    }
    return 1;
}

int rv_editor_mpack_ext(rv_editor_mpack_cursor &c, rv_editor_mpack &v, size_t n)
{
    if (!c.need(1 + n)) {
        return 0;
    }
    v.type = mtype::ext;
    v.ext_type = static_cast<int8_t>(c.byte());
    // nvim's handles are a msgpack integer inside the ext payload.
    rv_editor_mpack_cursor inner(c.take(n));
    rv_editor_mpack id;
    if (n > 0 && rv_editor_mpack_value(inner, id, mpack_ext_inner_start_depth) == 1 &&
        id.is(mtype::integer)) {
        v.i = id.i;
    }
    return 1;
}

int rv_editor_mpack_value(rv_editor_mpack_cursor &c, rv_editor_mpack &v, int depth)
{
    if (depth > mpack_max_nesting_depth) {
        return -1;
    }
    if (!c.need(1)) {
        return 0;
    }
    const uint8_t t = c.byte();
    v = {};
    if (t <= mpack_positive_fixint_max) {
        v.type = mtype::integer;
        v.i = t;
        return 1;
    }
    if (t >= mpack_negative_fixint_min) {
        v.type = mtype::integer;
        v.i = static_cast<int8_t>(t);
        return 1;
    }
    if ((t & mpack_fixmap_mask) == mpack_fixmap_value) {
        return rv_editor_mpack_map(c, v, t & mpack_fixmap_size_mask, depth);
    }
    if ((t & mpack_fixarray_mask) == mpack_fixarray_value) {
        return rv_editor_mpack_array(c, v, t & mpack_fixarray_size_mask, depth);
    }
    if ((t & mpack_fixstr_mask) == mpack_fixstr_value) {
        const size_t n = t & mpack_fixstr_size_mask;
        if (!c.need(n)) {
            return 0;
        }
        v.type = mtype::string;
        v.s = std::string(c.take(n));
        return 1;
    }
    auto integer = [&](int bytes, bool is_signed) {
        if (!c.need(static_cast<size_t>(bytes))) {
            return 0;
        }
        const uint64_t raw = c.be(bytes);
        v.type = mtype::integer;
        if (!is_signed) {
            v.i = static_cast<int64_t>(raw);
        } else if (bytes == mpack_int8_bytes) {
            v.i = static_cast<int8_t>(raw);
        } else if (bytes == mpack_int16_bytes) {
            v.i = static_cast<int16_t>(raw);
        } else if (bytes == mpack_int32_bytes) {
            v.i = static_cast<int32_t>(raw);
        } else {
            v.i = static_cast<int64_t>(raw);
        }
        return 1;
    };
    auto length = [&](int bytes, size_t &n) {
        if (!c.need(static_cast<size_t>(bytes))) {
            return false;
        }
        n = c.be(bytes);
        return true;
    };
    size_t n = 0;
    switch (t) {
    case mpack_nil:
        v.type = mtype::nil;
        return 1;
    case mpack_false:
        v.type = mtype::boolean;
        v.b = false;
        return 1;
    case mpack_true:
        v.type = mtype::boolean;
        v.b = true;
        return 1;
    case mpack_uint8:
        return integer(mpack_int8_bytes, false);
    case mpack_uint16:
        return integer(mpack_int16_bytes, false);
    case mpack_uint32:
        return integer(mpack_int32_bytes, false);
    case mpack_uint64:
        return integer(mpack_int64_bytes, false);
    case mpack_int8:
        return integer(mpack_int8_bytes, true);
    case mpack_int16:
        return integer(mpack_int16_bytes, true);
    case mpack_int32:
        return integer(mpack_int32_bytes, true);
    case mpack_int64:
        return integer(mpack_int64_bytes, true);
    case mpack_float32: {
        if (!c.need(mpack_float32_bytes)) {
            return 0;
        }
        const uint32_t raw = static_cast<uint32_t>(c.be(mpack_float32_bytes));
        float f;
        std::memcpy(&f, &raw, mpack_float32_bytes);
        v.type = mtype::real;
        v.d = f;
        return 1;
    }
    case mpack_float64: {
        if (!c.need(mpack_float64_bytes)) {
            return 0;
        }
        const uint64_t raw = c.be(mpack_float64_bytes);
        v.type = mtype::real;
        std::memcpy(&v.d, &raw, mpack_float64_bytes);
        return 1;
    }
    case mpack_str8:
    case mpack_bin8:
        return rv_editor_mpack_blob(c, v, mpack_len8_bytes);
    case mpack_str16:
    case mpack_bin16:
        return rv_editor_mpack_blob(c, v, mpack_len16_bytes);
    case mpack_str32:
    case mpack_bin32:
        return rv_editor_mpack_blob(c, v, mpack_len32_bytes);
    case mpack_array16:
        return length(mpack_len16_bytes, n) ? rv_editor_mpack_array(c, v, n, depth) : 0;
    case mpack_array32:
        return length(mpack_len32_bytes, n) ? rv_editor_mpack_array(c, v, n, depth) : 0;
    case mpack_map16:
        return length(mpack_len16_bytes, n) ? rv_editor_mpack_map(c, v, n, depth) : 0;
    case mpack_map32:
        return length(mpack_len32_bytes, n) ? rv_editor_mpack_map(c, v, n, depth) : 0;
    case mpack_fixext1:
        return rv_editor_mpack_ext(c, v, mpack_fixext1_bytes);
    case mpack_fixext2:
        return rv_editor_mpack_ext(c, v, mpack_fixext2_bytes);
    case mpack_fixext4:
        return rv_editor_mpack_ext(c, v, mpack_fixext4_bytes);
    case mpack_fixext8:
        return rv_editor_mpack_ext(c, v, mpack_fixext8_bytes);
    case mpack_fixext16:
        return rv_editor_mpack_ext(c, v, mpack_fixext16_bytes);
    case mpack_ext8:
        return length(mpack_len8_bytes, n) ? rv_editor_mpack_ext(c, v, n) : 0;
    case mpack_ext16:
        return length(mpack_len16_bytes, n) ? rv_editor_mpack_ext(c, v, n) : 0;
    case mpack_ext32:
        return length(mpack_len32_bytes, n) ? rv_editor_mpack_ext(c, v, n) : 0;
    default:
        return -1;
    }
}

} // namespace

const rv_editor_mpack *rv_editor_mpack::get(std::string_view key) const
{
    for (size_t n = 0; n < keys.size() && n < items.size(); ++n) {
        if (keys[n].is(rv_editor_mpack_type::string) && keys[n].s == key) {
            return &items[n];
        }
    }
    return nullptr;
}

void rv_editor_mpack_writer::put_be(uint64_t v, int bytes)
{
    for (int k = bytes - 1; k >= 0; --k) {
        put(static_cast<uint8_t>(v >> (8 * k)));
    }
}

void rv_editor_mpack_writer::nil()
{
    put(mpack_nil);
}

void rv_editor_mpack_writer::boolean(bool v)
{
    put(v ? mpack_true : mpack_false);
}

void rv_editor_mpack_writer::integer(int64_t v)
{
    if (v >= 0 && v <= mpack_positive_fixint_max) {
        put(static_cast<uint8_t>(v));
    } else if (v < 0 && v >= mpack_negative_fixint_max_value) {
        put(static_cast<uint8_t>(v));
    } else if (v >= 0 && v <= mpack_uint32_max) {
        put(mpack_uint32);
        put_be(static_cast<uint64_t>(v), 4);
    } else {
        put(mpack_int64);
        put_be(static_cast<uint64_t>(v), 8);
    }
}

void rv_editor_mpack_writer::string(std::string_view v)
{
    if (v.size() <= mpack_fixstr_max) {
        put(static_cast<uint8_t>(mpack_fixstr_value | v.size()));
    } else if (v.size() <= mpack_uint8_max) {
        put(mpack_str8);
        put_be(v.size(), 1);
    } else if (v.size() <= mpack_uint16_max) {
        put(mpack_str16);
        put_be(v.size(), 2);
    } else {
        put(mpack_str32);
        put_be(v.size(), 4);
    }
    out_.append(v);
}

void rv_editor_mpack_writer::array(uint32_t count)
{
    if (count <= mpack_fixarray_max) {
        put(static_cast<uint8_t>(mpack_fixarray_value | count));
    } else if (count <= mpack_uint16_max) {
        put(mpack_array16);
        put_be(count, 2);
    } else {
        put(mpack_array32);
        put_be(count, 4);
    }
}

void rv_editor_mpack_writer::map(uint32_t count)
{
    if (count <= mpack_fixmap_max) {
        put(static_cast<uint8_t>(mpack_fixmap_value | count));
    } else if (count <= mpack_uint16_max) {
        put(mpack_map16);
        put_be(count, 2);
    } else {
        put(mpack_map32);
        put_be(count, 4);
    }
}

ptrdiff_t rv_editor_mpack_read(std::string_view data, rv_editor_mpack &value)
{
    rv_editor_mpack_cursor c(data);
    rv_editor_mpack v;
    const int r = rv_editor_mpack_value(c, v, 0);
    if (r < 0) {
        return -1;
    }
    if (r == 0) {
        return 0;
    }
    value = std::move(v);
    return static_cast<ptrdiff_t>(c.pos());
}

} // namespace rv_editor
