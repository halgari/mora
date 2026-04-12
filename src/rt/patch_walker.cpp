#ifdef _WIN32
#include "mora/rt/form_ops.h"
#include "mora/rt/bst_hashmap.h"
#include "mora/data/form_constants.h"
#include <cstdint>
#include <cstring>

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
        case 1: { // Name
            if (e.value_type != 3) break; // must be string
            uint32_t offset = static_cast<uint32_t>(e.value);
            uint16_t len = 0;
            std::memcpy(&len, string_table + offset, 2);
            char buf[512];
            uint16_t copy_len = len < 511 ? len : 511;
            std::memcpy(buf, string_table + offset + 2, copy_len);
            buf[copy_len] = '\0';
            mora_rt_write_name(skyrim_base, form, buf, bs_ctor8, bs_release8);
            break;
        }
        case 2: { // Damage
            uint64_t off = get_field_offset(ft, 2);
            if (off) { uint16_t v = static_cast<uint16_t>(e.value); std::memcpy((char*)form + off, &v, 2); }
            break;
        }
        case 3: { // ArmorRating
            uint64_t off = get_field_offset(ft, 3);
            if (off) { uint32_t v = static_cast<uint32_t>(e.value); std::memcpy((char*)form + off, &v, 4); }
            break;
        }
        case 4: { // GoldValue
            uint64_t off = get_field_offset(ft, 4);
            if (off) { int32_t v = static_cast<int32_t>(e.value); std::memcpy((char*)form + off, &v, 4); }
            break;
        }
        case 5: { // Weight
            uint64_t off = get_field_offset(ft, 5);
            if (off) {
                double d;
                std::memcpy(&d, &e.value, 8);
                float v = static_cast<float>(d);
                std::memcpy((char*)form + off, &v, 4);
            }
            break;
        }
        // Weapon scalar fields
        case 20: case 21: case 22: case 23: case 24: { // Speed/Reach/Stagger/RangeMin/RangeMax (float)
            uint64_t off = get_field_offset(ft, e.field_id);
            if (off) {
                double d;
                std::memcpy(&d, &e.value, 8);
                float v = static_cast<float>(d);
                std::memcpy((char*)form + off, &v, 4);
            }
            break;
        }
        case 25: { // CritDamage (uint16_t)
            uint64_t off = get_field_offset(ft, 25);
            if (off) { uint16_t v = static_cast<uint16_t>(e.value); std::memcpy((char*)form + off, &v, 2); }
            break;
        }
        case 30: { // Health (uint32_t)
            uint64_t off = get_field_offset(ft, 30);
            if (off) { uint32_t v = static_cast<uint32_t>(e.value); std::memcpy((char*)form + off, &v, 4); }
            break;
        }
        // NPC level fields (uint16_t)
        case 11: case 41: case 42: {
            uint64_t off = get_field_offset(ft, e.field_id);
            if (off) { uint16_t v = static_cast<uint16_t>(e.value); std::memcpy((char*)form + off, &v, 2); }
            break;
        }
        // Form reference fields — write a form pointer
        case 50: case 51: case 52: case 53: case 54: case 55: {
            if (e.value_type != 0) break; // must be FormID
            uint64_t off = get_field_offset(ft, e.field_id);
            if (!off) break;
            void* ref_form = bst_hashmap_lookup(map, static_cast<uint32_t>(e.value));
            if (ref_form) {
                std::memcpy((char*)form + off, &ref_form, sizeof(void*));
            }
            break;
        }
        case 6: { // Keywords
            if (e.value_type != 0) break; // must be FormID
            void* kw_form = bst_hashmap_lookup(map, static_cast<uint32_t>(e.value));
            if (!kw_form) break;
            if (e.op == 1) // Add
                mora_rt_add_keyword(skyrim_base, form, kw_form, mm_singleton, mm_alloc, mm_dealloc);
            else if (e.op == 2) // Remove
                mora_rt_remove_keyword(skyrim_base, form, kw_form, mm_singleton, mm_alloc, mm_dealloc);
            break;
        }
        case 9: { // Spells
            if (e.value_type != 0) break;
            void* spell_form = bst_hashmap_lookup(map, static_cast<uint32_t>(e.value));
            if (spell_form)
                mora_rt_add_spell(skyrim_base, form, spell_form, mm_singleton, mm_alloc, mm_dealloc);
            break;
        }
        case 8: { // Perks
            if (e.value_type != 0) break;
            void* perk_form = bst_hashmap_lookup(map, static_cast<uint32_t>(e.value));
            if (perk_form)
                mora_rt_add_perk(skyrim_base, form, perk_form, mm_singleton, mm_alloc, mm_dealloc);
            break;
        }
        case 7: { // Factions
            if (e.value_type != 0) break;
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
    if (hdr->magic != 0x4D4F5241 || hdr->version != 2) return;

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
