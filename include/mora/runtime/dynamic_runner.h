#pragma once
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <filesystem>

namespace mora {

class DynamicRunner {
public:
    DynamicRunner(StringPool& pool, DiagBag& diags);
    bool load(const std::filesystem::path& rt_path);
    void on_npc_load(uint32_t npc_formid);
    void on_data_loaded();
    size_t rules_loaded() const { return rule_count_; }
private:
    StringPool& pool_;
    DiagBag& diags_;
    size_t rule_count_ = 0;
};

} // namespace mora
