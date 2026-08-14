#include "rv_manifest.hpp"

#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "rv_burner_manifest_parser.hpp"
#include "rv_burner_schema.hpp"
#include "rv_burner_text.hpp"

namespace rv_pdktools
{
namespace
{

// --- rendering ----------------------------------------------------------------

// The inverse of parse_string(). Only the five escapes the parser knows are
// ever emitted, so anything this function produces is something it can read
// back — that is the whole round-trip contract in one sentence.
std::string quote(const std::string &s)
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

void render_array(std::ostringstream &out, const std::string &key,
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

} // namespace

bool rv_manifest_parse(const std::string &text, rv_manifest &out, std::string &error)
{
	error.clear();
	rv_burner_manifest_parser parser(text, out);
	return parser.run(error);
}

bool rv_manifest_load(const std::string &path, rv_manifest &out, std::string &error)
{
	error.clear();
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		error = "cannot open manifest '" + path + "'";
		return false;
	}
	std::ostringstream buffer;
	buffer << in.rdbuf();
	if (in.bad()) {
		error = "cannot read manifest '" + path + "'";
		return false;
	}
	std::string message;
	if (!rv_manifest_parse(buffer.str(), out, message)) {
		// The line number is only useful next to the file it belongs to.
		error = path + ": " + message;
		return false;
	}
	return true;
}

std::string rv_manifest_render(const rv_manifest &manifest)
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

	out << "\n[assets]\n";
	render_array(out, "files", manifest.assets_files);

	out << "\n[textures]\n";
	render_array(out, "files", manifest.textures_files.files);
	out << "format = " << quote(manifest.textures_files.format) << "\n";

	out << "\n[budget]\n";
	out << "texture_max_width = " << manifest.budget.texture_max_width << "\n";
	out << "texture_max_height = " << manifest.budget.texture_max_height << "\n";
	out << "video_memory_size = " << manifest.budget.video_memory_size << "\n";

	return out.str();
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

	bool format_known = false;
	for (std::string_view f : rv_burner_approved_texture_formats) {
		if (f == manifest.textures_files.format) {
			format_known = true;
		}
	}
	if (!format_known) {
		std::string known;
		for (std::size_t i = 0; i < std::size(rv_burner_approved_texture_formats); ++i) {
			if (i != 0) {
				known += ", ";
			}
			known += std::string(rv_burner_approved_texture_formats[i]);
		}
		error = "[textures] format '" + manifest.textures_files.format +
			"' is unknown — expected one of " + known;
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
	for (const budget_field &b : budgets) {
		if (b.value <= 0) {
			error = std::string("[budget] ") + b.name + " must be positive, got " +
				std::to_string(b.value);
			return false;
		}
	}

	return true;
}

} // namespace rv_pdktools
