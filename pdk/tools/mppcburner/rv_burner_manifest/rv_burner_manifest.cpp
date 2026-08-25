#include "rv_burner_manifest.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "pdklib/rv_texfmt_name.hpp"

#include "rv_burner_manifest_binder.hpp"
#include "rv_burner_manifest_failer.hpp"
#include "rv_burner_manifest_lexer.hpp"
#include "rv_burner_manifest_parser.hpp"
#include "rv_burner_manifest_semantic.hpp"
#include "rv_burner_manifest_token.hpp"
#include "rv_burner_manifest_tree.hpp"
#include "rv_burner_schema.hpp"

namespace rv_pdktools
{
// --- rendering ----------------------------------------------------------------

// The inverse of parse_string(). Only the five escapes the parser knows are
// ever emitted, so anything this function produces is something it can read
// back — that is the whole round-trip contract in one sentence.
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
constexpr std::size_t kArrayWrapColumn = 80;

static void render_array(std::ostringstream &out, const std::string &key,
    const std::vector<std::string> &values)
{
    std::string one_line = key + " = [";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            one_line += ", ";
        }
        one_line += quote(values[i]);
    }
    one_line += "]";
    if (one_line.size() <= kArrayWrapColumn) {
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
// to know what rv_burner_manifest looks like.
int manifest_parse(const std::string &text,
    const std::string &origin,
    rv_burner_manifest &manifest,
    std::string &error)
{
    rv_burner_manifest_failer failer;

    const std::vector<rv_burner_token> tokens = rv_burner_manifest_lex(text);

    rv_burner_manifest_parser parser(tokens, failer);
    const rv_burner_tree tree = parser.run();

    // Semantics only on a tree that parsed. After a broken line every judgement
    // about what the author meant is a guess, and a guess printed next to a real
    // error buries it.
    if (failer.empty()) {
        rv_burner_manifest_check(tree, failer);
    }

    if (!failer.empty()) {
        error = failer.report(origin);
        return 1;
    }

    manifest = rv_burner_manifest_bind(tree);
    return 0;
}

int manifest_parse(const std::string &text,
    rv_burner_manifest &manifest,
    std::string &error)
{
    return manifest_parse(text, std::string(), manifest, error) != 0;
}

int manifest_load(const std::string &path,
    rv_burner_manifest &manifest,
    std::string &error)
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
    return manifest_parse(buffer.str(), path, manifest, error);
}

std::string manifest_render(const rv_burner_manifest &manifest)
{
    std::ostringstream out;

    // Every key is written, including empty arrays and zero counts. A rendered
    // manifest is also the copy that goes onto the disc, and there it has to say
    // what the burner actually used rather than leaning on a default that a
    // later version of the tool might change.
    out << "[disc]\n";
    out << "id = " << quote(manifest.disc_id) << "\n";
    out << "title = " << quote(manifest.disc_title) << "\n";

    out << "\n[build]\n";
    render_array(out, "sources", manifest.build_sources);
    render_array(out, "defines", manifest.build_defines);
    render_array(out, "include_dirs", manifest.build_include_dirs);

    out << "\n[scripts]\n";
    render_array(out, "sources", manifest.scripts_sources);

    out << "\n[assets]\n";
    render_array(out, "files", manifest.assets_files);

    // A manifest that reached rendering has been through the binder, so the
    // format is one of the enumerators; the empty string is what a corrupted
    // one would render as, and it fails to parse back rather than lying.
    const rv_pdklib::rv_texfmt_name *texfmt = rv_pdklib::rv_texfmt_name::by_format(
        manifest.textures_files.format);

    out << "\n[textures]\n";
    render_array(out, "files", manifest.textures_files.files);
    out << "format = " << quote(texfmt != nullptr ? texfmt->text : "") << "\n";

    out << "\n[budget]\n";
    out << "texture_max_width = " << manifest.budget.texture_max_width << "\n";
    out << "texture_max_height = " << manifest.budget.texture_max_height << "\n";
    out << "video_memory_size = " << manifest.budget.video_memory_size << "\n";

    return out.str();
}

bool manifest_validate(const rv_burner_manifest &manifest, std::string &error)
{
    error.clear();

    // The id is not decoration: it becomes the name of the burned file and of
    // the directory the console unpacks into, so it is checked as a FILENAME
    // before anything else touches it. A '/' would escape the output directory,
    // ".." would climb out of it, and a leading '.' would hide the result.
    if (manifest.disc_id.empty()) {
        error = "[disc] id is empty — the disc needs a short machine name";
        return false;
    }
    for (char c : manifest.disc_id) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) == 0 && c != '-' && c != '_') {
            error = "[disc] id '" + manifest.disc_id +
                "' is not a safe filename — use letters, digits, '-' and '_' only";
            return false;
        }
    }
    if (manifest.disc_id.front() == '.') { // unreachable through the loop above, kept as a guard
        error = "[disc] id must not start with '.'";
        return false;
    }

    // By the time a manifest reaches here the binder has already turned the
    // spelling into an enumerator, and an unknown spelling left the field at its
    // value-initialised zero. So this catches both a typo and a missing `format`
    // line — but it can no longer quote what the author actually typed, because
    // that string does not survive binding.
    if (rv_pdklib::rv_texfmt_name::by_format(manifest.textures_files.format) == nullptr) {
        std::string known;
        for (const rv_pdklib::rv_texfmt_name &row : rv_pdklib::rv_texfmt_names) {
            if (!known.empty()) {
                known += ", ";
            }
            known += row.text;
        }
        error = std::format("[textures] format is missing or unknown — expected one of {}", known);
        return false;
    }

    struct budget_field {
        const char *name;
        int64_t value;
    };
    const budget_field budgets[] = {
        { "texture_max_width", manifest.budget.texture_max_width },
        { "texture_max_height", manifest.budget.texture_max_height },
        { "video_memory_size", manifest.budget.video_memory_size },
    };

    // The budget has no defaults, so a manifest without the section arrives here
    // value-initialised — every field zero. Saying "texture_max_width must be
    // positive, got 0" about that is true and useless: nothing is wrong with the
    // value, the whole section was never written. Complain about what is actually
    // missing before looking at the fields one by one.
    std::size_t unset = 0;
    for (const budget_field &b : budgets) {
        if (b.value == 0) {
            ++unset;
        }
    }
    if (unset == std::size(budgets)) {
        std::string keys;
        for (const budget_field &b : budgets) {
            if (!keys.empty()) {
                keys += ", ";
            }
            keys += b.name;
        }
        error = std::format("[budget] section is missing — state {}", keys);
        return false;
    }

    for (const budget_field &b : budgets) {
        if (b.value <= 0) {
            error = std::format("[budget] {} must be positive, got {}", b.name, b.value);
            return false;
        }
    }

    return true;
}

} // namespace rv_pdktools
