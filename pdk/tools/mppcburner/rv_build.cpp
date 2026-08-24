#include "rv_build.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

#include "pdklib/rv_stdio.hpp"
#include "pdklib/rv_texfmt_name.hpp"

#include "rv_burner_compile/rv_burner_check.hpp"
#include "rv_burner_compile/rv_burner_cmake.hpp"
#include "rv_burner_compile/rv_burner_common_helpers.hpp"
#include "rv_burner_compile/rv_burner_compile_scripts.hpp"
#include "rv_burner_compile/rv_burner_compile_sources.hpp"
#include "rv_burner_globs.hpp"
#include "rv_burner_print.hpp"
#include "rv_zipwrite.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{
namespace
{

// ── .mppctex inspection, for the budget gate ──────────────────────────────────

// The 16-byte header mppcbaker writes (pdk/tools/mppcbaker/README.md). Read back
// after baking so the budget is checked against the TEXELS that will actually
// be uploaded, not against a guess made from the PNG.
struct mppctex_header {
	int format = 0;
	int64_t width = 0;
	int64_t height = 0;
	int64_t palette_count = 0;
};

uint16_t read_u16(const unsigned char *p)
{
	return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
		static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8));
}

bool read_mppctex_header(const fs::path &path, mppctex_header &out, std::string &error)
{
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		error = "cannot reopen baked texture '" + path.string() + "'";
		return false;
	}
	unsigned char raw[16] = {};
	file.read(reinterpret_cast<char *>(raw), sizeof(raw));
	if (file.gcount() != static_cast<std::streamsize>(sizeof(raw))) {
		error = "baked texture '" + path.string() + "' is shorter than its own header";
		return false;
	}
	if (std::memcmp(raw, "MPTX", 4) != 0) {
		error = "baked texture '" + path.string() + "' has no MPTX magic";
		return false;
	}
	if (read_u16(raw + 4) != 1) {
		error = "baked texture '" + path.string() + "' is version " +
			std::to_string(read_u16(raw + 4)) + ", this burner writes version 1";
		return false;
	}
	out.format = read_u16(raw + 6);
	out.width = read_u16(raw + 8);
	out.height = read_u16(raw + 10);
	out.palette_count = read_u16(raw + 12);
	if (out.format != rv_pdk::RV_TEXFMT_IDX4 && out.format != rv_pdk::RV_TEXFMT_IDX8 &&
		out.format != rv_pdk::RV_TEXFMT_DIRECT15) {
		error = "baked texture '" + path.string() + "' claims unknown format " +
			std::to_string(out.format);
		return false;
	}
	return true;
}

// Bytes of texels, by the strides mppcbaker documents.
int64_t texel_bytes(const mppctex_header &header)
{
	switch (header.format) {
	case rv_pdk::RV_TEXFMT_IDX4:
		return ((header.width + 1) / 2) * header.height;
	case rv_pdk::RV_TEXFMT_IDX8:
		return header.width * header.height;
	default:
		return header.width * header.height * 2;
	}
}

// ── finding mppcbaker ─────────────────────────────────────────────────────────

bool is_executable(const fs::path &path)
{
	std::error_code ec;
	return fs::is_regular_file(path, ec) && ::access(path.c_str(), X_OK) == 0;
}

fs::path executable_directory()
{
	std::array<char, 4096> buffer{};
	const ssize_t got = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
	if (got <= 0) {
		return fs::path();
	}
	buffer[static_cast<std::size_t>(got)] = '\0';
	return fs::path(buffer.data()).parent_path();
}

