#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace rv_editor_test
{

struct project_directory {
    std::filesystem::path root;
    std::optional<std::string> old_config;

    project_directory()
    {
        std::string pattern = (std::filesystem::temp_directory_path() / "rv_editor_project_lifecycle_XXXXXX").string();
        char *directory = ::mkdtemp(pattern.data());
        if (directory == nullptr) {
            return;
        }
        root = std::filesystem::canonical(directory);
        if (const char *config = std::getenv("XDG_CONFIG_HOME")) {
            old_config = config;
        }
        ::setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
    }

    ~project_directory()
    {
        if (root.empty()) {
            return;
        }
        if (old_config) {
            ::setenv("XDG_CONFIG_HOME", old_config->c_str(), 1);
        } else {
            ::unsetenv("XDG_CONFIG_HOME");
        }
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path project(const char *name) const
    {
        const std::filesystem::path path = root / name;
        std::filesystem::create_directories(path);
        std::ofstream(path / "disc.toml") << "[disc]\nid = \"" << name << "\"\ntitle = \"" << name << "\"\n";
        return path;
    }
};

} // namespace rv_editor_test
