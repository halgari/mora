#ifdef _WIN32
#include "mora/rt/form_ops.h"
#include "mora/data/form_model.h"
#include "mora/data/action_names.h"
#include "mora/emit/patch_table.h"
#include <cstdint>
#include <cstring>

using mora::fid;
using mora::fop;
using mora::FieldId;
using mora::FieldOp;

// These are defined by the LLVM IR data module and resolved at link time.
extern "C" const uint8_t mora_patch_data[];
extern "C" const uint32_t mora_patch_data_size;

struct PatchTableHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t patch_count;
    uint32_t string_table_size;
};

struct PatchEntry {
    uint32_t formid;
    uint8_t field_id;
    uint8_t op;
    uint8_t value_type;
    uint8_t pad;
    uint64_t value;
};

// ── Template helpers for scalar field application ──────────────────────

template<typename T>
static void apply_scalar(void* form, uint64_t off, const PatchEntry& e) {
    if (!off) return;
    if (e.op == fop(FieldOp::Multiply)) {
        T cur; std::memcpy(&cur, (char*)form + off, sizeof(T));
        double d; std::memcpy(&d, &e.value, 8);
        T v = static_cast<T>(cur * d);
        std::memcpy((char*)form + off, &v, sizeof(T));
    } else {
        T v = static_cast<T>(e.value);
        std::memcpy((char*)form + off, &v, sizeof(T));
    }
}

static void apply_scalar_float(void* form, uint64_t off, const PatchEntry& e) {
    if (!off) return;
    double d; std::memcpy(&d, &e.value, 8);
    if (e.op == fop(FieldOp::Multiply)) {
        float cur; std::memcpy(&cur, (char*)form + off, 4);
        float v = cur * static_cast<float>(d);
        std::memcpy((char*)form + off, &v, 4);
    } else {
        float v = static_cast<float>(d);
        std::memcpy((char*)form + off, &v, 4);
    }
}

// apply_patch_entry dispatches to the appropriate RT function
static void apply_patch_entry(void* form, const PatchEntry& e,
                               const uint8_t* string_table) {
    using namespace mora::rt;
    using namespace mora;
    namespace m = mora::model;

    uint8_t ft = get_form_type(form);
    FieldId field = static_cast<FieldId>(e.field_id);

    // Try scalar fields from the model
    auto* fdef = m::find_field(field);
    if (fdef) {
        auto& comp = m::kComponents[fdef->component_idx];
        auto& member = comp.members[fdef->member_idx];
        uint64_t off = m::field_offset_for(ft, field);

        if (comp.kind == m::ComponentDef::Kind::String) {
            // Name write
            if (e.value_type != static_cast<uint8_t>(PatchValueType::StringIndex)) return;
            uint32_t str_off = static_cast<uint32_t>(e.value);
            uint16_t len = 0;
            std::memcpy(&len, string_table + str_off, 2);
            char buf[kPatchStringBufSize];
            uint16_t copy_len = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
            std::memcpy(buf, string_table + str_off + 2, copy_len);
            buf[copy_len] = '\0';
            mora_rt_write_name(form, buf);
            return;
        }

        if (comp.kind == m::ComponentDef::Kind::Scalar && off != 0) {
            if (member.value_type == m::ValueType::FormRef) {
                // Form reference field
                if (e.value_type != static_cast<uint8_t>(PatchValueType::FormID)) return;
                void* ref_form = mora_rt_lookup_form(static_cast<uint32_t>(e.value));
                if (ref_form) {
                    std::memcpy((char*)form + off, &ref_form, sizeof(void*));
                }
            } else if (member.value_type == m::ValueType::Float32) {
                apply_scalar_float(form, off, e);
            } else if (member.value_type == m::ValueType::UInt16 ||
                       member.value_type == m::ValueType::Int16) {
                apply_scalar<uint16_t>(form, off, e);
            } else if (member.value_type == m::ValueType::Int32) {
                apply_scalar<int32_t>(form, off, e);
            } else if (member.value_type == m::ValueType::UInt32) {
                apply_scalar<uint32_t>(form, off, e);
            }
            return;
        }
        // off == 0 means this form type doesn't have the component — skip
        if (comp.kind == m::ComponentDef::Kind::Scalar) return;
    }

    // Try form array fields from the model
    auto* fa = m::find_form_array(field);
    if (fa) {
        if (e.value_type != static_cast<uint8_t>(PatchValueType::FormID)) return;
        void* target_form = mora_rt_lookup_form(static_cast<uint32_t>(e.value));
        if (!target_form) return;

        // Dispatch to the appropriate RT function by matching function name
        if (e.op == fop(FieldOp::Add)) {
            if (field == FieldId::Keywords) mora_rt_add_keyword(form, target_form);
            else if (field == FieldId::Spells) mora_rt_add_spell(form, target_form);
            else if (field == FieldId::Perks) mora_rt_add_perk(form, target_form);
            else if (field == FieldId::Factions) mora_rt_add_faction(form, target_form);
            else if (field == FieldId::Shouts) mora_rt_add_shout(form, target_form);
        } else if (e.op == fop(FieldOp::Remove)) {
            if (field == FieldId::Keywords) mora_rt_remove_keyword(form, target_form);
            else if (field == FieldId::Spells) mora_rt_remove_spell(form, target_form);
        }
        return;
    }

    // Leveled list operations (special handling)
    if (field == FieldId::LeveledEntries) {
        if (e.op == fop(FieldOp::Add)) {
            uint32_t entry_fid = static_cast<uint32_t>(e.value);
            uint16_t level_val = static_cast<uint16_t>(e.value >> 32);
            uint16_t count_val = static_cast<uint16_t>(e.value >> 48);
            if (count_val == 0) count_val = 1;
            mora_rt_add_to_leveled_list(form, entry_fid, level_val, count_val);
        } else if (e.op == fop(FieldOp::Remove)) {
            mora_rt_remove_from_leveled_list(form, static_cast<uint32_t>(e.value));
        } else if (e.op == fop(FieldOp::Set)) {
            mora_rt_clear_leveled_list(form);
        }
        return;
    }

    if (field == FieldId::ChanceNone) {
        mora_rt_set_chance_none(form, static_cast<int8_t>(e.value));
        return;
    }
}

// Main entry point -- called from plugin_entry.cpp at DataLoaded
extern "C" void apply_all_patches() {
    if (mora_patch_data_size == 0) return;

    auto* hdr = reinterpret_cast<const PatchTableHeader*>(mora_patch_data);
    if (hdr->magic != mora::kPatchTableMagic || hdr->version != mora::kPatchTableVersion) return;

    const uint8_t* string_table = mora_patch_data + sizeof(PatchTableHeader);
    const auto* entries = reinterpret_cast<const PatchEntry*>(
        string_table + hdr->string_table_size);

    uint32_t current_fid = 0;
    void* current_form = nullptr;

    for (uint32_t i = 0; i < hdr->patch_count; i++) {
        const auto& e = entries[i];
        if (e.formid != current_fid) {
            current_fid = e.formid;
            current_form = mora_rt_lookup_form(current_fid);
        }
        if (!current_form) continue;
        apply_patch_entry(current_form, e, string_table);
    }
}

#else
// Linux stub -- patch_walker is only meaningful when cross-compiled for Windows
namespace mora::rt { void patch_walker_stub() {} }
#endif
