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
// Entries must already be sorted and deduped. Callers that want string-valued
// patches must build the StringTable section via
// build_patch_entries_and_string_table() and pass it to the 5-arg overload
// below; this overload emits no StringTable section.
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

// Sixth overload: additionally emit a pre-built StringTable section when
// non-empty. `string_table_section` is the raw bytes as produced by
// build_patch_entries_and_string_table() (a sequence of [u16 len][data]
// records); entries with value_type=StringIndex must reference offsets
// into this blob.
std::vector<uint8_t> serialize_patch_table(
    const std::vector<PatchEntry>& entries,
    const std::array<uint8_t, 32>& esp_digest,
    const std::vector<uint8_t>& arrangements_section,
    const std::vector<uint8_t>& dag_bytecode,
    const std::vector<uint8_t>& string_table_section);

// Convert a ResolvedPatchSet into the PatchEntry array used by the fast-path
// serializer, and return the matching StringTable section bytes. Entries with
// String-valued `Value`s are emitted with value_type=StringIndex and `value`
// set to the byte offset into the returned table. Throws std::runtime_error
// if any patch carries an unsupported Value::Kind (catches regressions like
// silently dropping String-typed patches — cf. issue #4).
std::vector<uint8_t> build_patch_entries_and_string_table(
    const ResolvedPatchSet& patches,
    StringPool& pool,
    std::vector<PatchEntry>& out_entries);

} // namespace mora
