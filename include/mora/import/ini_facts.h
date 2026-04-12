#pragma once
#include "mora/eval/fact_db.h"
#include "mora/import/ini_common.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <unordered_map>
#include <unordered_set>

namespace mora {

using EditorIdMap = std::unordered_map<std::string, uint32_t>;
using PluginSet = std::unordered_set<std::string>;

struct IniLoadStats {
    size_t rules_emitted = 0;
    size_t rules_dropped_missing_plugin = 0;
    size_t unresolved_editor_ids = 0;
    // plugin filename → count of dropped rules
    std::unordered_map<std::string, size_t> missing_plugins;
};

// Parse a SPID _DISTR.ini file and emit facts into the FactDB.
size_t emit_spid_facts(const std::string& path, FactDB& db,
                        StringPool& pool, DiagBag& diags,
                        uint32_t& next_rule_id,
                        const EditorIdMap& editor_id_map,
                        const PluginSet& loaded_plugins,
                        IniLoadStats& stats);

// Parse a KID _KID.ini file and emit facts into the FactDB.
size_t emit_kid_facts(const std::string& path, FactDB& db,
                       StringPool& pool, DiagBag& diags,
                       uint32_t& next_rule_id,
                       const EditorIdMap& editor_id_map,
                       const PluginSet& loaded_plugins,
                       IniLoadStats& stats);

// Register SPID/KID fact relations in the FactDB with proper indexes.
void configure_ini_relations(FactDB& db, StringPool& pool);

} // namespace mora
