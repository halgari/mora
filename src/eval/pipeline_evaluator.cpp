#include "mora/eval/pipeline_evaluator.h"
#include "mora/eval/pipeline.h"
#include "mora/eval/operators.h"
#include "mora/eval/patch_buffer.h"
#include <utility>

namespace mora {

// ---------------------------------------------------------------------------
// ColumnarFactStore
// ---------------------------------------------------------------------------

ColumnarFactStore::ColumnarFactStore(ChunkPool& pool) : pool_(pool) {}

ColumnarRelation& ColumnarFactStore::get_or_create(StringId name,
                                                    const std::vector<ColType>& types) {
    auto it = relations_.find(name.index);
    if (it != relations_.end()) return it->second;

    auto [inserted, ok] = relations_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(name.index),
        std::forward_as_tuple(types.size(), types, pool_));
    return inserted->second;
}

ColumnarRelation* ColumnarFactStore::get(StringId name) {
    auto it = relations_.find(name.index);
    return it != relations_.end() ? &it->second : nullptr;
}

const ColumnarRelation* ColumnarFactStore::get(StringId name) const {
    auto it = relations_.find(name.index);
    return it != relations_.end() ? &it->second : nullptr;
}

void ColumnarFactStore::build_all_indexes() {
    for (auto& [_, rel] : relations_) {
        for (size_t c = 0; c < rel.arity(); c++) {
            if (rel.col_types()[c] == ColType::U32) {
                rel.build_index(c);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Distribution type descriptor
// ---------------------------------------------------------------------------

struct DistTypeInfo {
    const char* name;
    FieldId     field;
    FieldOp     op;
};

static constexpr DistTypeInfo kSpidDistTypes[] = {
    {"keyword", FieldId::Keywords, FieldOp::Add},
    {"spell",   FieldId::Spells,   FieldOp::Add},
    {"perk",    FieldId::Perks,    FieldOp::Add},
    {"item",    FieldId::Items,    FieldOp::Add},
    {"faction", FieldId::Factions, FieldOp::Add},
};

static constexpr DistTypeInfo kKidDistTypes[] = {
    {"keyword", FieldId::Keywords, FieldOp::Add},
};

// ---------------------------------------------------------------------------
// Helper: build rule_id -> target map from a dist relation for a given type
// ---------------------------------------------------------------------------

static void build_rule_target_map(
    const ColumnarRelation& dist_rel,
    uint32_t dist_type_val,
    std::unordered_map<uint32_t, uint32_t>& rule_to_target)
{
    rule_to_target.clear();
    scan_filtered(dist_rel, 1, dist_type_val, [&](const DataChunk& dc) {
        for (size_t i = 0; i < dc.sel.count; i++) {
            auto row = dc.sel.indices[i];
            uint32_t const rule_id = dc.u32(0)->data[row];
            uint32_t const target  = dc.u32(2)->data[row];
            rule_to_target[rule_id] = target;
        }
    });
}

// ---------------------------------------------------------------------------
// PatchBuffer-based operator pipelines
// ---------------------------------------------------------------------------

static void emit_keyword_filtered_buf(
    const std::unordered_map<uint32_t, uint32_t>& rule_to_target,
    const ColumnarRelation& kw_filter_rel,
    const ColumnarRelation* has_keyword_rel,
    const ColumnarRelation* npc_rel,
    FieldId field,
    FieldOp op,
    ChunkPool& pool,
    PatchBuffer& patch_buf)
{
    if (!has_keyword_rel || !npc_rel) return;

    // Pipeline: scan(kw_filter) -> hash_probe(has_keyword) -> semi_join(npc) -> emit
    scan(kw_filter_rel, [&](const DataChunk& filter_chunk) {
        // filter_chunk cols: (RuleID:0, KeywordFormID:1)
        hash_probe(*has_keyword_rel, /*probe_col*/1, /*build_key_col*/1,
                   filter_chunk, pool, [&](DataChunk& joined) {
            // joined cols: (RuleID:0, KeywordFormID:1, NPC_FormID:2, KW_FormID:3)
            semi_join(*npc_rel, /*key_col*/0, /*input_col*/2,
                      joined, [&](DataChunk& verified) {
                for (size_t i = 0; i < verified.sel.count; i++) {
                    auto row = verified.sel.indices[i];
                    uint32_t const rule_id = verified.u32(0)->data[row];
                    uint32_t const npc_fid = verified.u32(2)->data[row];
                    auto it = rule_to_target.find(rule_id);
                    if (it == rule_to_target.end()) continue;
                    patch_buf.emit(npc_fid,
                                   static_cast<uint8_t>(field),
                                   static_cast<uint8_t>(op),
                                   static_cast<uint8_t>(PatchValueType::FormID),
                                   it->second);
                }
            });
        });
    });
}

static void emit_form_filtered_buf(
    const std::unordered_map<uint32_t, uint32_t>& rule_to_target,
    const ColumnarRelation& form_filter_rel,
    const ColumnarRelation* race_of_rel,
    const ColumnarRelation* npc_rel,
    FieldId field,
    FieldOp op,
    ChunkPool& pool,
    PatchBuffer& patch_buf)
{
    if (!race_of_rel || !npc_rel) return;

    // Pipeline: scan(form_filter) -> hash_probe(race_of) -> semi_join(npc) -> emit
    scan(form_filter_rel, [&](const DataChunk& filter_chunk) {
        // filter_chunk cols: (RuleID:0, FormID:1)
        hash_probe(*race_of_rel, /*probe_col*/1, /*build_key_col*/1,
                   filter_chunk, pool, [&](DataChunk& joined) {
            // joined cols: (RuleID:0, FormID:1, NPC_FormID:2, Race_FormID:3)
            semi_join(*npc_rel, /*key_col*/0, /*input_col*/2,
                      joined, [&](DataChunk& verified) {
                for (size_t i = 0; i < verified.sel.count; i++) {
                    auto row = verified.sel.indices[i];
                    uint32_t const rule_id = verified.u32(0)->data[row];
                    uint32_t const npc_fid = verified.u32(2)->data[row];
                    auto it = rule_to_target.find(rule_id);
                    if (it == rule_to_target.end()) continue;
                    patch_buf.emit(npc_fid,
                                   static_cast<uint8_t>(field),
                                   static_cast<uint8_t>(op),
                                   static_cast<uint8_t>(PatchValueType::FormID),
                                   it->second);
                }
            });
        });
    });
}

static void emit_no_filter_buf(
    const std::unordered_map<uint32_t, uint32_t>& rule_to_target,
    const ColumnarRelation& no_filter_rel,
    const ColumnarRelation* npc_rel,
    FieldId field,
    FieldOp op,
    PatchBuffer& patch_buf)
{
    if (!npc_rel) return;

    scan(no_filter_rel, [&](const DataChunk& dc) {
        for (size_t i = 0; i < dc.sel.count; i++) {
            auto row = dc.sel.indices[i];
            uint32_t const rule_id = dc.u32(0)->data[row];

            auto it = rule_to_target.find(rule_id);
            if (it == rule_to_target.end()) continue;
            uint32_t target = it->second;

            // Distribute to ALL NPCs.
            scan(*npc_rel, [&](const DataChunk& npc_dc) {
                for (size_t ni = 0; ni < npc_dc.sel.count; ni++) {
                    uint32_t const npc_fid = npc_dc.u32(0)->data[npc_dc.sel.indices[ni]];
                    patch_buf.emit(npc_fid,
                                   static_cast<uint8_t>(field),
                                   static_cast<uint8_t>(op),
                                   static_cast<uint8_t>(PatchValueType::FormID),
                                   target);
                }
            });
        }
    });
}

// ---------------------------------------------------------------------------
// Legacy PatchSet-based helpers (kept for backward compatibility)
// ---------------------------------------------------------------------------

static void emit_keyword_filtered(
    const std::unordered_map<uint32_t, uint32_t>& rule_to_target,
    const ColumnarRelation& kw_filter_rel,
    const ColumnarRelation* has_keyword_rel,
    const ColumnarRelation* npc_rel,
    FieldId field,
    FieldOp op,
    StringId anon_mod,
    PatchSet& patches)
{
    if (!has_keyword_rel || !npc_rel) return;

    scan(kw_filter_rel, [&](const DataChunk& dc) {
        for (size_t i = 0; i < dc.sel.count; i++) {
            auto row = dc.sel.indices[i];
            uint32_t const rule_id = dc.u32(0)->data[row];
            uint32_t const kw_fid  = dc.u32(1)->data[row];

            auto it = rule_to_target.find(rule_id);
            if (it == rule_to_target.end()) continue;
            uint32_t const target = it->second;

            const auto& refs = has_keyword_rel->lookup(1, kw_fid);
            for (const auto& ref : refs) {
                const auto* npc_col = has_keyword_rel->u32_chunk(0, ref.chunk_idx);
                uint32_t const npc_fid = npc_col->data[ref.row_idx];

                const auto& npc_refs = npc_rel->lookup(0, npc_fid);
                if (npc_refs.empty()) continue;

                patches.add_patch(npc_fid, field, op,
                                  Value::make_formid(target), anon_mod, 0);
            }
        }
    });
}

static void emit_form_filtered(
    const std::unordered_map<uint32_t, uint32_t>& rule_to_target,
    const ColumnarRelation& form_filter_rel,
    const ColumnarRelation* race_of_rel,
    const ColumnarRelation* npc_rel,
    FieldId field,
    FieldOp op,
    StringId anon_mod,
    PatchSet& patches)
{
    if (!race_of_rel || !npc_rel) return;

    scan(form_filter_rel, [&](const DataChunk& dc) {
        for (size_t i = 0; i < dc.sel.count; i++) {
            auto row = dc.sel.indices[i];
            uint32_t const rule_id = dc.u32(0)->data[row];
            uint32_t const form_id = dc.u32(1)->data[row];

            auto it = rule_to_target.find(rule_id);
            if (it == rule_to_target.end()) continue;
            uint32_t const target = it->second;

            const auto& refs = race_of_rel->lookup(1, form_id);
            for (const auto& ref : refs) {
                const auto* npc_col = race_of_rel->u32_chunk(0, ref.chunk_idx);
                uint32_t const npc_fid = npc_col->data[ref.row_idx];

                const auto& npc_refs = npc_rel->lookup(0, npc_fid);
                if (npc_refs.empty()) continue;

                patches.add_patch(npc_fid, field, op,
                                  Value::make_formid(target), anon_mod, 0);
            }
        }
    });
}

static void emit_no_filter(
    const std::unordered_map<uint32_t, uint32_t>& rule_to_target,
    const ColumnarRelation& no_filter_rel,
    const ColumnarRelation* npc_rel,
    FieldId field,
    FieldOp op,
    StringId anon_mod,
    PatchSet& patches)
{
    if (!npc_rel) return;

    scan(no_filter_rel, [&](const DataChunk& dc) {
        for (size_t i = 0; i < dc.sel.count; i++) {
            auto row = dc.sel.indices[i];
            uint32_t const rule_id = dc.u32(0)->data[row];

            auto it = rule_to_target.find(rule_id);
            if (it == rule_to_target.end()) continue;
            uint32_t target = it->second;

            scan(*npc_rel, [&](const DataChunk& npc_dc) {
                for (size_t ni = 0; ni < npc_dc.sel.count; ni++) {
                    uint32_t const npc_fid = npc_dc.u32(0)->data[npc_dc.sel.indices[ni]];
                    patches.add_patch(npc_fid, field, op,
                                      Value::make_formid(target), anon_mod, 0);
                }
            });
        }
    });
}

// ---------------------------------------------------------------------------
// evaluate_distributions_columnar (PatchSet version - legacy)
// ---------------------------------------------------------------------------

void evaluate_distributions_columnar(
    const ColumnarFactStore& store,
    StringPool& pool,
    PatchSet& patches)
{
    auto npc_sid            = pool.intern("npc");
    auto has_keyword_sid    = pool.intern("has_keyword");
    auto race_of_sid        = pool.intern("race_of");
    auto spid_dist_sid      = pool.intern("spid_dist");
    auto spid_kw_filter_sid = pool.intern("spid_kw_filter");
    auto spid_form_filter_sid = pool.intern("spid_form_filter");
    auto spid_no_filter_sid = pool.intern("spid_no_filter");
    auto kid_dist_sid       = pool.intern("kid_dist");
    auto kid_kw_filter_sid  = pool.intern("kid_kw_filter");
    auto kid_no_filter_sid  = pool.intern("kid_no_filter");

    const auto* npc_rel         = store.get(npc_sid);
    const auto* has_keyword_rel = store.get(has_keyword_sid);
    const auto* race_of_rel     = store.get(race_of_sid);

    auto anon_mod = pool.intern("anonymous");

    std::unordered_map<uint32_t, uint32_t> rule_to_target;

    // SPID distributions
    const auto* spid_dist_rel      = store.get(spid_dist_sid);
    const auto* spid_kw_filter_rel = store.get(spid_kw_filter_sid);
    const auto* spid_form_filter_rel = store.get(spid_form_filter_sid);
    const auto* spid_no_filter_rel = store.get(spid_no_filter_sid);

    if (spid_dist_rel) {
        for (const auto& dt : kSpidDistTypes) {
            auto dist_type_sid = pool.intern(dt.name);
            build_rule_target_map(*spid_dist_rel, dist_type_sid.index, rule_to_target);
            if (rule_to_target.empty()) continue;

            if (spid_kw_filter_rel) {
                emit_keyword_filtered(rule_to_target, *spid_kw_filter_rel,
                                      has_keyword_rel, npc_rel,
                                      dt.field, dt.op, anon_mod, patches);
            }
            if (spid_form_filter_rel) {
                emit_form_filtered(rule_to_target, *spid_form_filter_rel,
                                   race_of_rel, npc_rel,
                                   dt.field, dt.op, anon_mod, patches);
            }
            if (spid_no_filter_rel) {
                emit_no_filter(rule_to_target, *spid_no_filter_rel, npc_rel,
                               dt.field, dt.op, anon_mod, patches);
            }
        }
    }

    // KID distributions
    const auto* kid_dist_rel      = store.get(kid_dist_sid);
    const auto* kid_kw_filter_rel = store.get(kid_kw_filter_sid);
    const auto* kid_no_filter_rel = store.get(kid_no_filter_sid);

    if (kid_dist_rel) {
        for (const auto& dt : kKidDistTypes) {
            auto dist_type_sid = pool.intern(dt.name);
            build_rule_target_map(*kid_dist_rel, dist_type_sid.index, rule_to_target);
            if (rule_to_target.empty()) continue;

            if (kid_kw_filter_rel) {
                emit_keyword_filtered(rule_to_target, *kid_kw_filter_rel,
                                      has_keyword_rel, npc_rel,
                                      dt.field, dt.op, anon_mod, patches);
            }
            if (kid_no_filter_rel) {
                emit_no_filter(rule_to_target, *kid_no_filter_rel, npc_rel,
                               dt.field, dt.op, anon_mod, patches);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// evaluate_distributions_columnar (PatchBuffer version - fast path)
// ---------------------------------------------------------------------------

void evaluate_distributions_columnar(
    const ColumnarFactStore& store,
    StringPool& pool,
    PatchBuffer& patch_buf)
{
    auto npc_sid            = pool.intern("npc");
    auto has_keyword_sid    = pool.intern("has_keyword");
    auto race_of_sid        = pool.intern("race_of");
    auto spid_dist_sid      = pool.intern("spid_dist");
    auto spid_kw_filter_sid = pool.intern("spid_kw_filter");
    auto spid_form_filter_sid = pool.intern("spid_form_filter");
    auto spid_no_filter_sid = pool.intern("spid_no_filter");
    auto kid_dist_sid       = pool.intern("kid_dist");
    auto kid_kw_filter_sid  = pool.intern("kid_kw_filter");
    auto kid_no_filter_sid  = pool.intern("kid_no_filter");

    const auto* npc_rel         = store.get(npc_sid);
    const auto* has_keyword_rel = store.get(has_keyword_sid);
    const auto* race_of_rel     = store.get(race_of_sid);

    // We need a mutable pool for hash_probe output chunks.
    // The store's pool is available via const_cast since ChunkPool is a shared resource.
    // Better: the store exposes pool() but only on non-const. We cast away const for the
    // pool since acquiring/releasing chunks is logically const w.r.t. the store's data.
    ChunkPool& chunk_pool = const_cast<ColumnarFactStore&>(store).pool();

    std::unordered_map<uint32_t, uint32_t> rule_to_target;

    // SPID distributions
    const auto* spid_dist_rel      = store.get(spid_dist_sid);
    const auto* spid_kw_filter_rel = store.get(spid_kw_filter_sid);
    const auto* spid_form_filter_rel = store.get(spid_form_filter_sid);
    const auto* spid_no_filter_rel = store.get(spid_no_filter_sid);

    if (spid_dist_rel) {
        for (const auto& dt : kSpidDistTypes) {
            auto dist_type_sid = pool.intern(dt.name);
            build_rule_target_map(*spid_dist_rel, dist_type_sid.index, rule_to_target);
            if (rule_to_target.empty()) continue;

            if (spid_kw_filter_rel) {
                emit_keyword_filtered_buf(rule_to_target, *spid_kw_filter_rel,
                                          has_keyword_rel, npc_rel,
                                          dt.field, dt.op, chunk_pool, patch_buf);
            }
            if (spid_form_filter_rel) {
                emit_form_filtered_buf(rule_to_target, *spid_form_filter_rel,
                                       race_of_rel, npc_rel,
                                       dt.field, dt.op, chunk_pool, patch_buf);
            }
            if (spid_no_filter_rel) {
                emit_no_filter_buf(rule_to_target, *spid_no_filter_rel, npc_rel,
                                   dt.field, dt.op, patch_buf);
            }
        }
    }

    // KID distributions
    const auto* kid_dist_rel      = store.get(kid_dist_sid);
    const auto* kid_kw_filter_rel = store.get(kid_kw_filter_sid);
    const auto* kid_no_filter_rel = store.get(kid_no_filter_sid);

    if (kid_dist_rel) {
        for (const auto& dt : kKidDistTypes) {
            auto dist_type_sid = pool.intern(dt.name);
            build_rule_target_map(*kid_dist_rel, dist_type_sid.index, rule_to_target);
            if (rule_to_target.empty()) continue;

            if (kid_kw_filter_rel) {
                emit_keyword_filtered_buf(rule_to_target, *kid_kw_filter_rel,
                                          has_keyword_rel, npc_rel,
                                          dt.field, dt.op, chunk_pool, patch_buf);
            }
            if (kid_no_filter_rel) {
                emit_no_filter_buf(rule_to_target, *kid_no_filter_rel, npc_rel,
                                   dt.field, dt.op, patch_buf);
            }
        }
    }
}

} // namespace mora
