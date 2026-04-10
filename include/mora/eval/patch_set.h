#pragma once
#include <cstdint>
#include <map>
#include <vector>
#include "mora/eval/fact_db.h"
#include "mora/core/string_pool.h"

namespace mora {

// ---------------------------------------------------------------------------
// Field identifiers
// ---------------------------------------------------------------------------

enum class FieldId : uint16_t {
    Name         = 1,
    Damage       = 2,
    ArmorRating  = 3,
    GoldValue    = 4,
    Weight       = 5,
    Keywords     = 6,
    Factions     = 7,
    Perks        = 8,
    Spells       = 9,
    Items        = 10,
    Level        = 11,
    Race         = 12,
    EditorId     = 13,
};

// ---------------------------------------------------------------------------
// Patch operation
// ---------------------------------------------------------------------------

enum class FieldOp : uint8_t { Set = 0, Add = 1, Remove = 2 };

// ---------------------------------------------------------------------------
// Raw patch — one (formid, field) mutation from a single mod
// ---------------------------------------------------------------------------

struct FieldPatch {
    FieldId    field;
    FieldOp    op;
    Value      value;
    StringId   source_mod;
    uint32_t   priority;
};

// ---------------------------------------------------------------------------
// Resolved output — winner patch per (formid, field) slot
// ---------------------------------------------------------------------------

struct ResolvedPatch {
    uint32_t              target_formid;
    std::vector<FieldPatch> fields;
};

// ---------------------------------------------------------------------------
// Conflict report types
// ---------------------------------------------------------------------------

struct ConflictEntry {
    Value    value;
    StringId source_mod;
    uint32_t priority;
};

struct Conflict {
    uint32_t              target_formid;
    FieldId               field;
    std::vector<ConflictEntry> entries;
};

// ---------------------------------------------------------------------------
// ResolvedPatchSet — query interface returned by PatchSet::resolve()
// ---------------------------------------------------------------------------

class ResolvedPatchSet {
public:
    // Returns the resolved FieldPatch list for a given FormID (empty if none).
    const std::vector<FieldPatch>& get_patches_for(uint32_t formid) const;

    // All resolved patches sorted ascending by FormID.
    std::vector<ResolvedPatch> all_patches_sorted() const;

    // Conflict report: one entry per (formid, field) pair that had >1 Set op.
    const std::vector<Conflict>& get_conflicts() const;

    // Total number of resolved field patches across all FormIDs.
    size_t patch_count() const;

private:
    friend class PatchSet;

    // Ordered map: formid -> resolved field patches for that formid
    std::map<uint32_t, std::vector<FieldPatch>> patches_;
    std::vector<Conflict>                        conflicts_;

    static const std::vector<FieldPatch> empty_patches_;
};

// ---------------------------------------------------------------------------
// PatchSet — accumulates raw patches, resolves on demand
// ---------------------------------------------------------------------------

class PatchSet {
public:
    void add_patch(uint32_t  formid,
                   FieldId   field,
                   FieldOp   op,
                   Value     value,
                   StringId  source_mod,
                   uint32_t  priority);

    ResolvedPatchSet resolve() const;

private:
    struct RawPatch {
        uint32_t formid;
        FieldId  field;
        FieldOp  op;
        Value    value;
        StringId source_mod;
        uint32_t priority;
    };

    std::vector<RawPatch> raw_;
};

} // namespace mora
