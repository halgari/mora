#ifdef _WIN32
#include "mora/rt/form_ops.h"
#include "mora/rt/bst_hashmap.h"
#include "mora/data/form_constants.h"
#include "mora/data/action_names.h"
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
    uint64_t map_offset;
    uint64_t mm_singleton_off;
    uint64_t mm_allocate_off;
    uint64_t mm_deallocate_off;
    uint64_t bs_ctor8_off;
    uint64_t bs_release8_off;
};

struct PatchEntry {
    uint32_t formid;
    uint8_t field_id;
    uint8_t op;
    uint8_t value_type;
    uint8_t pad;
    uint64_t value;
};

// apply_patch_entry dispatches to the appropriate RT function
static void apply_patch_entry(void* skyrim_base, void* form,
                               const PatchEntry& e, const uint8_t* string_table,
                               uint64_t mm_singleton, uint64_t mm_alloc,
                               uint64_t mm_dealloc, uint64_t bs_ctor8,
                               uint64_t bs_release8,
                               const mora::rt::BSTHashMapLayout* map) {
    using namespace mora::rt;
    using namespace mora;

    uint8_t ft = get_form_type(form);

    switch (e.field_id) {
        case fid(FieldId::Name): {
            if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::StringIndex)) break;
            uint32_t offset = static_cast<uint32_t>(e.value);
            uint16_t len = 0;
            std::memcpy(&len, string_table + offset, 2);
            char buf[mora::kPatchStringBufSize];
            uint16_t copy_len = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
            std::memcpy(buf, string_table + offset + 2, copy_len);
            buf[copy_len] = '\0';
            mora_rt_write_name(skyrim_base, form, buf, bs_ctor8, bs_release8);
            break;
        }
        // ── Scalar fields: uint16_t ──
        case fid(FieldId::Damage):
        case fid(FieldId::CritDamage):
        case fid(FieldId::Level):
        case fid(FieldId::CalcLevelMin):
        case fid(FieldId::CalcLevelMax):
        {
            uint64_t off = get_field_offset(ft, e.field_id);
            if (!off) break;
            if (e.op == fop(FieldOp::Multiply)) {
                uint16_t cur; std::memcpy(&cur, (char*)form + off, 2);
                double d; std::memcpy(&d, &e.value, 8);
                uint16_t v = static_cast<uint16_t>(cur * d);
                std::memcpy((char*)form + off, &v, 2);
            } else {
                uint16_t v = static_cast<uint16_t>(e.value);
                std::memcpy((char*)form + off, &v, 2);
            }
            break;
        }
        // ── Scalar fields: uint32_t ──
        case fid(FieldId::ArmorRating):
        case fid(FieldId::Health):
        {
            uint64_t off = get_field_offset(ft, e.field_id);
            if (!off) break;
            if (e.op == fop(FieldOp::Multiply)) {
                uint32_t cur; std::memcpy(&cur, (char*)form + off, 4);
                double d; std::memcpy(&d, &e.value, 8);
                uint32_t v = static_cast<uint32_t>(cur * d);
                std::memcpy((char*)form + off, &v, 4);
            } else {
                uint32_t v = static_cast<uint32_t>(e.value);
                std::memcpy((char*)form + off, &v, 4);
            }
            break;
        }
        // ── Scalar fields: int32_t ──
        case fid(FieldId::GoldValue):
        {
            uint64_t off = get_field_offset(ft, e.field_id);
            if (!off) break;
            if (e.op == fop(FieldOp::Multiply)) {
                int32_t cur; std::memcpy(&cur, (char*)form + off, 4);
                double d; std::memcpy(&d, &e.value, 8);
                int32_t v = static_cast<int32_t>(cur * d);
                std::memcpy((char*)form + off, &v, 4);
            } else {
                int32_t v = static_cast<int32_t>(e.value);
                std::memcpy((char*)form + off, &v, 4);
            }
            break;
        }
        // ── Scalar fields: float ──
        case fid(FieldId::Weight):
        case fid(FieldId::Speed):
        case fid(FieldId::Reach):
        case fid(FieldId::Stagger):
        case fid(FieldId::RangeMin):
        case fid(FieldId::RangeMax):
        {
            uint64_t off = get_field_offset(ft, e.field_id);
            if (!off) break;
            double d; std::memcpy(&d, &e.value, 8);
            if (e.op == fop(FieldOp::Multiply)) {
                float cur; std::memcpy(&cur, (char*)form + off, 4);
                float v = cur * static_cast<float>(d);
                std::memcpy((char*)form + off, &v, 4);
            } else {
                float v = static_cast<float>(d);
                std::memcpy((char*)form + off, &v, 4);
            }
            break;
        }
        // ── Form reference fields — write a form pointer ──
        case fid(FieldId::RaceForm):
        case fid(FieldId::ClassForm):
        case fid(FieldId::SkinForm):
        case fid(FieldId::OutfitForm):
        case fid(FieldId::EnchantmentForm):
        case fid(FieldId::VoiceTypeForm): {
            if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::FormID)) break;
            uint64_t off = get_field_offset(ft, e.field_id);
            if (!off) break;
            void* ref_form = bst_hashmap_lookup(map, static_cast<uint32_t>(e.value));
            if (ref_form) {
                std::memcpy((char*)form + off, &ref_form, sizeof(void*));
            }
            break;
        }
        // ── Form list operations ──
        case fid(FieldId::Keywords): {
            if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::FormID)) break;
            void* kw_form = bst_hashmap_lookup(map, static_cast<uint32_t>(e.value));
            if (!kw_form) break;
            if (e.op == fop(FieldOp::Add))
                mora_rt_add_keyword(skyrim_base, form, kw_form, mm_singleton, mm_alloc, mm_dealloc);
            else if (e.op == fop(FieldOp::Remove))
                mora_rt_remove_keyword(skyrim_base, form, kw_form, mm_singleton, mm_alloc, mm_dealloc);
            break;
        }
        case fid(FieldId::Spells): {
            if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::FormID)) break;
            void* spell_form = bst_hashmap_lookup(map, static_cast<uint32_t>(e.value));
            if (!spell_form) break;
            if (e.op == fop(FieldOp::Add))
                mora_rt_add_spell(skyrim_base, form, spell_form, mm_singleton, mm_alloc, mm_dealloc);
            else if (e.op == fop(FieldOp::Remove))
                mora_rt_remove_spell(skyrim_base, form, spell_form, mm_singleton, mm_alloc, mm_dealloc);
            break;
        }
        case fid(FieldId::Shouts): {
            if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::FormID)) break;
            void* shout_form = bst_hashmap_lookup(map, static_cast<uint32_t>(e.value));
            if (shout_form)
                mora_rt_add_shout(skyrim_base, form, shout_form, mm_singleton, mm_alloc, mm_dealloc);
            break;
        }
        case fid(FieldId::Perks): {
            if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::FormID)) break;
            void* perk_form = bst_hashmap_lookup(map, static_cast<uint32_t>(e.value));
            if (perk_form)
                mora_rt_add_perk(skyrim_base, form, perk_form, mm_singleton, mm_alloc, mm_dealloc);
            break;
        }
        case fid(FieldId::Factions): {
            if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::FormID)) break;
            void* fac_form = bst_hashmap_lookup(map, static_cast<uint32_t>(e.value));
            if (fac_form)
                mora_rt_add_faction(skyrim_base, form, fac_form, mm_singleton, mm_alloc, mm_dealloc);
            break;
        }
    }
}

