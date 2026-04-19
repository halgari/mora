#pragma once
#include "mora/data/schema_registry.h"
#include "mora/eval/fact_db.h"
#include "mora_skyrim_compile/esp/load_order.h"
#include "mora_skyrim_compile/esp/mmap_file.h"
#include "mora_skyrim_compile/esp/override_filter.h"
#include "mora_skyrim_compile/esp/plugin_index.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace mora {

class EspReader {
public:
    EspReader(StringPool& pool, DiagBag& diags, const SchemaRegistry& schema);

    // Restrict which relations to extract (lazy loading).
    // If not called, extracts ALL registered relations.
    // Pass the set of relation names that rules actually reference.
    void set_needed_relations(const std::unordered_set<uint32_t>& relation_name_indexes);

    // Attach a RuntimeIndexMap for globalizing local form ids. Without
    // one, the reader falls back to the legacy directory-walk index —
    // correct for single-plugin callers (tests, `mora info` against a
    // fixture) but wrong for a real load order where the compile-time
    // order diverges from the runtime one.
    void set_runtime_index_map(const RuntimeIndexMap* map);

    // Attach an OverrideFilter and this reader's per-plugin scalar
    // load index. When set, records whose global FormID lost the
    // override race emit zero facts. Without a filter the reader
    // emits every record it scans (legacy single-plugin behavior).
    void set_override_filter(const OverrideFilter* filter, uint32_t load_idx);

    // Read a single plugin file, populate facts into db
    void read_plugin(const std::filesystem::path& path, FactDB& db);

    // Extract facts from an already-mmapped, already-indexed plugin.
    // Lets the caller share a single build_plugin_index parse between
    // the override-filter build step and the final fact emission step
    // so the heavy GRUP walk happens exactly once.
    void extract_from(const MmapFile& file, const PluginInfo& info, FactDB& db);

    // Read all plugins in order
    void read_load_order(const std::vector<std::filesystem::path>& plugins, FactDB& db);

    // EditorID -> global FormID mapping (built during reading)
    const std::unordered_map<std::string, uint32_t>& editor_id_map() const;

    // Resolve a .mora symbolic reference to a FormID. Returns 0 if not found.
    uint32_t resolve_symbol(const std::string& editor_id) const;

    // Stats
    size_t records_processed() const { return records_processed_; }
    size_t facts_generated() const { return facts_generated_; }

private:
    void extract_record_facts(const MmapFile& file, const PluginInfo& info,
                               const RecordLocation& loc,
                               const std::vector<const RelationSchema*>& schemas,
                               FactDB& db);

    Value read_esp_value(std::span<const uint8_t> data, size_t offset,
                         ReadType read_type, bool localized);

    // Convert a local FormID from an ESP to a global FormID
    uint32_t make_global_formid(uint32_t local_id, const PluginInfo& info);

    StringPool& pool_;
    DiagBag& diags_;
    const SchemaRegistry& schema_;
    bool is_relation_needed(StringId name) const;

    std::unordered_map<std::string, uint32_t> editor_ids_;
    std::unordered_set<uint32_t> needed_relations_; // empty = all needed
    bool filter_active_ = false;
    size_t records_processed_ = 0;
    size_t facts_generated_ = 0;
    size_t relations_skipped_ = 0;
    uint32_t current_load_index_ = 0;
    const RuntimeIndexMap* runtime_index_ = nullptr;
    const OverrideFilter* override_filter_ = nullptr;
    uint32_t reader_load_idx_ = 0;
};

} // namespace mora
