#include "mora/emit/patch_table.h"
#include "mora/emit/flat_file_writer.h"
#include "mora/emit/patch_file_v2.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace mora {

namespace {

// Append raw bytes to a vector
template<typename T>
void append_bytes(std::vector<uint8_t>& buf, const T& val) {
    const auto* p = reinterpret_cast<const uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(T));
}

// Build the StringTable section bytes ([u16 len][data]...) while also
// producing the PatchEntry array, translating string values into offsets.
std::vector<uint8_t> build_string_table_and_entries(
    const ResolvedPatchSet& patches,
    StringPool& pool,
    std::vector<PatchEntry>& out_entries) {

    auto sorted = patches.all_patches_sorted();
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
                uint16_t len = static_cast<uint16_t>(sv.size());
                append_bytes(string_table, len);
                string_table.insert(string_table.end(), sv.begin(), sv.end());
            }
        }
    }

    out_entries.clear();
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
                    continue;
            }

            out_entries.push_back(e);
        }
    }

    return string_table;
}

} // anonymous namespace

std::vector<uint8_t> serialize_patch_table(const ResolvedPatchSet& patches,
                                            StringPool& pool) {
    std::array<uint8_t, 32> zero{};
    return serialize_patch_table(patches, pool, zero);
}

std::vector<uint8_t> serialize_patch_table(const ResolvedPatchSet& patches,
                                            StringPool& pool,
                                            const std::array<uint8_t, 32>& esp_digest) {
    std::vector<PatchEntry> entries;
    auto string_table = build_string_table_and_entries(patches, pool, entries);

    emit::FlatFileWriter w;
    w.set_esp_digest(esp_digest);
    if (!string_table.empty()) {
        w.add_section(emit::SectionId::StringTable,
                      string_table.data(), string_table.size());
    }
    w.add_section(emit::SectionId::Patches,
                  entries.data(), entries.size() * sizeof(PatchEntry));
    return w.finish();
}

std::vector<uint8_t> serialize_patch_table(const std::vector<PatchEntry>& entries) {
    std::array<uint8_t, 32> zero{};
    return serialize_patch_table(entries, zero);
}

std::vector<uint8_t> serialize_patch_table(const std::vector<PatchEntry>& entries,
                                            const std::array<uint8_t, 32>& esp_digest) {
    emit::FlatFileWriter w;
    w.set_esp_digest(esp_digest);
    w.add_section(emit::SectionId::Patches,
                  entries.data(), entries.size() * sizeof(PatchEntry));
    return w.finish();
}

} // namespace mora
