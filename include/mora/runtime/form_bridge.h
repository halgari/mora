#pragma once
#include "mora/eval/patch_set.h"
#include "mora/eval/fact_db.h"
#include "mora/data/value.h"
#include "mora/core/string_pool.h"

namespace mora {

class FormBridge {
public:
    static bool apply_patch(uint32_t formid, const FieldPatch& patch);
    static int apply_patches(uint32_t formid, const std::vector<FieldPatch>& patches);
    static void populate_facts_for_npc(uint32_t formid, FactDB& db, StringPool& pool);
};

} // namespace mora