// Main entry point -- called from plugin_entry.cpp at DataLoaded
extern "C" void apply_all_patches(void* skyrim_base) {
    if (mora_patch_data_size == 0) return;

    auto* hdr = reinterpret_cast<const PatchTableHeader*>(mora_patch_data);
    if (hdr->magic != mora::kPatchTableMagic || hdr->version != mora::kPatchTableVersion) return;

    const uint8_t* string_table = mora_patch_data + sizeof(PatchTableHeader);
    const auto* entries = reinterpret_cast<const PatchEntry*>(
        string_table + hdr->string_table_size);

    // Load form map pointer from skyrim_base + resolved offset
    auto* map = reinterpret_cast<const mora::rt::BSTHashMapLayout*>(
        *reinterpret_cast<void**>(reinterpret_cast<char*>(skyrim_base) + hdr->map_offset));

    uint64_t mm_singleton = hdr->mm_singleton_off;
    uint64_t mm_alloc     = hdr->mm_allocate_off;
    uint64_t mm_dealloc   = hdr->mm_deallocate_off;
    uint64_t bs_ctor8     = hdr->bs_ctor8_off;
    uint64_t bs_release8  = hdr->bs_release8_off;

    uint32_t current_fid = 0;
    void* current_form = nullptr;

    for (uint32_t i = 0; i < hdr->patch_count; i++) {
        const auto& e = entries[i];
        if (e.formid != current_fid) {
            current_fid = e.formid;
            current_form = bst_hashmap_lookup(map, current_fid);
        }
        if (!current_form) continue;
        apply_patch_entry(skyrim_base, current_form, e, string_table,
                          mm_singleton, mm_alloc, mm_dealloc,
                          bs_ctor8, bs_release8, map);
    }
}

#else
// Linux stub -- patch_walker is only meaningful when cross-compiled for Windows
namespace mora::rt { void patch_walker_stub() {} }
#endif
