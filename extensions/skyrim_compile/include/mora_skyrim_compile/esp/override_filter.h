#pragma once
#include "mora_skyrim_compile/esp/load_order.h"
#include "mora_skyrim_compile/esp/plugin_index.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mora {

// Tracks which plugin "owns" each global FormID in a load order.
//
// Skyrim's engine applies a whole-record-replacement rule: when two
// plugins declare the same global FormID, the later plugin's record
// is what the runtime sees; the earlier record is discarded entirely.
// OverrideFilter replicates that by walking the load order once in
// ascending order and upserting `(form_id → load_idx)`. Fact
// extraction then gates on `is_winner(gfid, this_load_idx)` so
// overridden records emit no facts.
struct OverrideFilter {
    std::unordered_map<uint32_t /*global_formid*/, uint32_t /*load_idx*/> winning;

    void observe(uint32_t global_formid, uint32_t load_idx);
    bool is_winner(uint32_t global_formid, uint32_t load_idx) const;

    // Build the filter by globalizing every RecordLocation across
    // every plugin and tracking the highest load_idx per global
    // FormID. `load_idxs[i]` is the scalar load position we want
    // recorded for `plugins[i]`'s records — regular plugins use
    // their 0–0xFD index, light plugins are typically passed a
    // synthetic value so orderings within the same high byte still
    // resolve cleanly (caller's choice).
    static OverrideFilter build(const std::vector<PluginInfo>& plugins,
                                const RuntimeIndexMap& rtmap,
                                const std::vector<uint32_t>& load_idxs);
};

} // namespace mora