// --baker wins; then next to ourselves (the usual case: both tools sit in the
// same build tree); then $PATH.
bool find_baker(const std::string &hint, std::string &out, std::string &error)
{
	if (!hint.empty()) {
		if (!is_executable(hint)) {
			error = "--baker '" + hint + "' is not an executable file";
			return false;
		}
		out = hint;
		return true;
	}

	const fs::path self = executable_directory();
	if (!self.empty()) {
		const fs::path candidates[] = { self / "mppcbaker", self / "mppcbaker" / "mppcbaker",
			self.parent_path() / "mppcbaker" / "mppcbaker" };
		for (const fs::path &candidate : candidates) {
			if (is_executable(candidate)) {
				out = candidate.string();
				return true;
			}
		}
	}

	const char *path_env = std::getenv("PATH");
	if (path_env != nullptr) {
		std::string_view rest(path_env);
		while (!rest.empty()) {
			const std::size_t colon = rest.find(':');
			const std::string_view head = rest.substr(0, colon);
			if (!head.empty()) {
				const fs::path candidate = fs::path(std::string(head)) / "mppcbaker";
				if (is_executable(candidate)) {
					out = candidate.string();
					return true;
				}
			}
			if (colon == std::string_view::npos) {
				break;
			}
			rest.remove_prefix(colon + 1);
		}
	}

	error = "mppcbaker not found next to mppcburner or in $PATH; pass --baker PATH";
	return false;
}

} // namespace

// ── flat names ────────────────────────────────────────────────────────────────
//
// THEOREM: the asset namespace of a .mppcdisc is FLAT, and two originals that
// collapse onto one flat name are a REFUSAL rather than a last-writer-wins.
//
// The premise is the console's contract, not a convenience: rv_cd resolves a
// resource by name and forbids path separators in that name outright, so
// `assets/protagonist.obj` can only ever enter the archive as `protagonist.obj`.
// The mapping from a disc directory to an archive is therefore not injective —
// `sprites/tex.png` and `models/tex.png` have the same image.
//
// Given a non-injective mapping there are exactly three things a burner can do
// with a collision:
//   1. write both entries: the archive now has two records under one name, and
//      which one the loader hands the game depends on its lookup order. The disc
//      is nondeterministic with respect to a detail nobody documented.
//   2. write the last one and drop the rest: the disc is deterministic and
//      WRONG. The game asks for `tex.png`, gets the other artist's file, and the
//      symptom is a texture that looks like a different asset — a bug whose
//      cause is invisible in both the source tree and the running game, because
//      nothing anywhere ever said the two files were related.
//   3. refuse, naming both originals.
// Only (3) leaves the developer with the information needed to fix it, and the
// fix is one rename. The console CANNOT diagnose this — by the time the archive
// exists, the second file is simply gone — so the check has to happen here or
// nowhere. Hence: refusal.

std::string rv_flat_name(const std::string &relative_path)
{
	return fs::path(relative_path).filename().string();
}

bool rv_check_asset_name(const std::string &name, std::string &error)
{
	if (name.empty()) {
		error = "an asset resolved to an empty name";
		return false;
	}
	// Belt and braces: rv_flat_name already strips directories, but a name
	// arriving from anywhere else must still meet rv_cd's contract.
	if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
		error = "asset name '" + name + "' contains a path separator; rv_cd forbids them";
		return false;
	}
	if (name[0] == '.') {
		error = "asset name '" + name +
			"' starts with a dot; dotfiles are editor and VCS bookkeeping, not disc content";
		return false;
	}
	if (name == "disc.toml" || name == "disc.so") {
		error = "asset name '" + name +
			"' collides with a service entry; the loader reads that name before it looks at "
			"the asset namespace";
		return false;
	}
	return true;
}

bool rv_check_collisions(const std::vector<rv_archive_item> &items, std::string &error)
{
	std::vector<rv_archive_item> sorted = items;
	std::sort(sorted.begin(), sorted.end(),
		[](const rv_archive_item &a, const rv_archive_item &b) {
			return a.name < b.name;
		});
	for (std::size_t i = 1; i < sorted.size(); ++i) {
		if (sorted[i].name == sorted[i - 1].name) {
			error = "two files collapse onto the archive name '" + sorted[i].name + "': '" +
				sorted[i - 1].source + "' and '" + sorted[i].source +
				"'. The disc namespace is flat — rename one of them.";
			return false;
		}
	}
	return true;
}

