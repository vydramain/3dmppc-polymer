#include "rv_pconsole/cv/rv_pccv_sdl3.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdio>
#include <limits>

#include "pdk/cv/rv_cv.h"
#include "pdk/cv/rv_pipeline.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_textures/rv_texfmt_name.hpp"
#include "rv_pconsole/cv/rv_pcraster.hpp"

namespace rv_3dmppc
{

namespace
{

// The only bits frame_configure() accepts today. Anything else is a disc built
// against a newer contract than this console implements, and the contract says
// that is RV_ERR_INVAL rather than "ignore what you do not understand".
constexpr uint64_t RV_PCCV_CONFIG_KNOWN_BITS =
    static_cast<uint64_t>(RV_PIPELINE_BUFFER_CONFIG_TYPE_Z);

// The value the depth page is cleared to: the FARTHEST representable key, so
// the first primitive to touch a pixel always passes the test
// (rv_pcfbuf::depth_accept keeps a pixel when depth >= stored). It is
// deliberately not conf_.depth_min — a disc may legally hand a depth below the
// ordering table's window (those clamp to the nearest bucket), and such a
// primitive must still be able to write a pixel.
constexpr int32_t RV_PCCV_DEPTH_FARTHEST = std::numeric_limits<int32_t>::min();

} // namespace

rv_pccv_sdl3::rv_pccv_sdl3(const rv_pccv_conf &conf, rv_pchost_sdl3 &host)
    : conf_(conf)
    , host_(host)
    , fbuf_(conf.screen_width, conf.screen_height)
    , vram_(conf.video_memory_size)
    , otable_(conf.ot_bucket_count, conf.depth_min, conf.depth_max)
{
    // Reserve the whole frame budget up front. frame_put() runs inside the
    // disc's frame loop, and a vector growth there would be an allocation (and
    // a copy of every primitive filed so far) at an unpredictable frame.
    if (conf_.frame_capacity > 0) {
        primitives_.reserve(static_cast<size_t>(conf_.frame_capacity));
    }
}

rv_pccv_sdl3::~rv_pccv_sdl3()
{
    // Texture before renderer: destroying the renderer first would leave the
    // texture handle dangling. The window they were built on belongs to
    // host_, which outlives this call (see the declaration order note on the
    // destructor's declaration in rv_pccv_sdl3.hpp).
    if (texture_) {
        SDL_DestroyTexture(texture_);
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
    }
}

// --- hardware geometry -------------------------------------------------------

int64_t rv_pccv_sdl3::screen_width()
{
    return conf_.screen_width;
}
int64_t rv_pccv_sdl3::screen_height()
{
    return conf_.screen_height;
}
int64_t rv_pccv_sdl3::texture_max_width()
{
    return conf_.texture_max_width;
}
int64_t rv_pccv_sdl3::texture_max_height()
{
    return conf_.texture_max_height;
}
int64_t rv_pccv_sdl3::video_memory_size()
{
    return conf_.video_memory_size;
}
int64_t rv_pccv_sdl3::frame_capacity()
{
    return conf_.frame_capacity;
}

// --- video RAM ---------------------------------------------------------------

// Pure delegation: the pool owns "is there room", "which address", "is this
// address live". Reproducing any of that here would give the console two
// answers to the same question.
int64_t rv_pccv_sdl3::video_asset_malloc(int64_t size)
{
    return vram_.malloc(size);
}

int64_t rv_pccv_sdl3::video_asset_free(int64_t addr)
{
    return vram_.free(addr);
}

bool rv_pccv_sdl3::texture_format_known(rv_texfmt format)
{
    if (rv_pdklib::rv_texfmt_name::by_format(format) != nullptr) {
        return true;
    }
    return false;
}

// The console's own limits are checked HERE, before the bytes reach the pool:
// the pool knows how many bytes fit in a region, but the texture *shape* limit
// is hardware geometry this class publishes (texture_max_width/height), so this
// is the one place that can enforce it.
int64_t rv_pccv_sdl3::video_asset_write(int64_t addr, const rv_texture *texture_ptr)
{
    if (texture_ptr == nullptr) {
        return RV_ERR_INVAL;
    }
    const rv_texture &texture = *texture_ptr;

    if (!texture_format_known(texture.format)) {
        return RV_ERR_INVAL;
    }

    // The limits are int64 configuration and the dimensions are unsigned data;
    // widen the limit into the data's domain (a non-positive limit means "no
    // texture is acceptable") so the comparison never mixes signedness.
    const uint64_t max_width =
        conf_.texture_max_width > 0 ? static_cast<uint64_t>(conf_.texture_max_width) : 0;
    const uint64_t max_height =
        conf_.texture_max_height > 0 ? static_cast<uint64_t>(conf_.texture_max_height) : 0;

    if (texture.width > max_width || texture.height > max_height) {
        return RV_ERR_INVAL;
    }

    // The shape must be BACKED by the bytes. Nothing else checks this: the pool
    // only knows the region is big enough to hold `size`, and the sampler
    // afterwards trusts width/height to address texels. A disc that declared
    // 64x64 and uploaded eight bytes would sample whatever else lives in the
    // pool — inside the allocation, so no crash, just another region's texels
    // appearing inside this one. Rejecting here turns that into the RV_ERR_INVAL
    // the contract already promises for "the data does not fit".
    const uint64_t texels = texture.width * texture.height;
    uint64_t needed = 0;
    switch (texture.format) {
    case RV_TEXFMT_IDX4:
        // Rows stay byte-aligned, so an odd width costs a padding nibble.
        needed = ((texture.width + 1) / 2) * texture.height;
        break;
    case RV_TEXFMT_IDX8:
        needed = texels;
        break;
    case RV_TEXFMT_DIRECT15:
        needed = texels * 2;
        break;
    }
    if (texture.size < needed) {
        return RV_ERR_INVAL;
    }

    // Everything left — unknown address, null source, data that overruns the
    // region — is the pool's business.
    return vram_.write(addr, texture);
}

// --- the frame ---------------------------------------------------------------

void rv_pccv_sdl3::frame_reset()
{
    primitives_.clear();
    otable_.reset();
}

int64_t rv_pccv_sdl3::frame_configure(uint64_t config, rv_color clear_color)
{
    if ((config & ~RV_PCCV_CONFIG_KNOWN_BITS) != 0) {
        return RV_ERR_INVAL;
    }

    clear_color_ = clear_color;
    z_enabled_ = (config & static_cast<uint64_t>(RV_PIPELINE_BUFFER_CONFIG_TYPE_Z)) != 0;

    // Configuration opens the frame, so anything a previous frame left behind
    // (a disc that filed primitives and never flushed) dies here rather than
    // leaking into the frame being built.
    frame_reset();
    return RV_OK;
}

int64_t rv_pccv_sdl3::check_fill(uint32_t fill_mode, int64_t addr_texture, int64_t addr_palette) const
{
    switch (fill_mode) {
    case RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED:
    case RV_PRIMITIVE_FILL_MODE_WIREFRAME:
        // Neither mode reads an address, so an address field left at zero —
        // the common case for a zero-initialized primitive — is not an
        // error here and must not be validated.
        return RV_OK;

    case RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE:
        break;

    default:
        return RV_ERR_INVAL;
    }

    // The contract is explicit: "an address the primitive names is unknown" is
    // RV_ERR_INVAL. Checking it at PUT time and not at draw time is what makes
    // the error reachable at all — by flush the disc has long moved on and has
    // no error channel left to hear about it through.
    if (!vram_.region_exists(addr_texture)) {
        return RV_ERR_INVAL;
    }

    // A palette is required only by the indexed formats (rv_texture.hpp): the
    // direct format carries its colour in the texel and ignores addr_palette.
    // An unwritten region has no format yet, so nothing about its palette can
    // be demanded — that upload is still the disc's to make.
    const int64_t format = vram_.region_format(addr_texture);
    if (format == RV_TEXFMT_IDX4 || format == RV_TEXFMT_IDX8) {
        if (!vram_.region_exists(addr_palette)) {
            return RV_ERR_INVAL;
        }
    }

    return RV_OK;
}

int64_t rv_pccv_sdl3::frame_put(const rv_primitive *primitive_ptr)
{
    if (primitive_ptr == nullptr) {
        return RV_ERR_INVAL;
    }
    const rv_primitive &primitive = *primitive_ptr;

    // Validate BEFORE the capacity test, so a malformed primitive reports what
    // is wrong with it rather than being masked by a full buffer.
    switch (primitive.type) {
    case RV_PRIMITIVE_LINE:
        // A line has neither a fill mode nor an address: the segment is
        // always drawn from its two vertex colours.
        break;

    case RV_PRIMITIVE_POLYGON: {
        const rv_polygon &polygon = primitive.data.polygon;
        if (polygon.vertex_count != 3 && polygon.vertex_count != 4) {
            return RV_ERR_INVAL;
        }
        const int64_t fill =
            check_fill(polygon.fill_mode, polygon.addr_texture, polygon.addr_palette);
        if (fill < 0) {
            return fill;
        }
        break;
    }

    case RV_PRIMITIVE_SPRITE: {
        const rv_sprite &sprite = primitive.data.sprite;
        const int64_t fill =
            check_fill(sprite.fill_mode, sprite.addr_texture, sprite.addr_palette);
        if (fill < 0) {
            return fill;
        }
        break;
    }

    default:
        return RV_ERR_INVAL;
    }

    if (static_cast<int64_t>(primitives_.size()) >= conf_.frame_capacity) {
        return RV_ERR_NOMEM;
    }

    // File the command and its ordering key. The index is the primitive's
    // position in `primitives_`, which is also its submission order — that is
    // what lets the ordering table keep ties in submission order for free.
    const int32_t index = static_cast<int32_t>(primitives_.size());
    primitives_.push_back(primitive);
    otable_.insert(primitive.depth, index);

    return RV_OK;
}

void rv_pccv_sdl3::texture_addresses(const rv_primitive &primitive, int64_t &addr_texture,
    int64_t &addr_palette)
{
    addr_texture = 0;
    addr_palette = 0;

    switch (primitive.type) {
    case RV_PRIMITIVE_POLYGON:
        if (primitive.data.polygon.fill_mode == RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE) {
            addr_texture = primitive.data.polygon.addr_texture;
            addr_palette = primitive.data.polygon.addr_palette;
        }
        break;
    case RV_PRIMITIVE_SPRITE:
        if (primitive.data.sprite.fill_mode == RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE) {
            addr_texture = primitive.data.sprite.addr_texture;
            addr_palette = primitive.data.sprite.addr_palette;
        }
        break;
    default:
        break; // a line is never textured (rv_primitives.hpp)
    }
}

rv_pctexview rv_pccv_sdl3::texture_view(const rv_primitive &primitive) const
{
    rv_pctexview view; // invalid until every piece is found

    int64_t addr_texture = 0;
    int64_t addr_palette = 0;
    texture_addresses(primitive, addr_texture, addr_palette);
    if (addr_texture == 0) {
        return view;
    }

    // Every one of these can fail benignly: the disc may have freed the region
    // after filing the primitive, or allocated it and never uploaded. The answer
    // is the same in all cases — an invalid view, which the rasterizer draws as
    // a flat fill.
    const int64_t format = vram_.region_format(addr_texture);
    const int64_t width = vram_.region_width(addr_texture);
    const int64_t height = vram_.region_height(addr_texture);
    if (format < 0 || width <= 0 || height <= 0) {
        return view;
    }

    const uint8_t *texels = vram_.region_data(addr_texture);
    if (texels == nullptr) {
        return view;
    }

    view.texels = texels;
    view.format = static_cast<rv_texfmt>(format);
    view.width = width;
    view.height = height;

    if (format == RV_TEXFMT_IDX4 || format == RV_TEXFMT_IDX8) {
        const uint8_t *entries = vram_.region_data(addr_palette);
        const int64_t count = vram_.region_width(addr_palette);
        if (entries == nullptr || count <= 0) {
            view.texels = nullptr; // indexed without a palette is unsamplable
            return view;
        }

        // A palette is uploaded as an array of 16-bit entries (rv_texture.hpp:
        // format DIRECT15, width = entry count, height = 1). Every region starts
        // on a 4-byte boundary (rv_pcvram.cpp), so the cast is aligned; the
        // sampler still bounds-checks every index against `palette_count`,
        // because a disc may legally upload a short palette and an IDX8 texel
        // can name entry 255 regardless.
        view.palette = reinterpret_cast<const uint16_t *>(entries);
        view.palette_count = count;
    }

    return view;
}

int64_t rv_pccv_sdl3::frame_flush()
{
    // THEOREM: painter's algorithm, with the ordering table's residual
    // ambiguity resolved by the Z buffer. Drawing far-to-near is correct
    // BETWEEN buckets — whatever the last bucket writes covers everything the
    // earlier ones wrote, which is what "larger depth = nearer = on top" means.
    // It is NOT correct INSIDE a bucket: the table quantizes the depth window
    // (65536 distinct keys on the reference console) into ot_bucket_count
    // buckets (1024), so 64 different depths collapse into one bucket and their
    // relative order there is submission order, not depth order. Two primitives
    // 1 depth unit apart therefore paint in whatever order the disc happened to
    // file them, and a nearer one filed first ends up underneath.
    //
    // RV_PIPELINE_BUFFER_CONFIG_TYPE_Z exists exactly for that residue: it
    // makes each pixel carry the depth of the primitive that wrote it, so a
    // later primitive from the SAME bucket with a smaller depth is rejected per
    // pixel instead of overwriting. Finer buckets would shrink the ambiguity
    // but never remove it (the key is 32-bit, the table is not), which is why
    // the flag is a real feature and not a workaround for a small table.
    fbuf_.clear(rv_pcraster::pack_rgb555(clear_color_), RV_PCCV_DEPTH_FARTHEST, z_enabled_);

    otable_.for_each_far_to_near([this](int32_t index) {
        const rv_primitive &primitive = primitives_[static_cast<size_t>(index)];

        // The view is resolved per primitive, at DRAW time and not at put time:
        // a primitive filed early in the frame may name a region the disc
        // uploaded into later in the same frame, and the contract's ordering
        // makes no promise about which happens first.
        rv_pcraster::draw(fbuf_, primitive, texture_view(primitive), z_enabled_);
    });

    present(fbuf_.expand_argb());

    // The next frame starts empty. The clear colour and the Z flag go back to
    // their defaults too: frame_configure is per-frame state, and a frame that
    // never configures itself gets a black, ordering-table-only frame rather
    // than inheriting whatever the previous frame chose.
    frame_reset();
    clear_color_ = rv_color{ 0, 0, 0 };
    z_enabled_ = false;

    return RV_OK;
}

// --- presentation -------------------------------------------------------------

int64_t rv_pccv_sdl3::screen_open(const char *title, uint64_t scale)
{
    const int64_t opened = host_.open(title, conf_.screen_width, conf_.screen_height, scale);
    if (opened < 0) {
        return opened;
    }

    renderer_ = SDL_CreateRenderer(host_.window(), nullptr);
    if (!renderer_) {
        RV_LOG_ERR("pccv", "SDL_CreateRenderer failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }

    // The frame is always the native console resolution; the window is just a
    // magnifying glass over it. Integer scaling keeps the pixels square and
    // crisp instead of smearing them across a non-multiple window size.
    SDL_SetRenderLogicalPresentation(renderer_, static_cast<int>(conf_.screen_width),
        static_cast<int>(conf_.screen_height),
        SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        static_cast<int>(conf_.screen_width),
        static_cast<int>(conf_.screen_height));
    if (!texture_) {
        RV_LOG_ERR("pccv", "SDL_CreateTexture failed: {}", SDL_GetError());
        return RV_ERR_IO;
    }
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);

    return RV_OK;
}

void rv_pccv_sdl3::present(const uint32_t *argb)
{
    if (!argb) {
        return;
    }

    // Recorded before the renderer/texture check so a run with cv=null (no
    // window, no texture) still remembers the pointer --dump-frame needs.
    // The bytes dumped are exactly the bytes handed to the display when there
    // is one — no second path that could drift.
    last_frame_ = argb;

    if (!renderer_ || !texture_) {
        return;
    }

    SDL_UpdateTexture(texture_, nullptr, argb, static_cast<int>(conf_.screen_width * 4));
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

// Write the presented frame out as a binary PPM. A developer-tooling
// convenience: it lets the exact pixels the console produced be inspected or
// diffed without a screen capture, which is the difference between "looks
// right to me" and a repeatable check. Deliberately the LAST frame rather
// than the first — an animated disc has usually settled by then.
void rv_pccv_sdl3::dump_last_frame(const std::string &path) const
{
    if (path.empty() || !last_frame_) {
        return;
    }

    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (!file) {
        RV_LOG_ERR("pccv", "cannot open frame dump '{}'", path);
        return;
    }

    std::fprintf(file, "P6\n%lld %lld\n255\n", static_cast<long long>(conf_.screen_width),
        static_cast<long long>(conf_.screen_height));

    const int64_t pixels = conf_.screen_width * conf_.screen_height;
    for (int64_t i = 0; i < pixels; ++i) {
        const uint32_t c = last_frame_[i];
        const unsigned char rgb[3] = { static_cast<unsigned char>((c >> 16) & 0xFF),
            static_cast<unsigned char>((c >> 8) & 0xFF),
            static_cast<unsigned char>(c & 0xFF) };
        std::fwrite(rgb, 1, sizeof(rgb), file);
    }
    std::fclose(file);
    RV_LOG_INFO("pccv", "frame written to '{}'", path);
}

} // namespace rv_3dmppc
