#pragma once
#include <cstdint>
#include <cstring>
#include <string_view>

namespace mora {

struct RecordTag {
    char bytes[4];
    bool operator==(const RecordTag& other) const { return std::memcmp(bytes, other.bytes, 4) == 0; }
    bool operator==(const char* str) const { return std::memcmp(bytes, str, 4) == 0; }
    std::string_view as_sv() const { return {bytes, 4}; }
    static RecordTag from(const char* s) { RecordTag t; std::memcpy(t.bytes, s, 4); return t; }
};

struct RawRecordHeader {
    RecordTag type;
    uint32_t data_size;
    uint32_t flags;
    uint32_t form_id;
    uint16_t timestamp;
    uint16_t vcs_info;
    uint16_t internal_version;
    uint16_t unknown;
};
static_assert(sizeof(RawRecordHeader) == 24);

struct RawGrupHeader {
    RecordTag type;
    uint32_t group_size;
    uint32_t label;
    uint32_t group_type;
    uint16_t timestamp;
    uint16_t vcs_info;
    uint32_t unknown;
};
static_assert(sizeof(RawGrupHeader) == 24);

struct RawSubrecordHeader {
    RecordTag type;
    uint16_t data_size;
};
static_assert(sizeof(RawSubrecordHeader) == 6);

namespace RecordFlags {
    constexpr uint32_t COMPRESSED = 0x00040000;
    constexpr uint32_t ESM       = 0x00000001;
    constexpr uint32_t LOCALIZED = 0x00000080;
    constexpr uint32_t ESL       = 0x00000200;
}

inline const RawRecordHeader* read_record_header(const uint8_t* data) {
    return reinterpret_cast<const RawRecordHeader*>(data);
}
inline const RawGrupHeader* read_grup_header(const uint8_t* data) {
    return reinterpret_cast<const RawGrupHeader*>(data);
}
inline const RawSubrecordHeader* read_subrecord_header(const uint8_t* data) {
    return reinterpret_cast<const RawSubrecordHeader*>(data);
}

} // namespace mora
