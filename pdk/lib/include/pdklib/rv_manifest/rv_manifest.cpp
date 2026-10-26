#include "rv_manifest.hpp"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_textures/rv_texfmt_name.hpp"

#include "detail/rv_manifest_binder.hpp"
#include "detail/rv_manifest_failer.hpp"
#include "detail/rv_manifest_lexer.hpp"
#include "detail/rv_manifest_parser.hpp"
#include "detail/rv_manifest_semantic.hpp"
#include "detail/rv_manifest_token.hpp"
#include "detail/rv_manifest_tree.hpp"
#include "detail/rv_manifest_schema.hpp"

namespace rv_pdklib
{
// --- rendering ----------------------------------------------------------------

// The inverse of parse_string(). Only the five escapes the parser knows are
// ever emitted, so anything this function produces is something it can read
// back - that is the whole round-trip contract in one sentence.
static std::string quote(const std::string &s)
{
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out += c;
            break;
        }
    }
    out += '"';
    return out;
}

// Arrays go on one line while they fit comfortably, and one element per line
// once they do not. Both shapes parse back identically; the split exists so a
// manifest with thirty globs stays readable in a diff.
constexpr std::size_t RV_MANIFEST_ARRAY_WRAP_COLUMN = 80;

static void render_array(std::ostringstream &out, const std::string &key, const std::vector<std::string> &values)
{
    std::string one_line = key + " = [";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            one_line += ", ";
        }
        one_line += quote(values[i]);
    }
    one_line += "]";
    if (one_line.size() <= RV_MANIFEST_ARRAY_WRAP_COLUMN) {
        out << one_line << "\n";
        return;
    }
    out << key << " = [\n";
    for (const std::string &v : values) {
        out << "    " << quote(v) << ",\n";
    }
    out << "]\n";
}
// The front of the pipeline, in the order of the stages. Each one hands the next
// a whole product and its complaints to one failer; only the binder is allowed
// to know what rv_manifest looks like.
int rv_manifest_parse(const std::string &text, const std::string &origin, rv_manifest &manifest, std::string &error)
{
    rv_manifest_failer failer;

    const std::vector<rv_manifest_token> tokens = rv_manifest_lex(text);

    rv_manifest_parser parser(tokens, failer);
    const rv_manifest_tree tree = parser.run();

    // Semantics only on a tree that parsed. After a broken line every judgement
    // about what the author meant is a guess, and a guess printed next to a real
    // error buries it.
    if (failer.empty()) {
        rv_manifest_check(tree, failer);
    }

    if (!failer.empty()) {
        error = failer.report(origin);
        return RV_ERR_INVAL;
    }

    manifest = rv_manifest_bind(tree);
    return RV_OK;
}

int rv_manifest_parse(const std::string &text, rv_manifest &manifest, std::string &error)
{
    return rv_manifest_parse(text, std::string(), manifest, error);
}

int rv_manifest_load(const std::string &path, rv_manifest &manifest, std::string &error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open manifest '" + path + "'";
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        error = "cannot read manifest '" + path + "'";
    }
    // The line number is only useful next to the file it belongs to, so the path
    // goes in before the diagnostics are rendered.
    return rv_manifest_parse(buffer.str(), path, manifest, error);
}

std::string rv_manifest_render(const rv_manifest &manifest)
{
    std::ostringstream out;

    // Every key of a present section is written, including empty arrays and zero
    // counts, except [build] sdk and [assets.stream], which are left out when empty.
    // This is the copy that goes ONTO THE DISC, and there it has to say
    // what the burner actually used rather than leaning on a default a later
    // version of the tool might change.
    //
    // That is exactly why the manifest an author WRITES may be four sections
    // long: what it leaves out is filled from the reference machine while it is
    // being baked, and what lands on the disc is the complete answer. Partial
    // going in, whole coming out.
    out << "[disc]\n";
    out << "id = " << quote(manifest.disc_id) << "\n";
    out << "title = " << quote(manifest.disc_title) << "\n";

    out << "\n[build]\n";
    render_array(out, "sources", manifest.build_sources);
    render_array(out, "defines", manifest.build_defines);
    render_array(out, "include_dirs", manifest.build_include_dirs);
    // No key without SDK trees: a console older than this key refuses an unknown one.
    if (!manifest.build_sdk.empty()) {
        render_array(out, "sdk", manifest.build_sdk);
    }

    out << "\n[scripts]\n";
    render_array(out, "sources", manifest.scripts_sources);

    out << "\n[assets]\n";
    render_array(out, "files", manifest.assets_files);

    // Always written, although its default is idx8: the copy on the disc names the format the burner baked
    // textures and model pictures in. An unknown format renders as "": parse reads it back as the unknown-spelling
    // marker, and rv_manifest_validate rejects it - the burner validates before it renders.
    const rv_pdklib::rv_texfmt_name *assets_texfmt = rv_pdklib::rv_texfmt_name::by_format(manifest.assets_texture_format);
    out << "\n[assets.texture]\n";
    out << "format = " << quote(assets_texfmt != nullptr ? assets_texfmt->text : "") << "\n";
    // Only when listed: a console older than this section refuses an unknown one.
    if (!manifest.assets_stream_files.empty()) {
        out << "\n[assets.stream]\n";
        render_array(out, "files", manifest.assets_stream_files);
    }

    const rv_manifest_budget &budget = manifest.budget;

    out << "\n[budget.pcca]\n";
    out << "voice_count = " << budget.pcca.voice_count << "\n";
    out << "sound_memory_size = " << budget.pcca.sound_memory_size << "\n";

    out << "\n[budget.pccv]\n";
    out << "screen_width = " << budget.pccv.screen_width << "\n";
    out << "screen_height = " << budget.pccv.screen_height << "\n";
    out << "texture_max_width = " << budget.pccv.texture_max_width << "\n";
    out << "texture_max_height = " << budget.pccv.texture_max_height << "\n";
    out << "video_memory_size = " << budget.pccv.video_memory_size << "\n";
    out << "frame_capacity = " << budget.pccv.frame_capacity << "\n";
    out << "ot_bucket_count = " << budget.pccv.ot_bucket_count << "\n";

    out << "\n[budget.pccg]\n";
    out << "model_memory_size = " << budget.pccg.model_memory_size << "\n";
    out << "triangle_capacity = " << budget.pccg.triangle_capacity << "\n";

    out << "\n[budget.pccio]\n";
    out << "iport_count = " << budget.pccio.iport_count << "\n";

    out << "\n[budget.pccm]\n";
    out << "card_slots = " << budget.pccm.card_slots << "\n";
    out << "card_slot_size = " << budget.pccm.card_slot_size << "\n";

    out << "\n[budget.pccd]\n";
    out << "code_entry = " << quote(budget.pccd.code_entry) << "\n";

    out << "\n[budget.pccl]\n";
    out << "script_memory_size = " << budget.pccl.script_memory_size << "\n";
    out << "script_entry = " << quote(budget.pccl.script_entry) << "\n";

    return out.str();
}

} // namespace rv_pdklib
