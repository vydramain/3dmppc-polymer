#include "rv_burner_kit.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <string_view>
#include <system_error>

#include "pdk/de/rv_dv.h"
#include "pdk/rv_err.h"
#include "rv_burner_common/rv_burner_process.hpp"

namespace fs = std::filesystem;

namespace
{

// Where the development build installs the kit, relative to the burner's own directory.
constexpr std::string_view kKitSubdir = "firmware/web";
constexpr std::string_view kKitFileName = "kit.txt";
constexpr std::string_view kNoKitHint = "a development build (-D3DMPPC_DEVTOOLS=ON) makes it";

// Every key is required: a kit missing one would be linked with a guess.
constexpr std::string_view kKeyPdkVersion = "pdk_version";
constexpr std::string_view kKeyBuildType = "build_type";
constexpr std::string_view kKeyLibs = "libs";
constexpr std::string_view kKeyLinkGroup = "link_group";
constexpr std::string_view kKeyLinkFlags = "link_flags";
constexpr std::string_view kKeyPreJs = "pre_js";

constexpr std::string_view kLinkGroupYes = "yes";
constexpr std::string_view kLinkGroupNo = "no";
constexpr std::string_view kBlanks = " \t\r"; // \r: a kit written on Windows still reads
constexpr char kKeySeparator = '=';
constexpr char kVersionSeparator = '.';
constexpr char kCommentStart = '#';

std::string_view trim(std::string_view text)
{
    const std::size_t first = text.find_first_not_of(kBlanks);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(kBlanks);
    return text.substr(first, last - first + 1);
}

std::vector<std::string> split_words(std::string_view text)
{
    std::vector<std::string> words;
    std::size_t pos = text.find_first_not_of(kBlanks);
    while (pos != std::string_view::npos) {
        const std::size_t end = text.find_first_of(kBlanks, pos);
        words.emplace_back(text.substr(pos, end - pos));
        pos = text.find_first_not_of(kBlanks, end);
    }
    return words;
}

bool parse_number(std::string_view text, int &out)
{
    const char *last = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), last, out);
    return !text.empty() && ec == std::errc{} && ptr == last;
}

// A kit file must exist as a file: a directory named in kit.txt would pass a bare existence test.
int require_kit_file(const fs::path &path, std::string_view key, std::string &error)
{
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        return RV_OK;
    }
    error = "kit.txt: '" + std::string(key) + "' names '" + path.string() + "', which is not a file";
    return RV_ERR_NOENT;
}

