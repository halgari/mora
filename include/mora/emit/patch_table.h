#pragma once
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include <vector>
#include <cstdint>

namespace mora {

struct PatchTableHeader {
    uint32_t magic = 0x4D4F5241; // "MORA"
    uint32_t version = 3;
    uint32_t patch_count = 0;
    uint32_t string_table_size = 0; // bytes
    // v3: Address Library offsets removed — CommonLibSSE-NG handles resolution
};

// Value type tags for patch entries
enum class PatchValueType : uint8_t {
    FormID = 0,
    Int = 1,
    Float = 2,
    StringIndex = 3,
};

struct PatchEntry {
    uint32_t formid;
    uint8_t field_id;   // FieldId enum value
    uint8_t op;         // FieldOp enum value
    uint8_t value_type; // PatchValueType
    uint8_t pad = 0;
    uint64_t value;     // interpretation depends on value_type
};
static_assert(sizeof(PatchEntry) == 16);

// Serialize patches into a binary blob ready for embedding in a DLL.
std::vector<uint8_t> serialize_patch_table(const ResolvedPatchSet& patches,
                                            StringPool& pool);

// Serialize pre-built PatchEntry array directly (fast path for PatchBuffer).
// Entries must already be sorted and deduped. No string table support (all
// values are FormID/Int/Float).
std::vector<uint8_t> serialize_patch_table(const std::vector<PatchEntry>& entries);

} // namespace mora
