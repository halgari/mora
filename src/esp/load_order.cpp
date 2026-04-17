#include "mora/esp/load_order.h"
#include "mora/esp/mmap_file.h"
#include "mora/esp/plugin_index.h"
#include "mora/esp/record_types.h"
#include <algorithm>
#include <fstream>
#include <string>

namespace mora {

namespace {

std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void trim_trailing(std::string& s) {
    while (!s.empty() &&
           (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
}

// Parse just the TES4 header: walk subrecords for HEDR/MAST, stop
// before scanning GRUPs. Used by resolve_entries() to collect ESL
// flags across the whole load order cheaply — the by-type index isn't
// needed at that stage.
bool read_header_flag(const std::filesystem::path& path, uint32_t& out_flags) {
    try {
        MmapFile file(path.string());
        auto data = file.span();
        if (data.size() < sizeof(RawRecordHeader)) return false;
        auto* tes4 = read_record_header(data.data());
        if (!(tes4->type == "TES4")) return false;
        out_flags = tes4->flags;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

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
        auto ext_lower = to_lower(entry.path().extension().string());
        if (ext_lower == ".esm" || ext_lower == ".esl") {
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
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        if (line[0] != '*') continue; // disabled entries are not loaded
        std::string plugin_name = line.substr(1);
        trim_trailing(plugin_name);
        if (plugin_name.empty()) continue;
        lo.plugins.push_back(data_dir / plugin_name);
    }

    // Skyrim.esm is always loaded first by the engine, regardless of
    // whether plugins.txt lists it (modern setups do; older setups
    // implicitly prepended it). Force that invariant here so
    // downstream RuntimeIndexMap always sees Skyrim.esm at index 0.
    auto is_skyrim_esm = [](const std::filesystem::path& p) {
        return to_lower(p.filename().string()) == "skyrim.esm";
    };
    auto it = std::find_if(lo.plugins.begin(), lo.plugins.end(), is_skyrim_esm);
    if (it == lo.plugins.end()) {
        lo.plugins.insert(lo.plugins.begin(), data_dir / "Skyrim.esm");
    } else if (it != lo.plugins.begin()) {
        auto path = *it;
        lo.plugins.erase(it);
        lo.plugins.insert(lo.plugins.begin(), path);
    }

    return lo;
}

LoadOrder LoadOrder::from_paths(const std::vector<std::filesystem::path>& paths) {
    LoadOrder lo;
    lo.plugins = paths;
    return lo;
}

std::vector<PluginOrderEntry> LoadOrder::resolve_entries() const {
    std::vector<PluginOrderEntry> out;
    out.reserve(plugins.size());
    for (auto& p : plugins) {
        uint32_t flags = 0;
        if (!read_header_flag(p, flags)) continue;
        PluginOrderEntry e;
        e.path = p;
        e.basename_lower = to_lower(p.filename().string());
        e.is_esl = (flags & RecordFlags::ESL) != 0;
        out.push_back(std::move(e));
    }
    return out;
}

RuntimeIndexMap LoadOrder::runtime_index_map() const {
    return RuntimeIndexMap::build(resolve_entries());
}

RuntimeIndexMap RuntimeIndexMap::build(const std::vector<PluginOrderEntry>& entries) {
    RuntimeIndexMap map;
    uint32_t regular_idx = 0;
    uint32_t light_idx = 0;
    for (auto& e : entries) {
        if (e.is_esl) {
            if (light_idx > 0xFFF) continue; // runtime caps ESL space at 0xFFF
            map.index[e.basename_lower] = light_idx++;
            map.light.insert(e.basename_lower);
        } else {
            if (regular_idx > 0xFD) continue; // 0xFE is ESL space, 0xFF is synthetic
            map.index[e.basename_lower] = regular_idx++;
        }
    }
    return map;
}

uint32_t RuntimeIndexMap::globalize(uint32_t local_id, const PluginInfo& info) const {
    uint8_t master_byte = static_cast<uint8_t>((local_id >> 24) & 0xFF);
    uint32_t object_id = local_id & 0x00FFFFFF;

    std::string target;
    if (master_byte < info.masters.size()) {
        target = info.masters[master_byte];
    } else {
        target = info.filename;
    }
    std::string key = target;
    for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto it = index.find(key);
    if (it == index.end()) return local_id;

    if (light.count(key)) {
        uint32_t esl_idx = it->second & 0xFFF;
        return 0xFE000000u | (esl_idx << 12) | (object_id & 0xFFF);
    }
    uint32_t idx = it->second & 0xFF;
    return (idx << 24) | (object_id & 0x00FFFFFFu);
}

} // namespace mora
