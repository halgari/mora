# Data-Driven Patch Table

> **Status:** Historical design doc — superseded. The shipping patch file
> layout lives in `include/mora/emit/patch_file_v2.h` (64-byte header,
> 24-byte section directory entries, multiple typed sections); the
> single-table design described below was not adopted.

## Problem

Running 3.9M patches through LLVM codegen takes 4.5 minutes and produces a 105MB DLL. The generated code is just sequential function calls with constant arguments — LLVM's backend is doing O(N^2) work on what is fundamentally a data problem.

## Design

Replace LLVM code generation with a serialized binary patch table embedded in the DLL's .rdata section. A pre-compiled interpreter loop in `mora_rt.lib` walks the table at runtime.

## Binary Table Format

```
Header (16 bytes):
  uint32_t magic       = 0x4D4F5241  ("MORA")
  uint32_t version     = 2
  uint32_t patch_count
  uint32_t string_count

String Table:
  For each string: uint16_t length, char[length] (UTF-8, not null-terminated)

Patch Entries (sorted by formid, 16 bytes each):
  uint32_t formid
  uint8_t  field_id    (FieldId enum)
  uint8_t  op          (FieldOp enum: Set=0, Add=1, Remove=2)
  uint8_t  value_type  (0=FormID, 1=Int, 2=Float, 3=StringIndex)
  uint8_t  pad
  uint64_t value       (formid, int64, double bits, or string table index)
```

16 bytes per entry. 3.9M entries = 62.4 MB. Sorted by formid so the runtime walker does one hashmap lookup per unique form.

## Compile Pipeline

```
Evaluation → ResolvedPatchSet
    ↓
serialize_patch_table(patches, pool) → std::vector<uint8_t>
    ↓
Emit as LLVM IR global constant array (just data, no functions)
    ↓
compile_to_object (trivial — just .rdata)
    ↓
link with mora_rt.lib → MoraRuntime.dll
```

The LLVM IR module contains:
- `@mora_patch_table = internal constant [N x i8] c"..."` — the serialized bytes
- `@mora_patch_table_size = external constant i32 N`
- No functions — the walker is in mora_rt.lib

## Runtime Walker (in mora_rt.lib)

```c
extern const uint8_t mora_patch_table[];
extern const uint32_t mora_patch_table_size;

void apply_all_patches(void* skyrim_base) {
    // Parse header
    auto* hdr = (PatchHeader*)mora_patch_table;
    if (hdr->magic != 0x4D4F5241 || hdr->version != 2) return;

    void* map_ptr = *(void**)(skyrim_base + MAP_OFFSET);

    auto* entries = (PatchEntry*)(mora_patch_table + sizeof(PatchHeader)
                                  + string_table_size);

    uint32_t current_fid = 0;
    void* current_form = nullptr;

    for (uint32_t i = 0; i < hdr->patch_count; i++) {
        auto& e = entries[i];
        if (e.formid != current_fid) {
            current_fid = e.formid;
            current_form = bst_hashmap_lookup(map_ptr, current_fid);
        }
        if (!current_form) continue;
        apply_patch_entry(skyrim_base, current_form, e, string_table);
    }
}
```

`apply_patch_entry` dispatches on field_id:
- Scalar fields (Damage, ArmorRating, GoldValue, Weight): direct GEP + store using `get_field_offset` and `get_form_type`
- Keywords: `mora_rt_add_keyword` / `mora_rt_remove_keyword`
- Spells/Perks/Factions: `mora_rt_add_spell` / `mora_rt_add_perk` / `mora_rt_add_faction`
- Names: `mora_rt_write_name` using string from string table

All these RT functions already exist in `mora_rt.lib`.

## Performance

**Compile time:**
- Serialize 3.9M patches: ~50ms
- LLVM IR module (data only): ~10ms
- Compile to object (.rdata only): ~100ms
- Link: ~100ms
- **Total: ~300ms** (vs 4.5 minutes)

**Runtime:**
- ~50K unique forms → 50K hashmap lookups: ~500µs
- 3.9M patch entries linear scan: ~4ms
- **Total: ~5ms** at DataLoaded

**DLL size:** ~62MB (patch data) + ~50KB (interpreter) = ~62MB

## Files to Change

**New:**
- `src/emit/patch_table.cpp` / `include/mora/emit/patch_table.h` — serialize ResolvedPatchSet to binary format
- `src/rt/patch_walker.cpp` — runtime walker (compiled into mora_rt.lib)

**Modify:**
- `src/main.cpp` — replace LLVM codegen call with table serialization + minimal IR emission
- `src/codegen/dll_builder.cpp` — add method to emit data-only IR module
- `src/rt/plugin_entry.cpp` — call walker's `apply_all_patches` instead of codegen'd version
- `scripts/build_rt_lib.sh` / `scripts/build_rt_bitcode.sh` — add patch_walker.cpp to build

**Dormant (not deleted):**
- `src/codegen/ir_emitter.cpp` — kept for future dynamic rule codegen
- All existing RT functions — still called by the walker

## Out of Scope

- Dynamic rules (future — will use LLVM codegen)
- Compression of the patch table (62MB is fine for disk, streams through L2 at runtime)
- Parallel patch application (patches are sorted by formid, sequential is cache-optimal)
