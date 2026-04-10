#include "mora/esp/load_order.h"
#include <algorithm>
#include <fstream>
#include <string>

namespace mora {

LoadOrder LoadOrder::from_directory(const std::filesystem::path& data_dir) {
    LoadOrder lo;
    lo.data_dir = data_dir;

    std::vector<std::filesystem::path> esms;
    std::vector<std::filesystem::path> esps;

    if (!std::filesystem::exists(data_dir) || !std::filesystem::is_directory(data_dir)) {
        return lo;
    }

    for (auto& entry : std::filesystem::directory_iterator(data_dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        // Case-insensitive extension comparison
        std::string ext_lower = ext;
        for (auto& c : ext_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext_lower == ".esm") {
            esms.push_back(entry.path());
        } else if (ext_lower == ".esp") {
            esps.push_back(entry.path());
        }
    }

    std::sort(esms.begin(), esms.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return a.filename() < b.filename();
    });
    std::sort(esps.begin(), esps.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return a.filename() < b.filename();
    });

    lo.plugins.insert(lo.plugins.end(), esms.begin(), esms.end());
    lo.plugins.insert(lo.plugins.end(), esps.begin(), esps.end());

    return lo;
}

LoadOrder LoadOrder::from_plugins_txt(const std::filesystem::path& plugins_txt,
                                       const std::filesystem::path& data_dir) {
    LoadOrder lo;
    lo.data_dir = data_dir;

    std::ifstream file(plugins_txt);
    if (!file.is_open()) return lo;

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines
        if (line.empty()) continue;
        // Skip comments
        if (line[0] == '#') continue;
        // Lines starting with * are enabled plugins
        if (line[0] == '*') {
            std::string plugin_name = line.substr(1);
            // Trim trailing whitespace/carriage return
            while (!plugin_name.empty() &&
                   (plugin_name.back() == '\r' || plugin_name.back() == ' ' || plugin_name.back() == '\t')) {
                plugin_name.pop_back();
            }
            if (!plugin_name.empty()) {
                lo.plugins.push_back(data_dir / plugin_name);
            }
        }
    }

    return lo;
}

LoadOrder LoadOrder::from_paths(const std::vector<std::filesystem::path>& paths) {
    LoadOrder lo;
    lo.plugins = paths;
    return lo;
}

} // namespace mora
