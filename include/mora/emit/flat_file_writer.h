#pragma once
#include "mora/emit/patch_file_v2.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <array>

namespace mora::emit {

class FlatFileWriter {
public:
    FlatFileWriter();
    void add_section(SectionId id, const void* data, size_t bytes);
    void set_esp_digest(const std::array<uint8_t, 32>& digest);
    void set_toolchain_id(uint64_t id);
    std::vector<uint8_t> finish();

private:
    struct PendingSection {
        SectionId id;
        std::vector<uint8_t> payload;
    };
    std::vector<PendingSection> sections_;
    std::array<uint8_t, 32> esp_digest_{};
    uint64_t toolchain_id_ = 0;
};

} // namespace mora::emit
