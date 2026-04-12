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

    // Bethesda's hardcoded master order — these always load first in this order
    static const std::string bethesda_masters[] = {
        "Skyrim.esm", "Update.esm", "Dawnguard.esm",
        "HearthFires.esm", "Dragonborn.esm"
    };

    // Partition ESMs into Bethesda masters (in fixed order) and others (alphabetical)
    std::vector<std::filesystem::path> beth_esms;
    std::vector<std::filesystem::path> other_esms;
    for (auto& p : esms) {
        bool is_beth = false;
        for (auto& m : bethesda_masters) {
            if (p.filename().string() == m) { is_beth = true; break; }
        }
        if (is_beth) beth_esms.push_back(p);
        else other_esms.push_back(p);
    }

    // Sort Bethesda masters into the canonical order
    std::sort(beth_esms.begin(), beth_esms.end(),
        [&](const std::filesystem::path& a, const std::filesystem::path& b) {
            int ai = 99, bi = 99;
            for (int i = 0; i < 5; i++) {
                if (a.filename().string() == bethesda_masters[i]) ai = i;
                if (b.filename().string() == bethesda_masters[i]) bi = i;
            }
            return ai < bi;
        });

    std::sort(other_esms.begin(), other_esms.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return a.filename() < b.filename();
    });
    std::sort(esps.begin(), esps.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return a.filename() < b.filename();
    });

    lo.plugins.insert(lo.plugins.end(), beth_esms.begin(), beth_esms.end());
    lo.plugins.insert(lo.plugins.end(), other_esms.begin(), other_esms.end());
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
