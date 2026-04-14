#pragma once
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include <array>
#include <vector>
#include <cstdint>

namespace mora {

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

// Serialize patches into a v2 sectioned binary blob.
std::vector<uint8_t> serialize_patch_table(const ResolvedPatchSet& patches,
                                            StringPool& pool);

// Same, but embed an esp_digest into the file header so the runtime can
// detect when the loaded plugin set has changed since compilation.
std::vector<uint8_t> serialize_patch_table(const ResolvedPatchSet& patches,
                                            StringPool& pool,
                                            const std::array<uint8_t, 32>& esp_digest);

// Serialize pre-built PatchEntry array directly (fast path for PatchBuffer).
// Entries must already be sorted and deduped. No string table support (all
// values are FormID/Int/Float).
std::vector<uint8_t> serialize_patch_table(const std::vector<PatchEntry>& entries);

// Same, with esp_digest.
std::vector<uint8_t> serialize_patch_table(const std::vector<PatchEntry>& entries,
                                            const std::array<uint8_t, 32>& esp_digest);

// Fourth overload: accept pre-built arrangements section bytes and an optional digest.
std::vector<uint8_t> serialize_patch_table(
    const std::vector<PatchEntry>& entries,
    const std::array<uint8_t, 32>& esp_digest,
    const std::vector<uint8_t>& arrangements_section);

// Fifth overload: also emit a DagBytecode section when non-empty.
std::vector<uint8_t> serialize_patch_table(
    const std::vector<PatchEntry>& entries,
    const std::array<uint8_t, 32>& esp_digest,
    const std::vector<uint8_t>& arrangements_section,
    const std::vector<uint8_t>& dag_bytecode);

} // namespace mora
