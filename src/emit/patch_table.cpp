#include "mora/emit/patch_table.h"
#include "mora/emit/flat_file_writer.h"
#include "mora/emit/patch_file_v2.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace mora {

namespace {

// Append raw bytes to a vector
template<typename T>
void append_bytes(std::vector<uint8_t>& buf, const T& val) {
    const auto* p = reinterpret_cast<const uint8_t*>(&val);
    buf.insert(buf.end(), p, p + sizeof(T));
}

const char* value_kind_name(Value::Kind k) {
    switch (k) {
        case Value::Kind::Var:    return "Var";
        case Value::Kind::FormID: return "FormID";
        case Value::Kind::Int:    return "Int";
        case Value::Kind::Float:  return "Float";
        case Value::Kind::String: return "String";
        case Value::Kind::Bool:   return "Bool";
        case Value::Kind::List:   return "List";
    }
    return "?";
}

} // anonymous namespace

std::vector<uint8_t> build_patch_entries_and_string_table(
    const ResolvedPatchSet& patches,
    StringPool& pool,
    std::vector<PatchEntry>& out_entries) {

    auto sorted = patches.all_patches_sorted();
    std::vector<uint8_t> string_table;
    std::unordered_map<uint32_t, uint32_t> string_offsets; // StringId::index -> byte offset

    for (const auto& rp : sorted) {
        for (const auto& fp : rp.fields) {
            if (fp.value.kind() == Value::Kind::String) {
                uint32_t const sid = fp.value.as_string().index;
                if (string_offsets.contains(sid)) continue;
                std::string_view const sv = pool.get(fp.value.as_string());
                uint32_t const offset = static_cast<uint32_t>(string_table.size());
                string_offsets[sid] = offset;
                uint16_t const len = static_cast<uint16_t>(sv.size());
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
                    throw std::runtime_error(
                        std::string("unsupported Value::Kind in patch conversion: ") +
                        value_kind_name(fp.value.kind()));
            }

            out_entries.push_back(e);
        }
    }

    return string_table;
}

std::vector<uint8_t> serialize_patch_table(const ResolvedPatchSet& patches,
                                            StringPool& pool) {
    std::array<uint8_t, 32> const zero{};
    return serialize_patch_table(patches, pool, zero);
}

std::vector<uint8_t> serialize_patch_table(const ResolvedPatchSet& patches,
                                            StringPool& pool,
                                            const std::array<uint8_t, 32>& esp_digest) {
    std::vector<PatchEntry> entries;
    auto string_table = build_patch_entries_and_string_table(patches, pool, entries);

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
    std::array<uint8_t, 32> const zero{};
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

std::vector<uint8_t> serialize_patch_table(
    const std::vector<PatchEntry>& entries,
    const std::array<uint8_t, 32>& esp_digest,
    const std::vector<uint8_t>& arrangements_section) {

    emit::FlatFileWriter w;
    w.set_esp_digest(esp_digest);
    w.add_section(emit::SectionId::Patches,
                  entries.data(), entries.size() * sizeof(PatchEntry));
    if (!arrangements_section.empty()) {
        w.add_section(emit::SectionId::Arrangements,
                      arrangements_section.data(), arrangements_section.size());
    }
    return w.finish();
}

std::vector<uint8_t> serialize_patch_table(
    const std::vector<PatchEntry>& entries,
    const std::array<uint8_t, 32>& esp_digest,
    const std::vector<uint8_t>& arrangements_section,
    const std::vector<uint8_t>& dag_bytecode) {

    std::vector<uint8_t> const empty;
    return serialize_patch_table(entries, esp_digest, arrangements_section,
                                 dag_bytecode, empty);
}

std::vector<uint8_t> serialize_patch_table(
    const std::vector<PatchEntry>& entries,
    const std::array<uint8_t, 32>& esp_digest,
    const std::vector<uint8_t>& arrangements_section,
    const std::vector<uint8_t>& dag_bytecode,
    const std::vector<uint8_t>& string_table_section) {

    emit::FlatFileWriter w;
    w.set_esp_digest(esp_digest);
    if (!string_table_section.empty()) {
        w.add_section(emit::SectionId::StringTable,
                      string_table_section.data(), string_table_section.size());
    }
    w.add_section(emit::SectionId::Patches,
                  entries.data(), entries.size() * sizeof(PatchEntry));
    if (!arrangements_section.empty()) {
        w.add_section(emit::SectionId::Arrangements,
                      arrangements_section.data(), arrangements_section.size());
    }
    if (!dag_bytecode.empty()) {
        w.add_section(emit::SectionId::DagBytecode,
                      dag_bytecode.data(), dag_bytecode.size());
    }
    return w.finish();
}

} // namespace mora
