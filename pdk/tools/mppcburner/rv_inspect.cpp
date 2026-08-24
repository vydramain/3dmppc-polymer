#include "rv_inspect.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "pdklib/rv_stdio.hpp"

#include "rv_burner_print.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{
namespace
{

// ── a minimal zip reader, for `inspect` only ──────────────────────────────────
//
// Deliberately small: the central directory gives names and sizes, and that is
// the whole of what `inspect` prints. The real reader — bounds-checked, CRC-
// verifying, hostile-input-hardened — lives on the console side in
// src/rv_pconsole/cd/rv_pczip.*, because that is where an archive downloaded
// from the internet gets opened. Here the input is a file the developer just
// produced on their own machine, so the job is reporting, not defence.

struct zip_entry {
	std::string name;
	int64_t size = 0;
	int64_t local_offset = 0;
	int method = 0;
};

uint32_t read_u32(const std::vector<unsigned char> &bytes, std::size_t offset)
{
	return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
		(static_cast<uint32_t>(bytes[offset + 2]) << 16) |
		(static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

uint16_t read_u16(const std::vector<unsigned char> &bytes, std::size_t offset)
{
	return static_cast<uint16_t>(
		static_cast<uint16_t>(bytes[offset]) |
		static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8));
}

bool read_whole_file(const fs::path &path, std::vector<unsigned char> &out, std::string &error)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		error = "cannot open '" + path.string() + "'";
		return false;
	}
	const std::streamoff size = file.tellg();
	if (size < 0) {
		error = "cannot size '" + path.string() + "'";
		return false;
	}
	out.resize(static_cast<std::size_t>(size));
	file.seekg(0);
	if (!out.empty()) {
		file.read(reinterpret_cast<char *>(out.data()), size);
		if (!file) {
			error = "short read on '" + path.string() + "'";
			return false;
		}
	}
	return true;
}

bool zip_list(const std::vector<unsigned char> &bytes, std::vector<zip_entry> &out,
	std::string &error)
{
	constexpr uint32_t kEocdSig = 0x06054b50;
	constexpr uint32_t kCentralSig = 0x02014b50;
	if (bytes.size() < 22) {
		error = "file is too small to be a zip archive";
		return false;
	}
	// The end-of-central-directory record sits last, possibly behind a comment
	// of up to 64 KiB, so it is found by scanning backwards for its signature.
	std::size_t eocd = 0;
	bool found = false;
	const std::size_t limit = std::min<std::size_t>(bytes.size(), 22 + 65535);
	for (std::size_t back = 22; back <= limit; ++back) {
		const std::size_t at = bytes.size() - back;
		if (read_u32(bytes, at) == kEocdSig) {
			eocd = at;
			found = true;
			break;
		}
	}
	if (!found) {
		error = "no end-of-central-directory record: not a zip archive";
		return false;
	}

	const uint16_t count = read_u16(bytes, eocd + 10);
	const uint32_t directory_size = read_u32(bytes, eocd + 12);
	const uint32_t directory_offset = read_u32(bytes, eocd + 16);
	if (static_cast<std::size_t>(directory_offset) + directory_size > bytes.size()) {
		error = "central directory runs past the end of the file";
		return false;
	}

	std::size_t at = directory_offset;
	for (uint16_t i = 0; i < count; ++i) {
		if (at + 46 > bytes.size() || read_u32(bytes, at) != kCentralSig) {
			error = "central directory entry " + std::to_string(i) + " is malformed";
			return false;
		}
		zip_entry entry;
		entry.method = read_u16(bytes, at + 10);
		entry.size = read_u32(bytes, at + 24);
		const uint16_t name_length = read_u16(bytes, at + 28);
		const uint16_t extra_length = read_u16(bytes, at + 30);
		const uint16_t comment_length = read_u16(bytes, at + 32);
		entry.local_offset = read_u32(bytes, at + 42);
		if (at + 46 + name_length > bytes.size()) {
			error = "central directory entry " + std::to_string(i) + " has a runaway name";
			return false;
		}
		entry.name.assign(reinterpret_cast<const char *>(bytes.data()) + at + 46, name_length);
		out.push_back(entry);
		at += 46u + name_length + extra_length + comment_length;
	}
	return true;
}

bool zip_entry_bytes(const std::vector<unsigned char> &bytes, const zip_entry &entry,
	std::string &out, std::string &error)
{
	constexpr uint32_t kLocalSig = 0x04034b50;
	const std::size_t at = static_cast<std::size_t>(entry.local_offset);
	if (at + 30 > bytes.size() || read_u32(bytes, at) != kLocalSig) {
		error = "entry '" + entry.name + "' has no local header where the directory says";
		return false;
	}
	if (entry.method != 0) {
		error = "entry '" + entry.name + "' is compressed; .mppcdisc is store-only";
		return false;
	}
	const std::size_t data = at + 30 + read_u16(bytes, at + 26) + read_u16(bytes, at + 28);
	if (data + static_cast<std::size_t>(entry.size) > bytes.size()) {
		error = "entry '" + entry.name + "' runs past the end of the file";
		return false;
	}
	out.assign(reinterpret_cast<const char *>(bytes.data()) + data,
		static_cast<std::size_t>(entry.size));
	return true;
}

} // namespace

// ── inspect ───────────────────────────────────────────────────────────────────

int rv_inspect_run(const rv_burner_options &options)
{
	std::vector<unsigned char> bytes;
	std::string error;
	if (!read_whole_file(options.operand, bytes, error)) {
		rv_burner_print_error(error);
		return 1;
	}
	std::vector<zip_entry> entries;
	if (!zip_list(bytes, entries, error)) {
		rv_burner_print_error(options.operand + ": " + error);
		return 1;
	}

	rv_pdklib::rv_fprintf(stdout, "[manifest]\n");
	bool manifest_found = false;
	for (const zip_entry &entry : entries) {
		if (entry.name != "disc.toml") {
			continue;
		}
		std::string text;
		if (!zip_entry_bytes(bytes, entry, text, error)) {
			rv_burner_print_error(error);
			return 1;
		}
		std::fwrite(text.data(), 1, text.size(), stdout);
		if (!text.empty() && text.back() != '\n') {
			std::fputc('\n', stdout);
		}
		manifest_found = true;
		break;
	}
	if (!manifest_found) {
		rv_burner_print_warning(options.operand + " has no disc.toml; the console would refuse it");
	}

	rv_pdklib::rv_fprintf(stdout, "[entries]\n");
	int64_t total = 0;
	for (const zip_entry &entry : entries) {
		rv_pdklib::rv_fprintf(stdout, "%s\t%lld\n", entry.name.c_str(),
			static_cast<long long>(entry.size));
		total += entry.size;
	}

	rv_pdklib::rv_fprintf(stdout, "[total]\n\n");
	rv_pdklib::rv_fprintf(stdout, "entries\t%zu\n", entries.size());
	rv_pdklib::rv_fprintf(stdout, "bytes\t%lld\n", static_cast<long long>(total));
	rv_pdklib::rv_fprintf(stdout, "file\t%lld\n", static_cast<long long>(bytes.size()));
	return 0;
}

} // namespace rv_pdktools
