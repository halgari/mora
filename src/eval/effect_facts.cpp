#include "mora/eval/effect_facts.h"

#include "mora/data/value.h"
#include "mora/emit/patch_table.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_buffer.h"
#include "mora/eval/patch_set.h"
#include "mora/model/field_names.h"

#include <bit>
#include <unordered_set>

namespace mora {

void populate_effect_facts(const PatchBuffer& buf,
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

    for (const auto& e : buf.entries()) {
        // PatchValueType::StringIndex is skipped: in the production path
        // (main.cpp evaluate_mora_rules → build_patch_entries_and_string_table)
        // the value uint64 is a BYTE OFFSET into the mora_patches.bin
        // StringTable blob, NOT a StringPool StringId. Decoding it here as
        // a StringId would materialise garbage strings. Properly supporting
        // string-valued effect facts requires either (a) driving this
        // bridge from the upstream PatchSet where Value::Kind::String
        // still carries a StringPool index, or (b) passing the string
        // table blob alongside the buffer and re-interning at decode
        // time. Both are later-plan work; today the pipeline evaluator
        // only emits FormID-typed entries so the skip is a no-op in
        // practice.
        if (static_cast<PatchValueType>(e.value_type) == PatchValueType::StringIndex) {
            continue;
        }

        StringId rel_id;
        switch (static_cast<FieldOp>(e.op)) {
            case FieldOp::Set:      rel_id = id_set;      break;
            case FieldOp::Add:      rel_id = id_add;      break;
            case FieldOp::Remove:   rel_id = id_remove;   break;
            case FieldOp::Multiply: rel_id = id_multiply; break;
        }
        ensure_configured(rel_id);

        Value val;
        switch (static_cast<PatchValueType>(e.value_type)) {
            case PatchValueType::FormID:
                val = Value::make_formid(static_cast<uint32_t>(e.value));
                break;
            case PatchValueType::Int:
                val = Value::make_int(static_cast<int64_t>(e.value));
                break;
            case PatchValueType::Float:
                val = Value::make_float(std::bit_cast<double>(e.value));
                break;
            case PatchValueType::StringIndex:
                // Unreachable — skipped at top of loop. Present to make
                // the switch exhaustive.
                continue;
        }

        auto field_kw = Value::make_keyword(
            pool.intern(field_id_name(static_cast<FieldId>(e.field_id))));

        db.add_fact(rel_id, Tuple{
            Value::make_formid(e.formid),
            field_kw,
            val,
        });
    }
}

} // namespace mora
