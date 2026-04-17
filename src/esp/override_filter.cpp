#include "mora/esp/override_filter.h"

namespace mora {

void OverrideFilter::observe(uint32_t global_formid, uint32_t load_idx) {
    auto it = winning.find(global_formid);
    if (it == winning.end() || load_idx > it->second) {
        winning[global_formid] = load_idx;
    }
}

bool OverrideFilter::is_winner(uint32_t global_formid, uint32_t load_idx) const {
    auto it = winning.find(global_formid);
    if (it == winning.end()) return false;
    return it->second == load_idx;
}

OverrideFilter OverrideFilter::build(const std::vector<PluginInfo>& plugins,
                                      const RuntimeIndexMap& rtmap,
                                      const std::vector<uint32_t>& load_idxs) {
    OverrideFilter f;
    for (size_t i = 0; i < plugins.size(); i++) {
        const auto& info = plugins[i];
        const uint32_t load_idx = i < load_idxs.size() ? load_idxs[i] : 0;
        for (auto& [type, records] : info.by_type) {
            for (auto& loc : records) {
                uint32_t gfid = rtmap.globalize(loc.form_id, info);
                f.observe(gfid, load_idx);
            }
        }
    }
    return f;
}

} // namespace mora