// ── the generated project ─────────────────────────────────────────────────────
//
// PATTERN: GENERATE A BUILD SYSTEM, DO NOT BE ONE.
//
// The burner has to turn a list of .cpp files into one loadable .so. The
// tempting shortcut is to shell out to `g++ -shared` directly — it is twenty
// lines and it works on the first disc. What it does not have is dependency
// tracking, parallelism, incremental rebuilds, header scanning, compiler
// detection, response files for long command lines, or any of the per-platform
// knowledge about how a shared module is linked. Every one of those arrives
// later as a bug report, and answering them turns this file into a bad build
// system maintained by one project.
//
// So the burner writes a CMakeLists.txt and drives cmake + ninja instead. cmake
// is a RUN-TIME dependency of the tool (see pdk/tools/mppcburner/CMakeLists.txt),
// which is a real cost, but it is a dependency every C++ developer already has
// installed — while a hand-rolled compiler driver is a dependency only this
// project would ever maintain. The generated project is also a readable
// artifact: when a disc will not build, `--keep-build` leaves a directory the
// developer can enter and run `ninja -v` in, which is a far better debugging
// story than reverse-engineering a command line out of our source.
//
// The generated project is what makes the console/disc boundary CHECKABLE. It
// adds the PDK and the SDK to the include path and nothing else — no src/, no
// console headers, not even by accident — so a disc that reaches for the
// implementation fails to compile instead of quietly linking against a private
// header and breaking on the next refactor. A boundary the build enforces is a
// boundary; a boundary in a README is a suggestion.

std::string rv_human_size(int64_t bytes)
{
	char buffer[64];
	const double value = static_cast<double>(bytes);
	if (bytes < 1024) {
		std::snprintf(buffer, sizeof(buffer), "%lld B", static_cast<long long>(bytes));
	} else if (bytes < 1024 * 1024) {
		std::snprintf(buffer, sizeof(buffer), "%.1f KB", value / 1024.0);
	} else {
		std::snprintf(buffer, sizeof(buffer), "%.1f MB", value / (1024.0 * 1024.0));
	}
	return std::string(buffer);
}

// --- build --------------------------------------------------------

