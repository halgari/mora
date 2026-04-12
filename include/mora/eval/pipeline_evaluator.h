#pragma once
#include "mora/data/columnar_relation.h"
#include "mora/data/chunk_pool.h"
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include <unordered_map>

namespace mora {

// Columnar fact store: holds ColumnarRelations by name.
class ColumnarFactStore {
public:
    explicit ColumnarFactStore(ChunkPool& pool);

    ColumnarRelation& get_or_create(StringId name, std::vector<ColType> types);
    ColumnarRelation* get(StringId name);
    const ColumnarRelation* get(StringId name) const;

    void build_all_indexes();

    ChunkPool& pool() { return pool_; }

private:
    ChunkPool& pool_;
    std::unordered_map<uint32_t, ColumnarRelation> relations_;
};

// Evaluate SPID/KID distribution rules using columnar pipeline.
// This is the fast path for INI distributions — it bypasses the
// Datalog evaluator entirely.
//
// Expected relations in the store:
//   npc(FormID: U32)
//   has_keyword(NPC_FormID: U32, Keyword_FormID: U32)
//   race_of(NPC_FormID: U32, Race_FormID: U32)
//   spid_dist(RuleID: U32, DistType_StringId: U32, Target_FormID: U32)
//   spid_kw_filter(RuleID: U32, Keyword_FormID: U32)
//   spid_form_filter(RuleID: U32, FormID: U32)
//   spid_no_filter(RuleID: U32)
//   kid_dist(RuleID: U32, DistType_StringId: U32, Target_FormID: U32)
//   kid_kw_filter(RuleID: U32, Keyword_FormID: U32)
//   kid_no_filter(RuleID: U32)
void evaluate_distributions_columnar(
    const ColumnarFactStore& store,
    StringPool& pool,
    PatchSet& patches);

} // namespace mora
