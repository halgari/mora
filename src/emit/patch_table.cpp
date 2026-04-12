#include "mora/emit/patch_table.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace mora {

namespace {

// Address Library IDs (copied from ir_emitter.cpp)
constexpr uint64_t kFormMapAddrLibId_AE = 400507;
constexpr uint64_t kFormMapAddrLibId_SE = 514351;

constexpr uint64_t kMemMgr_GetSingleton_AE = 11141;
constexpr uint64_t kMemMgr_GetSingleton_SE = 11045;
constexpr uint64_t kMemMgr_Allocate_AE     = 68115;
constexpr uint64_t kMemMgr_Allocate_SE     = 66859;
constexpr uint64_t kMemMgr_Deallocate_AE   = 68117;
constexpr uint64_t kMemMgr_Deallocate_SE   = 66861;

constexpr uint64_t kBS_Ctor8_AE    = 69161;
constexpr uint64_t kBS_Ctor8_SE    = 67819;
constexpr uint64_t kBS_Release8_AE = 69192;
constexpr uint64_t kBS_Release8_SE = 67847;

uint64_t resolve_ae_or_se(const AddressLibrary& al, uint64_t ae_id, uint64_t se_id) {
    return al.resolve(ae_id).value_or(al.resolve(se_id).value_or(0));
}

// Append raw bytes to a vector
template<typename T>
void append_bytes(std::vector<uint8_t>& buf, const T& val) {
    const auto* p = reinterpret_cast<const uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(T));
}

} // anonymous namespace

std::vector<uint8_t> serialize_patch_table(const ResolvedPatchSet& patches,
                                            StringPool& pool,
                                            const AddressLibrary& addrlib) {
    auto sorted = patches.all_patches_sorted();

    // Step 1: Collect all string values into a string table.
    // Map StringId::index -> offset in string table bytes.
    std::vector<uint8_t> string_table;
    std::unordered_map<uint32_t, uint32_t> string_offsets; // StringId::index -> byte offset

    for (const auto& rp : sorted) {
        for (const auto& fp : rp.fields) {
            if (fp.value.kind() == Value::Kind::String) {
                uint32_t sid = fp.value.as_string().index;
                if (string_offsets.count(sid)) continue;
                std::string_view sv = pool.get(fp.value.as_string());
                uint32_t offset = static_cast<uint32_t>(string_table.size());
                string_offsets[sid] = offset;
                // Write uint16_t length followed by string bytes (no null terminator)
                uint16_t len = static_cast<uint16_t>(sv.size());
                append_bytes(string_table, len);
                string_table.insert(string_table.end(), sv.begin(), sv.end());
            }
        }
    }

    // Step 2: Build patch entries
    std::vector<PatchEntry> entries;
    for (const auto& rp : sorted) {
        for (const auto& fp : rp.fields) {
            PatchEntry e{};
            e.formid = rp.target_formid;
            e.field_id = static_cast<uint8_t>(fp.field);
            e.op = static_cast<uint8_t>(fp.op);

            switch (fp.value.kind()) {
                case Value::Kind::FormID:
                    e.value_type = static_cast<uint8_t>(PatchValueType::FormID);
                    e.value = fp.value.as_formid();
                    break;
                case Value::Kind::Int: {
                    e.value_type = static_cast<uint8_t>(PatchValueType::Int);
                    int64_t ival = fp.value.as_int();
                    std::memcpy(&e.value, &ival, 8);
                    break;
                }
                case Value::Kind::Float: {
                    e.value_type = static_cast<uint8_t>(PatchValueType::Float);
                    double d = fp.value.as_float();
                    std::memcpy(&e.value, &d, 8);
                    break;
                }
                case Value::Kind::String: {
                    e.value_type = static_cast<uint8_t>(PatchValueType::StringIndex);
                    e.value = string_offsets[fp.value.as_string().index];
                    break;
                }
                default:
                    // Skip unsupported value types
                    continue;
            }

            entries.push_back(e);
        }
    }

    // Step 3: Build header
    PatchTableHeader hdr;
    hdr.patch_count = static_cast<uint32_t>(entries.size());
    hdr.string_table_size = static_cast<uint32_t>(string_table.size());
    hdr.map_offset = resolve_ae_or_se(addrlib, kFormMapAddrLibId_AE, kFormMapAddrLibId_SE);
    hdr.mm_singleton_off = resolve_ae_or_se(addrlib, kMemMgr_GetSingleton_AE, kMemMgr_GetSingleton_SE);
    hdr.mm_allocate_off = resolve_ae_or_se(addrlib, kMemMgr_Allocate_AE, kMemMgr_Allocate_SE);
    hdr.mm_deallocate_off = resolve_ae_or_se(addrlib, kMemMgr_Deallocate_AE, kMemMgr_Deallocate_SE);
    hdr.bs_ctor8_off = resolve_ae_or_se(addrlib, kBS_Ctor8_AE, kBS_Ctor8_SE);
    hdr.bs_release8_off = resolve_ae_or_se(addrlib, kBS_Release8_AE, kBS_Release8_SE);

    // Step 4: Serialize: header + string table + entries
    std::vector<uint8_t> result;
    result.reserve(sizeof(PatchTableHeader) + string_table.size() +
                   entries.size() * sizeof(PatchEntry));

    append_bytes(result, hdr);
    result.insert(result.end(), string_table.begin(), string_table.end());
    for (const auto& e : entries) {
        append_bytes(result, e);
    }

    return result;
}

std::vector<uint8_t> serialize_patch_table(const std::vector<PatchEntry>& entries,
                                            const AddressLibrary& addrlib) {
    PatchTableHeader hdr;
    hdr.patch_count = static_cast<uint32_t>(entries.size());
    hdr.string_table_size = 0; // no string table in fast path
    hdr.map_offset = resolve_ae_or_se(addrlib, kFormMapAddrLibId_AE, kFormMapAddrLibId_SE);
    hdr.mm_singleton_off = resolve_ae_or_se(addrlib, kMemMgr_GetSingleton_AE, kMemMgr_GetSingleton_SE);
    hdr.mm_allocate_off = resolve_ae_or_se(addrlib, kMemMgr_Allocate_AE, kMemMgr_Allocate_SE);
    hdr.mm_deallocate_off = resolve_ae_or_se(addrlib, kMemMgr_Deallocate_AE, kMemMgr_Deallocate_SE);
    hdr.bs_ctor8_off = resolve_ae_or_se(addrlib, kBS_Ctor8_AE, kBS_Ctor8_SE);
    hdr.bs_release8_off = resolve_ae_or_se(addrlib, kBS_Release8_AE, kBS_Release8_SE);

    std::vector<uint8_t> result;
    result.reserve(sizeof(PatchTableHeader) + entries.size() * sizeof(PatchEntry));

    append_bytes(result, hdr);
    for (const auto& e : entries) {
        append_bytes(result, e);
    }

    return result;
}

} // namespace mora
