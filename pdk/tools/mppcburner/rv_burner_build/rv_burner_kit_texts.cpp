// The web kit's page texts file: the keys every text must have, and the reader that checks them.
#include "rv_burner_kit.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "pdk/rv_err.h"

namespace fs = std::filesystem;

namespace
{

// Every text of the page texts file is required, as every key of kit.txt is.
constexpr std::array kWebTextKeys{ rv_pdktools::kWebTextLoading,
    rv_pdktools::kWebTextAllowGamepads,
    rv_pdktools::kWebTextClickToStart,
    rv_pdktools::kWebTextPadA,
    rv_pdktools::kWebTextPadB,
    rv_pdktools::kWebTextPadX,
    rv_pdktools::kWebTextPadY,
    rv_pdktools::kWebTextPadL1,
    rv_pdktools::kWebTextPadR1,
    rv_pdktools::kWebTextPadL2,
    rv_pdktools::kWebTextPadR2,
    rv_pdktools::kWebTextPadL4,
    rv_pdktools::kWebTextPadL5,
    rv_pdktools::kWebTextPadR4,
    rv_pdktools::kWebTextPadR5,
    rv_pdktools::kWebTextPadView,
    rv_pdktools::kWebTextPadMenu,
    rv_pdktools::kWebTextPadLs,
    rv_pdktools::kWebTextPadRs,
    rv_pdktools::kWebTextPadLp,
    rv_pdktools::kWebTextPadRp,
    rv_pdktools::kWebTextPadDn,
    rv_pdktools::kWebTextPadDs,
    rv_pdktools::kWebTextPadDw,
    rv_pdktools::kWebTextPadDe };

} // namespace

int rv_pdktools::read_page_texts(const fs::path &file,
    std::map<std::string, std::string, std::less<>> &texts,
    std::string &error)
{
    std::string content;
    if (const int rv = read_text_file(file, content, error); rv != RV_OK) {
        return rv;
    }
    std::map<std::string, std::string, std::less<>> entries;
    if (const int rv = parse_entries(content, file.string(), entries, error); rv != RV_OK) {
        return rv;
    }
    for (const auto &entry : entries) {
        if (std::find(kWebTextKeys.begin(), kWebTextKeys.end(), entry.first) == kWebTextKeys.end()) {
            error = file.string() + ": unknown key '" + entry.first + "'";
            return RV_ERR_INVAL;
        }
    }
    for (const std::string_view key : kWebTextKeys) {
        if (entries.find(key) == entries.end()) {
            error = file.string() + ": missing key '" + std::string(key) + "'";
            return RV_ERR_INVAL;
        }
    }
    texts = std::move(entries);
    return RV_OK;
}
