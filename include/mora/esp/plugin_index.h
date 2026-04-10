#pragma once
#include "mora/esp/mmap_file.h"
#include "mora/esp/record_types.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mora {

struct RecordLocation {
    uint32_t form_id;
    uint32_t offset;        // byte offset of the RECORD HEADER (not data)
    uint32_t data_size;
    uint32_t flags;
};

struct PluginInfo {
    float version = 0;
    uint32_t num_records = 0;
    uint32_t flags = 0;
    std::vector<std::string> masters;
    std::string filename;

    // Records grouped by 4-char type string
    std::unordered_map<std::string, std::vector<RecordLocation>> by_type;

    bool is_localized() const { return (flags & RecordFlags::LOCALIZED) != 0; }
    bool is_esm() const { return (flags & RecordFlags::ESM) != 0; }
    bool is_esl() const { return (flags & RecordFlags::ESL) != 0; }
};

PluginInfo build_plugin_index(const MmapFile& file, const std::string& filename);

struct ResolvedFormID {
    std::string_view master;
    uint32_t object_id;
};
ResolvedFormID resolve_form_id(uint32_t raw_id, const PluginInfo& info);

} // namespace mora
