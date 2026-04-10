#include "mora/emit/rt_writer.h"
#include <cstddef>

namespace mora {

RtWriter::RtWriter(StringPool& pool)
    : pool_(pool)
    , id_current_location_(pool.intern("current_location"))
    , id_current_cell_(pool.intern("current_cell"))
    , id_equipped_(pool.intern("equipped"))
    , id_quest_stage_(pool.intern("quest_stage"))
    , id_current_level_(pool.intern("current_level"))
    , id_is_alive_(pool.intern("is_alive"))
{}

// ---------------------------------------------------------------------------
// Little-endian primitive writers
// ---------------------------------------------------------------------------

void RtWriter::write_u8(std::ostream& out, uint8_t v) {
    out.write(reinterpret_cast<const char*>(&v), 1);
}

void RtWriter::write_u16(std::ostream& out, uint16_t v) {
    uint8_t buf[2];
    buf[0] = static_cast<uint8_t>(v);
    buf[1] = static_cast<uint8_t>(v >> 8);
    out.write(reinterpret_cast<const char*>(buf), 2);
}

void RtWriter::write_u32(std::ostream& out, uint32_t v) {
    uint8_t buf[4];
    buf[0] = static_cast<uint8_t>(v);
    buf[1] = static_cast<uint8_t>(v >> 8);
    buf[2] = static_cast<uint8_t>(v >> 16);
    buf[3] = static_cast<uint8_t>(v >> 24);
    out.write(reinterpret_cast<const char*>(buf), 4);
}

// ---------------------------------------------------------------------------
// Trigger inference
// Inspect body FactPatterns for instance facts and map to a TriggerKind.
// Priority order: cell/location -> OnCellChange, equipped -> OnEquip,
// quest_stage -> OnQuestUpdate, current_level/is_alive -> OnNpcLoad,
// default -> OnDataLoaded.
// ---------------------------------------------------------------------------

TriggerKind RtWriter::infer_trigger(const Rule& rule) const {
    bool has_cell_location = false;
    bool has_equipped      = false;
    bool has_quest_stage   = false;
    bool has_npc_fact      = false;

    for (const Clause& clause : rule.body) {
        const auto* fp = std::get_if<FactPattern>(&clause.data);
        if (!fp) continue;

        StringId n = fp->name;
        if (n == id_current_location_ || n == id_current_cell_) {
            has_cell_location = true;
        } else if (n == id_equipped_) {
            has_equipped = true;
        } else if (n == id_quest_stage_) {
            has_quest_stage = true;
        } else if (n == id_current_level_ || n == id_is_alive_) {
            has_npc_fact = true;
        }
    }

    if (has_cell_location) return TriggerKind::OnCellChange;
    if (has_equipped)      return TriggerKind::OnEquip;
    if (has_quest_stage)   return TriggerKind::OnQuestUpdate;
    if (has_npc_fact)      return TriggerKind::OnNpcLoad;
    return TriggerKind::OnDataLoaded;
}

// ---------------------------------------------------------------------------
// Top-level write
//
// Binary layout:
//   Magic:       "MORT" (4 bytes)
//   Version:     u16 = 1
//   Rule count:  u32
//   Per rule:
//     name:          u16 len + utf8 bytes
//     trigger:       u8 (TriggerKind)
//     clause_count:  u16
//     action_count:  u16
// ---------------------------------------------------------------------------

void RtWriter::write(std::ostream& out, const std::vector<const Rule*>& rules) {
    // Header
    out.write("MORT", 4);
    write_u16(out, 1);  // version
    write_u32(out, static_cast<uint32_t>(rules.size()));

    for (const Rule* rule : rules) {
        // name: u16 length-prefixed utf8
        std::string_view name = pool_.get(rule->name);
        write_u16(out, static_cast<uint16_t>(name.size()));
        out.write(name.data(), static_cast<std::streamsize>(name.size()));

        // trigger
        TriggerKind trigger = infer_trigger(*rule);
        write_u8(out, static_cast<uint8_t>(trigger));

        // clause_count: body entries that are FactPatterns or GuardClauses
        uint16_t clause_count = static_cast<uint16_t>(rule->body.size());
        write_u16(out, clause_count);

        // action_count: direct effects + conditional effects
        uint16_t action_count = static_cast<uint16_t>(
            rule->effects.size() + rule->conditional_effects.size());
        write_u16(out, action_count);
    }
}

} // namespace mora
