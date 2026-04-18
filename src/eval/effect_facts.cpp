#include "mora/eval/effect_facts.h"

#include "mora/data/value.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_set.h"
#include "mora/model/field_names.h"

#include <unordered_set>

namespace mora {

void populate_effect_facts(const ResolvedPatchSet& patches,
                            FactDB& db,
                            StringPool& pool) {
    auto id_set      = pool.intern("skyrim/set");
    auto id_add      = pool.intern("skyrim/add");
    auto id_remove   = pool.intern("skyrim/remove");
    auto id_multiply = pool.intern("skyrim/multiply");

    std::unordered_set<uint32_t> configured;
    auto ensure_configured = [&](StringId rel_id) {
        if (configured.insert(rel_id.index).second) {
            db.configure_relation(rel_id, /*arity*/ 3, /*indexed*/ {0});
        }
    };

    for (const auto& rp : patches.all_patches_sorted()) {
        for (const auto& fp : rp.fields) {
            StringId rel_id;
            switch (fp.op) {
                case FieldOp::Set:      rel_id = id_set;      break;
                case FieldOp::Add:      rel_id = id_add;      break;
                case FieldOp::Remove:   rel_id = id_remove;   break;
                case FieldOp::Multiply: rel_id = id_multiply; break;
            }
            ensure_configured(rel_id);

            auto field_kw = Value::make_keyword(
                pool.intern(field_id_name(fp.field)));

            db.add_fact(rel_id, Tuple{
                Value::make_formid(rp.target_formid),
                field_kw,
                fp.value,   // already typed — no decode needed
            });
        }
    }
}

} // namespace mora
