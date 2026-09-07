#include "rv_manifest.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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
// to know what rv_manifest looks like.
int rv_manifest_parse(const std::string &text,
    const std::string &origin,
    rv_manifest &manifest,
    std::string &error)
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
        return 1;
    }

    manifest = rv_manifest_bind(tree);
    return 0;
}

int rv_manifest_parse(const std::string &text,
    rv_manifest &manifest,
    std::string &error)
{
    return rv_manifest_parse(text, std::string(), manifest, error) != 0;
}

int rv_manifest_load(const std::string &path,
    rv_manifest &manifest,
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
    return rv_manifest_parse(buffer.str(), path, manifest, error);
}

std::string rv_manifest_render(const rv_manifest &manifest)
{
    std::ostringstream out;

    // EVERY key is written, including empty arrays and zero counts — no
    // section is an exception and none is omitted. This is the copy that goes
    // ONTO THE DISC, and there it has to say what the burner actually used
    // rather than leaning on a default a later version of the tool might
    // change.
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

// One name the drive will be asked for verbatim. Empty passes: it means the
// key was not written, and each caller decides what that absence means.
static bool safe_entry_name(const std::string &entry, const char *what, std::string &error)
{
    if (entry.empty()) {
        return true;
    }
    if (entry.front() == '/' || entry.find("..") != std::string::npos ||
        entry.find('/') != std::string::npos || entry.find('\\') != std::string::npos) {
        error = std::string(what) + " '" + entry +
            "' is not a safe entry name — it must not be a path";
        return false;
    }
    for (char c : entry) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) == 0 && c != '-' && c != '_' && c != '.') {
            error = std::string(what) + " '" + entry +
                "' is not a safe entry name — use letters, digits, '-', '_' and '.' only";
            return false;
        }
    }
    return true;
}

bool rv_manifest_validate(const rv_manifest &manifest, std::string &error)
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

    // The rest of what a disc cannot be built without. Everything NOT listed
    // here has a reference value and may be left unsaid; these five have none,
    // because no answer the tool could invent would be the author's.
    if (manifest.disc_title.empty()) {
        error = "[disc] title is empty — the disc needs a human title for the window and logs";
        return false;
    }
    if (manifest.build_sources.empty()) {
        error = "[build] sources is empty — a disc is built from its own code";
        return false;
    }
    if (manifest.assets_files.empty()) {
        error = "[assets] files is empty — state the files the disc carries";
        return false;
    }
    if (manifest.textures_files.files.empty()) {
        error = "[textures] files is empty — state the images the disc carries";
        return false;
    }

    // pccd.code_entry and pccl.script_entry both name something the drive is
    // later asked for BY NAME
    // — an empty value is legal and means "use the conventional name", but
    // anything that IS given must be a bare name: no path to climb out of the
    // medium with.
    if (!safe_entry_name(manifest.budget.pccd.code_entry, "[budget.pccd] code_entry", error) ||
        !safe_entry_name(manifest.budget.pccl.script_entry, "[budget.pccl] script_entry", error)) {
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
    // Every resource the machine has to supply. A zero here is not a value the
    // disc chose, it is a field nobody wrote.
    const budget_field budgets[] = {
        { "[budget.pcca] voice_count", manifest.budget.pcca.voice_count },
        { "[budget.pcca] sound_memory_size", manifest.budget.pcca.sound_memory_size },
        { "[budget.pccv] screen_width", manifest.budget.pccv.screen_width },
        { "[budget.pccv] screen_height", manifest.budget.pccv.screen_height },
        { "[budget.pccv] texture_max_width", manifest.budget.pccv.texture_max_width },
        { "[budget.pccv] texture_max_height", manifest.budget.pccv.texture_max_height },
        { "[budget.pccv] video_memory_size", manifest.budget.pccv.video_memory_size },
        { "[budget.pccv] frame_capacity", manifest.budget.pccv.frame_capacity },
        { "[budget.pccv] ot_bucket_count", manifest.budget.pccv.ot_bucket_count },
        { "[budget.pccio] iport_count", manifest.budget.pccio.iport_count },
        { "[budget.pccm] card_slots", manifest.budget.pccm.card_slots },
        { "[budget.pccm] card_slot_size", manifest.budget.pccm.card_slot_size },
    };

    // Every field here now arrives filled: absent means the reference machine,
    // not zero. So a zero or a negative can only be something the author typed,
    // and that is the only thing left to refuse.
    for (const budget_field &b : budgets) {
        if (b.value <= 0) {
            error = std::format("{} must be positive, got {}", b.name, b.value);
            return false;
        }
    }

    // pccl is deliberately NOT in the table above. Every other subsystem is
    // hardware the machine always has; the Lua machine exists only for a disc
    // that carries scripts, so an absent section is a legal statement rather
    // than a missing one. Only a negative is nonsense — nobody asks for less
    // than no memory.
    if (manifest.budget.pccl.script_memory_size < 0) {
        error = std::format("[budget.pccl] script_memory_size must not be negative, got {}",
            manifest.budget.pccl.script_memory_size);
        return false;
    }

    // A lua disc is declared by THREE statements that mean nothing apart:
    // where the scripts come from, how much memory they run in, and which one
    // starts. All three, or none of them — a disc is a lua disc or a C++ disc,
    // and there is no state between the two. Any partial declaration is a
    // manifest that describes a machine nobody can build.
    const bool has_scripts = !manifest.scripts_sources.empty();
    const bool has_memory = manifest.budget.pccl.script_memory_size > 0;
    const bool has_entry = !manifest.budget.pccl.script_entry.empty();

    if (has_scripts != has_memory || has_memory != has_entry) {
        std::string stated;
        std::string missing;
        const auto note = [&](bool present, const char *what) {
            std::string &side = present ? stated : missing;
            if (!side.empty()) {
                side += ", ";
            }
            side += what;
        };
        note(has_scripts, "[scripts] sources");
        note(has_memory, "[budget.pccl] script_memory_size");
        note(has_entry, "[budget.pccl] script_entry");

        error = std::format(
            "a lua disc states all three of [scripts] sources, [budget.pccl] script_memory_size "
            "and [budget.pccl] script_entry; this manifest states {} and leaves out {}. "
            "State the rest, or drop them all and burn a C++ disc",
            stated, missing);
        return false;
    }

    return true;
}

} // namespace rv_pdklib
