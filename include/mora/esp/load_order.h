#pragma once
#include "mora/esp/plugin_index.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mora {

struct PluginOrderEntry {
    std::filesystem::path path;
    std::string basename_lower;
    bool is_esl;
};

// Maps plugin basename (lowercase) to its runtime load-order index.
//
// For a regular plugin the recorded value is the 0x00–0xFD high byte
// Skyrim places into global FormIDs. For an ESL-flagged plugin the
// value is the 12-bit "light index" living under the shared 0xFE high
// byte; `light` tracks which basenames fall into that space so
// `globalize` can synthesize the right layout.
struct RuntimeIndexMap {
    std::unordered_map<std::string, uint32_t> index;
    std::unordered_set<std::string> light;

    // Translate a raw local FormID (as stored in the plugin file) into
    // its runtime global FormID. `info` supplies the MAST list so the
    // local-id's high byte can be resolved back to the plugin it
    // references (or to `info.filename` if it's a self-reference).
    //
    // Plugins absent from the map pass through unchanged.
    uint32_t globalize(uint32_t local_id, const PluginInfo& info) const;

    // Build the map from an ordered list of plugin entries. Non-ESL
    // entries get indices 0, 1, 2, …; ESL entries get light indices
    // 0, 1, 2, … under the 0xFE high byte.
    static RuntimeIndexMap build(const std::vector<PluginOrderEntry>& entries);
};

struct LoadOrder {
    std::vector<std::filesystem::path> plugins;
    std::filesystem::path data_dir;

    // Read all .esm and .esp files from a directory, ESMs first then ESPs
    static LoadOrder from_directory(const std::filesystem::path& data_dir);

    // Read from a plugins.txt file (MO2/vanilla format). Only `*`-prefixed
    // (enabled) plugin lines are honored, comments and disabled entries
    // are ignored. Skyrim.esm is always forced to load first.
    static LoadOrder from_plugins_txt(const std::filesystem::path& plugins_txt,
                                       const std::filesystem::path& data_dir);

    // From explicit list
    static LoadOrder from_paths(const std::vector<std::filesystem::path>& paths);

    // Read each plugin's TES4 header to collect its ESL flag, producing
    // an ordered entry list suitable for RuntimeIndexMap::build.
    // Plugins whose files don't exist or fail to parse are skipped.
    std::vector<PluginOrderEntry> resolve_entries() const;

    // Convenience: resolve_entries() + RuntimeIndexMap::build().
    RuntimeIndexMap runtime_index_map() const;
};

} // namespace mora
