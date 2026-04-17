#include "mora/esp/plugin_index.h"
#include <cstring>
#include <stdexcept>
#include <string>

namespace mora {

namespace {

void scan_grup(const uint8_t* base, size_t file_size, uint32_t grup_offset, PluginInfo& info) {
    auto* gh = read_grup_header(base + grup_offset);
    if (!(gh->type == "GRUP")) {
        throw std::runtime_error("Expected GRUP at offset " + std::to_string(grup_offset));
    }

    uint32_t const grup_end = grup_offset + gh->group_size;
    if (grup_end > file_size) {
        throw std::runtime_error("GRUP extends past end of file");
    }

    // Entries start after the GRUP header (24 bytes)
    uint32_t pos = grup_offset + sizeof(RawGrupHeader);

    while (pos < grup_end) {
        // Peek at the tag to determine if this is a sub-GRUP or a record
        auto* tag = reinterpret_cast<const RecordTag*>(base + pos);

        if (*tag == "GRUP") {
            // Recurse into sub-GRUP
            auto* sub_gh = read_grup_header(base + pos);
            scan_grup(base, file_size, pos, info);
            pos += sub_gh->group_size;
        } else {
            // It's a record
            auto* rh = read_record_header(base + pos);
            std::string const type_str(rh->type.as_sv());

            info.by_type[type_str].push_back(RecordLocation{
                .form_id = rh->form_id,
                .offset = pos,
                .data_size = rh->data_size,
                .flags = rh->flags,
            });

            // Advance past record header + data
            pos += sizeof(RawRecordHeader) + rh->data_size;
        }
    }
}

} // anonymous namespace

PluginInfo build_plugin_index(const MmapFile& file, const std::string& filename) {
    PluginInfo info;
    info.filename = filename;

    auto data = file.span();
    const uint8_t* base = data.data();
    size_t const file_size = data.size();

    if (file_size < sizeof(RawRecordHeader)) {
        throw std::runtime_error("File too small to contain a record header");
    }

    // Phase 1: Parse TES4 record at offset 0
    auto* tes4 = read_record_header(base);
    if (!(tes4->type == "TES4")) {
        throw std::runtime_error("Expected TES4 record at start of file");
    }

    info.flags = tes4->flags;

    // Walk subrecords within TES4 data region
    uint32_t sub_pos = sizeof(RawRecordHeader);
    uint32_t const tes4_end = sizeof(RawRecordHeader) + tes4->data_size;

    while (sub_pos < tes4_end) {
        auto* sub = read_subrecord_header(base + sub_pos);
        uint32_t const sub_data_offset = sub_pos + sizeof(RawSubrecordHeader);

        if (sub->type == "HEDR" && sub->data_size >= 8) {
            // HEDR: float version (4 bytes), uint32_t num_records (4 bytes), ...
            std::memcpy(&info.version, base + sub_data_offset, sizeof(float));
            std::memcpy(&info.num_records, base + sub_data_offset + 4, sizeof(uint32_t));
        } else if (sub->type == "MAST") {
            // Null-terminated string
            std::string master(reinterpret_cast<const char*>(base + sub_data_offset));
            info.masters.push_back(std::move(master));
        }

        sub_pos = sub_data_offset + sub->data_size;
    }

    // Phase 2: Scan top-level GRUPs after TES4
    uint32_t pos = tes4_end;

    while (pos < file_size) {
        if (pos + sizeof(RawGrupHeader) > file_size) break;

        scan_grup(base, file_size, pos, info);

        auto* gh = read_grup_header(base + pos);
        pos += gh->group_size;
    }

    return info;
}

ResolvedFormID resolve_form_id(uint32_t raw_id, const PluginInfo& info) {
    uint8_t const master_index = (raw_id >> 24) & 0xFF;
    uint32_t const object_id = raw_id & 0x00FFFFFF;

    if (master_index < info.masters.size()) {
        return ResolvedFormID{info.masters[master_index], object_id};
    }
    return ResolvedFormID{info.filename, object_id};
}

} // namespace mora
