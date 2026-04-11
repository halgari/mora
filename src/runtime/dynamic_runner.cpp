#include "mora/runtime/dynamic_runner.h"
#include <array>
#include <cstring>
#include <fstream>

namespace mora {

DynamicRunner::DynamicRunner(StringPool& pool, DiagBag& diags)
    : pool_(pool), diags_(diags) {}

bool DynamicRunner::load(const std::filesystem::path& rt_path) {
    std::ifstream file(rt_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read magic: "MORT"
    std::array<char, 4> magic{};
    file.read(magic.data(), 4);
    if (!file || std::memcmp(magic.data(), "MORT", 4) != 0) {
        return false;
    }

    // Read version (uint16_t little-endian)
    uint16_t version = 0;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file || version != 1) {
        return false;
    }

    // Read rule count (uint32_t little-endian)
    uint32_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!file) {
        return false;
    }

    rule_count_ = count;
    return true;
}

void DynamicRunner::on_npc_load([[maybe_unused]] uint32_t npc_formid) {
    // Phase 1 stub — dynamic rule evaluation will be implemented later
}

void DynamicRunner::on_data_loaded() {
    // Phase 1 stub — triggered after Skyrim's DataLoaded event
}

} // namespace mora
