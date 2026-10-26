// glTF image reading, compiled by mppcmodbaker. A PNG is read whole, from a bufferView or a file;
// a file is checked by its signature.
#include "rv_mppcmod_gltf_image.hpp"

#include "cgltf.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"

namespace rv_pdklib::detail
{

namespace
{

constexpr std::string_view RV_MPPCMOD_GLTF_MIME_PNG = "image/png";
constexpr std::string_view RV_MPPCMOD_GLTF_MIME_JPEG = "image/jpeg";
constexpr std::string_view RV_MPPCMOD_GLTF_TEXTURE_EXT = ".mppctex";
// A data: URI is embedded in the file, so it is not an external buffer.
constexpr std::string_view RV_MPPCMOD_GLTF_DATA_URI = "data:";
// The eight bytes that open every PNG file.
constexpr std::string_view RV_MPPCMOD_GLTF_PNG_SIGNATURE = "\x89PNG\r\n\x1a\n";
// A PNG file is read whole, so its size is bounded; the limit is far above any console texture.
constexpr std::uintmax_t RV_MPPCMOD_GLTF_MAX_PNG_BYTES = 8 * 1024 * 1024;

// The URI as cgltf reads it, with %XX escapes resolved.
std::string decoded_uri(const char *uri)
{
    std::vector<char> buf(uri, uri + std::string_view(uri).size() + 1);
    cgltf_decode_uri(buf.data());
    return buf.data();
}

// data: is embedded. A file must be non-empty, not start with '/' or '\', and have no ':' before its first '/'.
bool is_allowed_uri(const char *uri)
{
    if (uri == nullptr || std::string_view(uri).starts_with(RV_MPPCMOD_GLTF_DATA_URI)) {
        return true;
    }
    const std::string path = decoded_uri(uri);
    if (path.empty() || path[0] == '/' || path[0] == '\\') {
        return false;
    }
    return path.substr(0, path.find('/')).find(':') == std::string::npos;
}

// The PNG bytes of an image: its bufferView (a .glb embeds it), else the regular file its URI names.
int image_bytes(const std::string &dir,
    const cgltf_image &image,
    const std::string &where,
    std::vector<uint8_t> &out,
    std::string &error)
{
    const cgltf_buffer_view *view = image.buffer_view;
    const auto *src = view != nullptr ? static_cast<const uint8_t *>(cgltf_buffer_view_data(view)) : nullptr;
    if (src != nullptr) {
        out.assign(src, src + view->size);
        return RV_OK;
    }
    if (view != nullptr) {
        error = where + " has no data";
        return RV_ERR_INVAL;
    }
    if (image.uri == nullptr || std::string_view(image.uri).starts_with(RV_MPPCMOD_GLTF_DATA_URI)) {
        error = where + " has no bufferView or file; the baker reads images from one (a .glb embeds them)";
        return RV_ERR_INVAL;
    }
    const std::filesystem::path file = std::filesystem::path(dir) / decoded_uri(image.uri);
    // Only a regular file of bounded size is read: a FIFO, a device or a directory would block or never end.
    std::error_code ec;
    const bool regular = std::filesystem::is_regular_file(file, ec);
    const std::uintmax_t size = regular ? std::filesystem::file_size(file, ec) : 0;
    if (!regular || ec || size > RV_MPPCMOD_GLTF_MAX_PNG_BYTES) {
        error = where + " file '" + image.uri + "' is missing, not a regular file, or over the size limit";
        return RV_ERR_INVAL;
    }
    std::ifstream in(file, std::ios::binary);
    out.resize(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(size))) {
        error = where + " file '" + image.uri + "' cannot be read";
        return RV_ERR_INVAL;
    }
    if (!std::string_view(reinterpret_cast<const char *>(out.data()), out.size()).starts_with(RV_MPPCMOD_GLTF_PNG_SIGNATURE)) {
        error = where + " file '" + image.uri + "' is not PNG";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

} // namespace

// Every buffer and image URI must pass is_allowed_uri; the error names the first that does not.
int check_uris(const cgltf_data *data, std::string &error)
{
    for (size_t b = 0; b < data->buffers_count; ++b) {
        if (!is_allowed_uri(data->buffers[b].uri)) {
            error = "buffer " + std::to_string(b) + " has a URI that is neither a relative file path nor data:";
            return RV_ERR_INVAL;
        }
    }
    for (size_t i = 0; i < data->images_count; ++i) {
        if (!is_allowed_uri(data->images[i].uri)) {
            error = "image " + std::to_string(i) + " has a URI that is neither a relative file path nor data:";
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// Names the texture of `material` and keeps its PNG once. Leaves `texture` empty when
// the material has none.
int bake_texture(image_sink &sink,
    const cgltf_material *material,
    const std::string &where,
    std::string &texture,
    std::string &error)
{
    texture.clear();
    if (material == nullptr || !material->has_pbr_metallic_roughness) {
        return RV_OK;
    }
    const cgltf_texture_view &view = material->pbr_metallic_roughness.base_color_texture;
    if (view.texture == nullptr) {
        return RV_OK;
    }
    if (view.texcoord != 0) {
        error = where + " uses TEXCOORD_" + std::to_string(view.texcoord) + " for its texture; only TEXCOORD_0 is baked";
        return RV_ERR_INVAL;
    }
    const cgltf_image *image = view.texture->image;
    if (image == nullptr) {
        error = "has a texture with no image";
        return RV_ERR_INVAL;
    }
    const size_t index = static_cast<size_t>(image - sink.data->images);
    const std::string image_where = "image " + std::to_string(index);
    const std::string_view mime = image->mime_type != nullptr ? std::string_view(image->mime_type) : std::string_view();
    // A file may omit its MIME type; its signature is checked when it is read.
    const bool mime_ok = mime == RV_MPPCMOD_GLTF_MIME_PNG || (image->buffer_view == nullptr && mime.empty());
    if (!mime_ok) {
        const std::string kind = mime == RV_MPPCMOD_GLTF_MIME_JPEG ? "JPEG" : mime.empty() ? "no MIME type" : std::string(mime);
        error = image_where + " is " + kind + ", not PNG; export the model with PNG textures";
        return RV_ERR_INVAL;
    }

    const std::string name = sink.stem + "-" + std::to_string(index);
    if (!sink.image_seen[index]) {
        rv_mppcmod_gltf_image out;
        out.name = name;
        const int rc = image_bytes(sink.dir, *image, image_where, out.png, error);
        if (rc != RV_OK) {
            return rc;
        }
        sink.images->push_back(std::move(out));
        sink.image_seen[index] = true;
    }
    texture = name + std::string(RV_MPPCMOD_GLTF_TEXTURE_EXT);
    return RV_OK;
}

} // namespace rv_pdklib::detail