int rv_burn_run(const rv_burner_options &options)
{
	std::error_code ec;
	const fs::path disc_dir = fs::weakly_canonical(fs::path(options.operand), ec);
	if (ec || !fs::is_directory(disc_dir, ec)) {
		rv_burner_print_error("'" + options.operand + "' is not a directory");
		return 1;
	}

	const fs::path output_path = fs::absolute(fs::path(options.output), ec);
	if (ec) {
		rv_burner_print_error("cannot resolve output path '" + options.output + "'");
		return 1;
	}

	// --- [1/4] manifest -------------------------------------------

	const fs::path manifest_path = disc_dir / "disc.toml";
	std::expected<rv_burner_manifest, std::string> loaded =
		rv_manifest_load(manifest_path.string());
	if (!loaded) {
		rv_burner_print_error(loaded.error());
		return 1;
	}
	rv_burner_manifest manifest = std::move(*loaded);
	std::string error;
	if (!rv_manifest_validate(manifest, error)) {
		rv_burner_print_error(manifest_path.string() + ": " + error);
		return 1;
	}

	// --- prepare disc structure -----------------------------------

	std::vector<std::string> sources;
	if (!rv_glob_expand(disc_dir, manifest.build_sources, sources, error)) {
		rv_burner_print_error("[build] sources: " + error);
		return 1;
	}

	if (sources.empty()) {
		rv_burner_print_error("[build] sources is empty: a disc with no code cannot export an entry point");
		return 1;
	}

	// --- check sources on outside disc directory ------------------

	std::string er;
	std::vector<std::string> absolute_includes;
	std::vector<std::string> absolute_sources;
	if (rv_pdktools::rv_burner_check_sources_outside_disc(
			manifest,
			disc_dir,
			sources,
			absolute_includes,
			absolute_sources,
			er) != 0) {
		rv_burner_print_error("[build] include_dirs: " + er);
		return 1;
	}

	// --- create directories --------------------------------------

	const fs::path project_dir =
		options.build_dir.empty() ? disc_dir / ".mppcburn" : fs::absolute(options.build_dir, ec);
	const fs::path binary_dir = project_dir / "build";
	const fs::path scripts_dir = project_dir / "scripts";
	const fs::path texture_dir = project_dir / "textures";
	fs::create_directories(binary_dir, ec);
	fs::create_directories(scripts_dir, ec);
	fs::create_directories(texture_dir, ec);
	if (ec) {
		rv_burner_print_error("cannot create build directory '" + project_dir.string() + "'");
		return 1;
	}

	// --- [2/4] compile --------------------------------------------

	// --- create CMakeLists ---------------------------------------

	if (rv_pdktools::rv_burner_create_cmakelists(
			options,
			manifest,
			project_dir,
			absolute_includes,
			absolute_sources,
			er) != 0) {
		rv_burner_print_error(er);
		return 1;
	}

	// --- prepare build configure cmake -------------------------------------------

	if (rv_pdktools::rv_burner_prepare_build_configure_cmake(binary_dir, project_dir, er) != 0) {
		rv_burner_print_error(er);
		return 1;
	}

	// --- build sources ----------------------------------------------------------

	if (rv_pdktools::rv_burner_compile_sources(options, binary_dir, sources.size(), er) != 0) {
		rv_pdktools::rv_burner_print_error(er);
		return 1;
	}

	// The phase above proved this file exists; the archive stage below carries it.
	const fs::path disc_so = binary_dir / "disc.so";

	// --- [3/4] assets ---------------------------------------------
	//
	// The whole archive is PLANNED first — every flat name, and the original it
	// came from — and only then is any work done. That ordering is what makes
	// the collision diagnostic useful: two PNGs colliding are two PNGs, not two
	// identical paths to a baked file that had already overwritten itself.
	std::vector<rv_archive_item> items;

	std::vector<std::string> asset_files;
	if (!manifest.assets_files.empty()) {
		if (!rv_glob_expand(disc_dir, manifest.assets_files, asset_files, error)) {
			rv_burner_print_error("[assets] files: " + error);
			return 1;
		}
	}
	for (const std::string &relative : asset_files) {
		rv_archive_item item;
		item.name = rv_flat_name(relative);
		item.source = relative;
		item.payload = (disc_dir / relative).string();
		if (!rv_check_asset_name(item.name, error)) {
			rv_burner_print_error("[assets] files: " + error + " (from '" + relative + "')");
			return 1;
		}
		items.push_back(item);
	}

	std::vector<std::string> texture_files;
	if (!manifest.textures_files.files.empty()) {
		if (!rv_glob_expand(disc_dir, manifest.textures_files.files, texture_files, error)) {
			rv_burner_print_error("[textures] files: " + error);
			return 1;
		}
	}
	const std::size_t first_texture = items.size();
	for (const std::string &relative : texture_files) {
		rv_archive_item item;
		item.name = fs::path(relative).stem().string() + ".mppctex";
		item.source = relative;
		item.payload = (texture_dir / item.name).string();
		if (!rv_check_asset_name(item.name, error)) {
			rv_burner_print_error("[textures] files: " + error + " (from '" + relative + "')");
			return 1;
		}
		items.push_back(item);
	}

	std::vector<std::string> script_files;
	if (!manifest.scripts_sources.empty()) {
		if (!rv_glob_expand(disc_dir, manifest.scripts_sources, script_files, error)) {
			rv_burner_print_error("[scripts] sources: " + error);
			return 1;
		}
	}
	const std::size_t first_script = items.size();
	for (const std::string &relative : script_files) {
		rv_archive_item item;
		item.name = fs::path(relative).stem().string() + ".luac";
		item.source = relative;
		item.payload = (scripts_dir / item.name).string();
		if (!rv_check_asset_name(item.name, error)) {
			rv_burner_print_error("[scripts] sources: " + error + " (from '" + relative + "')");
			return 1;
		}
		items.push_back(item);
	}

	if (!rv_check_collisions(items, error)) {
		rv_burner_print_error(error);
		return 1;
	}

	// Past the collision check, so no .luac can overwrite another's bytecode.
	if (rv_pdktools::rv_burner_compile_scripts(items, first_script, disc_dir, er) != 0) {
		rv_burner_print_error(er);
		return 1;
	}

	if (!texture_files.empty()) {
		std::string baker;
		if (!find_baker(options.baker, baker, error)) {
			rv_burner_print_error(error);
			return 1;
		}

		// One format for the whole manifest, so the spelling mppcbaker is given is
		// looked up once rather than per texture.
		const rv_pdklib::rv_texfmt_name *texfmt = rv_pdklib::rv_texfmt_name::by_format(
			manifest.textures_files.format);
		const std::string texfmt_name_str = texfmt != nullptr ? texfmt->text : "";

		std::string child_output;
		int status = 0;
		int64_t video_memory_used = 0;
		// Bounded by the texture range, not by items.size(): the scripts planned
		// after the textures are in the same vector and are not baker input.
		for (std::size_t i = first_texture; i < first_texture + texture_files.size(); ++i) {
			const rv_archive_item &item = items[i];
			const std::string command =
				rv_pdktools::shell_quote(baker) + " " + rv_pdktools::shell_quote((disc_dir / item.source).string()) + " " +
				rv_pdktools::shell_quote(item.payload) + " --format " + texfmt_name_str;
			status = run_capture(command, child_output);
			if (status != 0) {
				rv_pdktools::dump_child_output(child_output);
				rv_burner_print_error("mppcbaker failed on '" + item.source + "' (exit " +
					std::to_string(status) + ")");
				return 1;
			}

			mppctex_header header;
			if (!read_mppctex_header(item.payload, header, error)) {
				rv_burner_print_error(error);
				return 1;
			}
			// Budget: one texture that does not fit the reference machine's
			// limits. Caught here rather than at video_asset_write(), where it
			// would be an RV_ERR_INVAL on a loading screen.
			if (header.width > manifest.budget.texture_max_width ||
				header.height > manifest.budget.texture_max_height) {
				rv_burner_print_error("texture '" + item.source + "' is " + std::to_string(header.width) + "x" +
					std::to_string(header.height) + ", over the budget of " +
					std::to_string(manifest.budget.texture_max_width) + "x" +
					std::to_string(manifest.budget.texture_max_height) +
					" declared in [budget]");
				return 1;
			}
			video_memory_used += texel_bytes(header) + header.palette_count * 2;
		}

		// Budget: everything the disc would upload, against the video memory it
		// says the target machine has. This is an upper bound — a game that
		// frees a texture before loading the next one uses less — so it is the
		// conservative check, and a disc that trips it says so with a number.
		if (video_memory_used > manifest.budget.video_memory_size) {
			rv_burner_print_error("baked texels and palettes total " + std::to_string(video_memory_used) +
				" bytes, over the [budget] video_memory_size of " +
				std::to_string(manifest.budget.video_memory_size) + " bytes");
			return 1;
		}
	}

	rv_burner_print_step(3, "assets",
		std::to_string(texture_files.size()) + " png -> .mppctex, " +
			std::to_string(script_files.size()) + " lua -> .luac, " +
			std::to_string(asset_files.size()) + " copied");

	// --- [4/4] burn -----------------------------------------------

	fs::create_directories(output_path.parent_path(), ec);
	rv_zipwriter writer(output_path.string());
	if (!writer.ok()) {
		rv_burner_print_error("cannot open '" + output_path.string() + "' for writing");
		return 1;
	}

	// The manifest is re-rendered from the parsed structure rather than copied
	// byte for byte: what the archive carries is then exactly what the burner
	// understood, and a comment or a stray key that the parser ignored cannot
	// ride along and mislead a later reader.
	const std::string manifest_text = rv_manifest_render(manifest);
	if (!writer.add("disc.toml", manifest_text.data(), manifest_text.size(), error)) {
		rv_burner_print_error(error);
		return 1;
	}
	if (!writer.add_file("disc.so", disc_so.string(), error)) {
		rv_burner_print_error(error);
		return 1;
	}
	for (const rv_archive_item &item : items) {
		if (!writer.add_file(item.name, item.payload, error)) {
			rv_burner_print_error(error);
			return 1;
		}
	}
	if (!writer.finish(error)) {
		rv_burner_print_error(error);
		return 1;
	}

	const int64_t burned = static_cast<int64_t>(fs::file_size(output_path, ec));
	rv_burner_print_step(4, "burn",
		output_path.filename().string() + " (" + rv_human_size(ec ? 0 : burned) + ")");

	// The build tree is scratch space and goes away — unless the developer
	// asked to keep it, in which case it is a readable CMake project they can
	// run ninja in themselves. It is also kept after a FAILURE (an early return
	// above), for the same reason.
	if (!options.keep_build) {
		fs::remove_all(project_dir, ec);
		if (ec) {
			rv_burner_print_warning("could not remove the build directory '" + project_dir.string() + "'");
		}
	}
	return 0;
}

} // namespace rv_pdktools
