#include "rv_burner_map.hpp"

#include <fstream>
#include <system_error>

#include "pdklib/rv_textures/rv_texfmt_name.hpp"
#include "pdklib/rv_version/rv_version.hpp"

int rv_pdktools::write_map(const std::filesystem::path &path,
    const rv_pdklib::rv_manifest &manifest,
    const std::vector<std::string> &sources,
    const archive_plan &plan,
    std::string &error)
{
    std::string text = std::string("mppcburner-map ") + rv_pdklib::rv_version_str + "\n";
    for (const std::string &source : sources) {
        text += source + "\tcode\tdisc.so\n";
    }
    const auto line = [&text](const archive_item &item, const char *kind, const std::string &parameter) {
        text += item.source + "\t" + kind + "\t" + item.name + (parameter.empty() ? "" : "\t" + parameter) + "\n";
    };
    for (std::size_t i = 0; i < plan.asset_count; ++i) {
        line(plan.items[i], "file", "");
    }
    const rv_pdklib::rv_texfmt_name *format = rv_pdklib::rv_texfmt_name::by_format(manifest.textures_files.format);
    for (std::size_t i = plan.first_texture; i < plan.first_texture + plan.texture_count; ++i) {
        line(plan.items[i], "texture", format != nullptr ? format->text : "");
    }
    for (std::size_t i = plan.first_sound; i < plan.first_sound + plan.sound_count; ++i) {
        line(plan.items[i], "sound", "s16le-44100-mono");
    }
    for (std::size_t i = plan.first_script; i < plan.first_script + plan.script_count; ++i) {
        const archive_item &item = plan.items[i];
        if (item.name == manifest.budget.pccl.script_entry) {
            line(item, "entry", "");
        } else {
            line(item, "module", std::filesystem::path(item.name).stem().string());
        }
    }

    std::error_code ec;
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) {
            error = "cannot write the map '" + tmp.string() + "'";
            return 1;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        error = "cannot write the map '" + path.string() + "': " + ec.message();
        return 1;
    }
    return 0;
}
