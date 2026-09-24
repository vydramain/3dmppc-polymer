// msgpack: the encoder and the incremental decoder nvim's RPC stream needs.

#include "nvim/rv_editor_msgpack.hpp"

#include <cstring>

namespace rv_editor
{

namespace
{

using mtype = rv_editor_mpack::rv_editor_mpack_type;

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
            v = (v << 8) | static_cast<uint8_t>(data_[pos_++]);
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
    if (n > 0 && rv_editor_mpack_value(inner, id, 63) == 1 && id.is(mtype::integer)) {
        v.i = id.i;
    }
    return 1;
}

int rv_editor_mpack_value(rv_editor_mpack_cursor &c, rv_editor_mpack &v, int depth)
{
    if (depth > 64) {
        return -1;
    }
    if (!c.need(1)) {
        return 0;
    }
    const uint8_t t = c.byte();
    v = {};
    if (t <= 0x7f) {
        v.type = mtype::integer;
        v.i = t;
        return 1;
    }
    if (t >= 0xe0) {
        v.type = mtype::integer;
        v.i = static_cast<int8_t>(t);
        return 1;
    }
    if ((t & 0xf0) == 0x80) {
        return rv_editor_mpack_map(c, v, t & 0x0f, depth);
    }
    if ((t & 0xf0) == 0x90) {
        return rv_editor_mpack_array(c, v, t & 0x0f, depth);
    }
    if ((t & 0xe0) == 0xa0) {
        const size_t n = t & 0x1f;
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
        } else if (bytes == 1) {
            v.i = static_cast<int8_t>(raw);
        } else if (bytes == 2) {
            v.i = static_cast<int16_t>(raw);
        } else if (bytes == 4) {
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
        case 0xc0: v.type = mtype::nil; return 1;
        case 0xc2: v.type = mtype::boolean; v.b = false; return 1;
        case 0xc3: v.type = mtype::boolean; v.b = true; return 1;
        case 0xcc: return integer(1, false);
        case 0xcd: return integer(2, false);
        case 0xce: return integer(4, false);
        case 0xcf: return integer(8, false);
        case 0xd0: return integer(1, true);
        case 0xd1: return integer(2, true);
        case 0xd2: return integer(4, true);
        case 0xd3: return integer(8, true);
        case 0xca: {
            if (!c.need(4)) {
                return 0;
            }
            const uint32_t raw = static_cast<uint32_t>(c.be(4));
            float f;
            std::memcpy(&f, &raw, 4);
            v.type = mtype::real;
            v.d = f;
            return 1;
        }
        case 0xcb: {
            if (!c.need(8)) {
                return 0;
            }
            const uint64_t raw = c.be(8);
            v.type = mtype::real;
            std::memcpy(&v.d, &raw, 8);
            return 1;
        }
        case 0xd9:
        case 0xc4: return rv_editor_mpack_blob(c, v, 1);
        case 0xda:
        case 0xc5: return rv_editor_mpack_blob(c, v, 2);
        case 0xdb:
        case 0xc6: return rv_editor_mpack_blob(c, v, 4);
        case 0xdc: return length(2, n) ? rv_editor_mpack_array(c, v, n, depth) : 0;
        case 0xdd: return length(4, n) ? rv_editor_mpack_array(c, v, n, depth) : 0;
        case 0xde: return length(2, n) ? rv_editor_mpack_map(c, v, n, depth) : 0;
        case 0xdf: return length(4, n) ? rv_editor_mpack_map(c, v, n, depth) : 0;
        case 0xd4: return rv_editor_mpack_ext(c, v, 1);
        case 0xd5: return rv_editor_mpack_ext(c, v, 2);
        case 0xd6: return rv_editor_mpack_ext(c, v, 4);
        case 0xd7: return rv_editor_mpack_ext(c, v, 8);
        case 0xd8: return rv_editor_mpack_ext(c, v, 16);
        case 0xc7: return length(1, n) ? rv_editor_mpack_ext(c, v, n) : 0;
        case 0xc8: return length(2, n) ? rv_editor_mpack_ext(c, v, n) : 0;
        case 0xc9: return length(4, n) ? rv_editor_mpack_ext(c, v, n) : 0;
        default: return -1;
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
    put(0xc0);
}

void rv_editor_mpack_writer::boolean(bool v)
{
    put(v ? 0xc3 : 0xc2);
}

void rv_editor_mpack_writer::integer(int64_t v)
{
    if (v >= 0 && v <= 0x7f) {
        put(static_cast<uint8_t>(v));
    } else if (v < 0 && v >= -32) {
        put(static_cast<uint8_t>(v));
    } else if (v >= 0 && v <= 0xffffffffll) {
        put(0xce);
        put_be(static_cast<uint64_t>(v), 4);
    } else {
        put(0xd3);
        put_be(static_cast<uint64_t>(v), 8);
    }
}

void rv_editor_mpack_writer::string(std::string_view v)
{
    if (v.size() <= 31) {
        put(static_cast<uint8_t>(0xa0 | v.size()));
    } else if (v.size() <= 0xff) {
        put(0xd9);
        put_be(v.size(), 1);
    } else if (v.size() <= 0xffff) {
        put(0xda);
        put_be(v.size(), 2);
    } else {
        put(0xdb);
        put_be(v.size(), 4);
    }
    out_.append(v);
}

void rv_editor_mpack_writer::array(uint32_t count)
{
    if (count <= 15) {
        put(static_cast<uint8_t>(0x90 | count));
    } else if (count <= 0xffff) {
        put(0xdc);
        put_be(count, 2);
    } else {
        put(0xdd);
        put_be(count, 4);
    }
}

void rv_editor_mpack_writer::map(uint32_t count)
{
    if (count <= 15) {
        put(static_cast<uint8_t>(0x80 | count));
    } else if (count <= 0xffff) {
        put(0xde);
        put_be(count, 2);
    } else {
        put(0xdf);
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