// Each apply step turns one value of kit.txt into its field of the kit.
int apply_pdk_version(rv_pdktools::rv_burner_kit &kit, const std::string &value, std::string &error)
{
    const std::string_view text = value;
    const std::size_t dot = text.find(kVersionSeparator);
    const bool parsed = dot != std::string_view::npos && parse_number(text.substr(0, dot), kit.pdk_major) &&
        parse_number(text.substr(dot + 1), kit.pdk_minor);
    if (!parsed) {
        error = "kit.txt: 'pdk_version' must be major.minor, got '" + value + "'";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

int apply_build_type(rv_pdktools::rv_burner_kit &kit, const std::string &value, std::string &error)
{
    if (value.empty()) {
        error = "kit.txt: 'build_type' is empty";
        return RV_ERR_INVAL;
    }
    kit.build_type = value;
    return RV_OK;
}

// A path in kit.txt must stay inside the kit: an absolute path or a ".." would reach outside it.
int check_kit_relative(const std::string &word, std::string_view key, std::string &error)
{
    const fs::path rel(word);
    if (rel.is_absolute()) {
        error = "kit.txt: '" + std::string(key) + "' names '" + word + "', which is not relative to the kit";
        return RV_ERR_INVAL;
    }
    for (const fs::path &part : rel) {
        if (part == "..") {
            error = "kit.txt: '" + std::string(key) + "' names '" + word + "', which leaves the kit directory";
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

int apply_libs(rv_pdktools::rv_burner_kit &kit, const std::string &value, std::string &error)
{
    const std::vector<std::string> words = split_words(value);
    if (words.empty()) {
        error = "kit.txt: 'libs' is empty";
        return RV_ERR_INVAL;
    }
    for (const std::string &word : words) {
        if (const int rv = check_kit_relative(word, kKeyLibs, error); rv != RV_OK) {
            return rv;
        }
        const fs::path lib = kit.dir / word;
        if (const int rv = require_kit_file(lib, kKeyLibs, error); rv != RV_OK) {
            return rv;
        }
        kit.libs.push_back(lib);
    }
    return RV_OK;
}

int apply_link_group(rv_pdktools::rv_burner_kit &kit, const std::string &value, std::string &error)
{
    if (value != kLinkGroupYes && value != kLinkGroupNo) {
        error = "kit.txt: 'link_group' must be yes or no, got '" + value + "'";
        return RV_ERR_INVAL;
    }
    kit.link_group = value == kLinkGroupYes;
    return RV_OK;
}

int apply_link_flags(rv_pdktools::rv_burner_kit &kit, const std::string &value, std::string &)
{
    kit.link_flags = split_words(value);
    return RV_OK;
}

int apply_pre_js(rv_pdktools::rv_burner_kit &kit, const std::string &value, std::string &error)
{
    if (const int rv = check_kit_relative(value, kKeyPreJs, error); rv != RV_OK) {
        return rv;
    }
    kit.pre_js = kit.dir / value;
    return require_kit_file(kit.pre_js, kKeyPreJs, error);
}

// Key -> field table. Adding a key is one row here and one field in the struct.
struct kit_field {
    std::string_view key;
    int (*apply)(rv_pdktools::rv_burner_kit &kit, const std::string &value, std::string &error);
};

constexpr std::array kKitFields{
    kit_field{ kKeyPdkVersion, apply_pdk_version },
    kit_field{ kKeyBuildType, apply_build_type },
    kit_field{ kKeyLibs, apply_libs },
    kit_field{ kKeyLinkGroup, apply_link_group },
    kit_field{ kKeyLinkFlags, apply_link_flags },
    kit_field{ kKeyPreJs, apply_pre_js },
};

const kit_field *find_field(std::string_view key)
{
    const auto it = std::find_if(kKitFields.begin(), kKitFields.end(), [key](const kit_field &field) {
        return field.key == key;
    });
    return it == kKitFields.end() ? nullptr : &*it;
}

// Splits "key = value" lines into @p entries; blank lines and '#' comments are skipped.
// A key given twice is refused, since the later line would silently win.
int parse_entries(std::string_view text, std::map<std::string, std::string, std::less<>> &entries, std::string &error)
{
    int line_no = 0;
    while (!text.empty()) {
        const std::size_t eol = text.find('\n');
        const std::string_view line = trim(text.substr(0, eol));
        text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
        ++line_no;
        if (line.empty() || line.front() == kCommentStart) {
            continue;
        }
        const std::size_t sep = line.find(kKeySeparator);
        if (sep == std::string_view::npos) {
            error = "kit.txt line " + std::to_string(line_no) + ": expected 'key = value', got '" + std::string(line) + "'";
            return RV_ERR_INVAL;
        }
        const std::string key(trim(line.substr(0, sep)));
        if (!entries.emplace(key, std::string(trim(line.substr(sep + 1)))).second) {
            error = "kit.txt line " + std::to_string(line_no) + ": key '" + key + "' is given twice";
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

} // namespace

int rv_pdktools::rv_burner_kit_locate(fs::path &dir, std::string &error)
{
    const fs::path exe_dir = rv_pdktools::executable_directory();
    if (exe_dir.empty()) {
        error = "no web kit: the burner's own directory is unknown; " + std::string(kNoKitHint);
        return RV_ERR_NOENT;
    }

    const fs::path kit_dir = exe_dir / kKitSubdir;
    std::error_code ec;
    if (!fs::exists(kit_dir / kKitFileName, ec)) {
        error = "no web kit at '" + kit_dir.string() + "'";
        if (ec) {
            error += ": " + ec.message();
        }
        error += "; " + std::string(kNoKitHint);
        return RV_ERR_NOENT;
    }

    dir = kit_dir;
    return RV_OK;
}

int rv_pdktools::rv_burner_kit_read(const fs::path &dir, rv_burner_kit &out, std::string &error)
{
    std::ifstream in(dir / kKitFileName);
    if (!in) {
        error = "cannot open '" + (dir / kKitFileName).string() + "'";
        return RV_ERR_IO;
    }
    const std::string text{ std::istreambuf_iterator<char>{ in }, std::istreambuf_iterator<char>{} };

    std::map<std::string, std::string, std::less<>> entries;
    if (const int rv = parse_entries(text, entries, error); rv != RV_OK) {
        return rv;
    }

    out = rv_burner_kit{};
    out.dir = dir;

    // Version first: a kit of another PDK may have other keys, and must be named as a version
    // mismatch, not as an unknown key.
    const auto pdk_version = entries.find(kKeyPdkVersion);
    if (pdk_version == entries.end()) {
        error = "kit.txt: missing key '" + std::string(kKeyPdkVersion) + "'";
        return RV_ERR_INVAL;
    }
    if (const int rv = apply_pdk_version(out, pdk_version->second, error); rv != RV_OK) {
        return rv;
    }
    if (const int rv = rv_burner_kit_check_version(out, error); rv != RV_OK) {
        return rv;
    }

    // Refused, not skipped: an unknown key means kit.txt came from a writer this burner
    // was not built against. Skipping a key such as link_group would link a disc that
    // differs from the console without a word.
    for (const auto &entry : entries) {
        if (find_field(entry.first) == nullptr) {
            error = "kit.txt: unknown key '" + entry.first + "'";
            return RV_ERR_INVAL;
        }
    }

    for (const kit_field &field : kKitFields) {
        const auto it = entries.find(field.key);
        if (it == entries.end()) {
            error = "kit.txt: missing key '" + std::string(field.key) + "'";
            return RV_ERR_INVAL;
        }
        if (const int rv = field.apply(out, it->second, error); rv != RV_OK) {
            return rv;
        }
    }
    return RV_OK;
}

int rv_pdktools::rv_burner_kit_check_version(const rv_burner_kit &kit, std::string &error)
{
    if (kit.pdk_major == RV_MPPC_VER_MAJOR && kit.pdk_minor == RV_MPPC_VER_MINOR) {
        return RV_OK;
    }
    error = "kit is PDK " + std::to_string(kit.pdk_major) + "." + std::to_string(kit.pdk_minor) + ", this burner is " +
        std::to_string(RV_MPPC_VER_MAJOR) + "." + std::to_string(RV_MPPC_VER_MINOR);
    return RV_ERR_INVAL;
}
