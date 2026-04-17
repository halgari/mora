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

    if (!std::filesystem::exists(data_dir) || !std::filesystem::is_directory(data_dir)) {
        return lo;
    }

    // Collect every candidate plugin file together with its TES4
    // flags. Bucketing keys on flags + extension, NOT on extension
    // alone, because Skyrim engines treat ESM/ESL-flagged ESPs as
    // "master-like" for load ordering and only fall back to the
    // extension when no plugin file is readable.
    struct Candidate {
        std::filesystem::path path;
        std::string ext_lower;
        uint32_t flags = 0;
        bool readable = false;
        bool is_master() const {
            // ESM or ESL flag → master-like for ordering purposes
            // (ESPFESL ships in the master group even though the file
            // is an .esp on disk). Extension is a secondary signal:
            // .esm and .esl both imply master-ordering historically,
            // which catches plugins whose TES4 couldn't be parsed
            // *and* the rare "light master without the flag set".
            if (readable && (flags & (RecordFlags::ESM | RecordFlags::ESL)) != 0) {
                return true;
            }
            return ext_lower == ".esm" || ext_lower == ".esl";
        }
    };

    std::vector<Candidate> candidates;
    for (auto& entry : std::filesystem::directory_iterator(data_dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext_lower = to_lower(entry.path().extension().string());
        if (ext_lower != ".esm" && ext_lower != ".esp" && ext_lower != ".esl") continue;
        Candidate c;
        c.path = entry.path();
        c.ext_lower = std::move(ext_lower);
        c.readable = read_header_flag(c.path, c.flags);
        candidates.push_back(std::move(c));
    }

    // Bethesda's hardcoded master order — these always load first in
    // this order regardless of alphabetical position.
    static const std::string bethesda_masters[] = {
        "Skyrim.esm", "Update.esm", "Dawnguard.esm",
        "HearthFires.esm", "Dragonborn.esm"
    };
    auto beth_rank = [&](const std::filesystem::path& p) -> int {
        for (int i = 0; i < 5; i++) {
            if (p.filename().string() == bethesda_masters[i]) return i;
        }
        return -1;
    };

    std::vector<Candidate> masters;
    std::vector<Candidate> plugins;
    for (auto& c : candidates) {
        if (c.is_master()) masters.push_back(std::move(c));
        else plugins.push_back(std::move(c));
    }

    std::sort(masters.begin(), masters.end(),
        [&](const Candidate& a, const Candidate& b) {
            int ar = beth_rank(a.path), br = beth_rank(b.path);
            bool a_beth = ar >= 0, b_beth = br >= 0;
            if (a_beth != b_beth) return a_beth;  // Bethesda first
            if (a_beth) return ar < br;            // canonical order
            return a.path.filename() < b.path.filename();
        });
    std::sort(plugins.begin(), plugins.end(),
        [](const Candidate& a, const Candidate& b) {
            return a.path.filename() < b.path.filename();
        });

    for (auto& c : masters) lo.plugins.push_back(c.path);
    for (auto& c : plugins) lo.plugins.push_back(c.path);

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
        e.is_esl    = (flags & RecordFlags::ESL) != 0;
        e.is_master = (flags & RecordFlags::ESM) != 0;
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
