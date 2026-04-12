#pragma once
#include "mora/eval/fact_db.h"
#include "mora/import/ini_common.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <unordered_map>

namespace mora {

using EditorIdMap = std::unordered_map<std::string, uint32_t>;

// Parse a SPID _DISTR.ini file and emit facts into the FactDB.
// next_rule_id is incremented per INI line to provide unique RuleIDs.
// editor_id_map resolves editor IDs → FormIDs so filter values match
// the FormID-typed has_keyword/race_of facts from ESP loading.
size_t emit_spid_facts(const std::string& path, FactDB& db,
                        StringPool& pool, DiagBag& diags,
                        uint32_t& next_rule_id,
                        const EditorIdMap& editor_id_map);

// Parse a KID _KID.ini file and emit facts into the FactDB.
size_t emit_kid_facts(const std::string& path, FactDB& db,
                       StringPool& pool, DiagBag& diags,
                       uint32_t& next_rule_id,
                       const EditorIdMap& editor_id_map);

// Register SPID/KID fact relations in the FactDB with proper indexes.
void configure_ini_relations(FactDB& db, StringPool& pool);

} // namespace mora
