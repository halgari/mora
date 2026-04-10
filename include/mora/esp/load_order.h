#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace mora {

struct LoadOrder {
    std::vector<std::filesystem::path> plugins;
    std::filesystem::path data_dir;

    // Read all .esm and .esp files from a directory, ESMs first then ESPs
    static LoadOrder from_directory(const std::filesystem::path& data_dir);

    // Read from a plugins.txt file (MO2/vanilla format)
    // Lines starting with * are enabled, lines starting with # are comments
    static LoadOrder from_plugins_txt(const std::filesystem::path& plugins_txt,
                                       const std::filesystem::path& data_dir);

    // From explicit list
    static LoadOrder from_paths(const std::vector<std::filesystem::path>& paths);
};

} // namespace mora
