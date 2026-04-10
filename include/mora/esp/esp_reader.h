#pragma once
#include "mora/data/schema_registry.h"
#include "mora/eval/fact_db.h"
#include "mora/esp/mmap_file.h"
#include "mora/esp/plugin_index.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <filesystem>
#include <unordered_map>

namespace mora {

class EspReader {
public:
    EspReader(StringPool& pool, DiagBag& diags, const SchemaRegistry& schema);

    // Read a single plugin file, populate facts into db
    void read_plugin(const std::filesystem::path& path, FactDB& db);

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
    std::unordered_map<std::string, uint32_t> editor_ids_;
    size_t records_processed_ = 0;
    size_t facts_generated_ = 0;
    uint32_t current_load_index_ = 0;
};

} // namespace mora
