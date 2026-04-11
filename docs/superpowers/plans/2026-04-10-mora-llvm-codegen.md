# Mora LLVM Codegen Implementation Plan (Plan 6)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Emit a complete SKSE runtime DLL via LLVM, with all patches baked as static data, the BSTHashMap lookup inlined, and Address Library offsets resolved at compile time. The generated DLL has zero runtime overhead — no file parsing, no interpreter, no evaluation. Just direct memory writes at DataLoaded.

**Architecture:** The Mora compiler gains an LLVM backend that:
1. Reads the Address Library `.bin` file to resolve Skyrim function/data offsets
2. Compiles a runtime support module (`mora_rt`) to LLVM bitcode (BSTHashMap walker, form field mutators)
3. Emits LLVM IR for the patch application (one function per patch set, static data arrays)
4. LTO-links mora_rt.bc + patches.ll → MoraRuntime.dll
5. CRC32 hashes of FormIDs are constant-folded by LLVM, hash map indices precomputed

**Tech Stack:** C++20, LLVM 18+ (IR generation, LTO, target codegen), xmake.

---

## Design: What Gets Emitted

### The Generated DLL Contains:

```
.rdata section:
  - Patch table: array of {uint32_t formid, uint16_t field_id, uint8_t op, 
                           uint8_t value_type, uint64_t value}
  - String table: for name patches
  - Address Library offset for the global form map pointer

.text section:
  - apply_all_patches(): walks patch table, for each entry:
      1. BSTHashMap lookup (inlined from mora_rt via LTO)  
      2. Field write at known offset
  - DllMain / SKSEPlugin_Load / DataLoaded handler

.text section (from mora_rt.bc, inlined by LTO):
  - bst_crc32(key, len) → hash
  - bst_lookup(map_ptr, formid) → TESForm*
  - write_keyword_add(form, keyword_formid)
  - write_set_damage(form, value)
  - write_set_value(form, value)
  - write_set_weight(form, value)
  - write_set_armor_rating(form, value)
```

### LTO Optimization Wins:

Since every FormID in the patch table is a compile-time constant:
- `bst_crc32(0x00012EB7, 4)` → constant-folded to `0xABCD1234`
- `hash & (capacity - 1)` → can't fold (capacity is runtime), but the hash is free
- The entire lookup becomes: load entries pointer, index by precomputed hash, walk chain

For field writes on scalar values (damage, weight, gold value, armor rating):
- Direct `mov` instruction at known offset — no function call overhead

---

## File Structure

```
include/mora/
├── codegen/
│   ├── address_library.h    # read Address Library .bin → offset map
│   ├── ir_emitter.h         # emit LLVM IR for patch application
│   └── dll_builder.h        # orchestrate: IR + mora_rt.bc → DLL via LTO
├── rt/
│   ├── bst_hashmap.h        # BSTHashMap layout + lookup (compiled to bitcode)
│   ├── crc32.h              # CRC32 hash function (same as Skyrim's)
│   └── form_ops.h           # form field write operations

src/codegen/
├── address_library.cpp
├── ir_emitter.cpp
└── dll_builder.cpp

src/rt/
├── bst_hashmap.cpp          # compiled to mora_rt.bc via clang -emit-llvm
├── crc32.cpp
├── form_ops.cpp
└── plugin_entry.cpp         # DllMain + SKSEPlugin_Load + DataLoaded glue

tests/
├── address_library_test.cpp
├── crc32_test.cpp
├── ir_emitter_test.cpp
```

---

### Task 1: Address Library Reader

**Files:**
- Create: `include/mora/codegen/address_library.h`
- Create: `src/codegen/address_library.cpp`
- Create: `tests/address_library_test.cpp`

The Address Library is a `.bin` file that maps stable IDs to runtime offsets within Skyrim's executable. The file format is documented by meh321 (the author). We need to read it and resolve specific IDs we care about.

Key IDs we need:
- `514351` — pointer to the global `BSTHashMap<FormID, TESForm*>` (allForms map)
- `514352` — pointer to the global `BSTHashMap<BSFixedString, TESForm*>` (allFormsByEditorID)
- `514360` — BSReadWriteLock for allForms
- Additional IDs for any engine functions we need to call

The Address Library .bin format:
```
Header:
  int32  format_version (1 or 2)
  int32  skyrim_version[4] (major, minor, revision, build)
  int32  address_count

Entries (format v2, delta-encoded):
  For each entry:
    varint id_delta    (added to previous ID)
    varint offset_delta (added to previous offset)
```

- [ ] **Step 1: Write tests**

Test against the real Address Library file from Skyrim's Data/SKSE/Plugins/ folder:

```cpp
TEST(AddressLibraryTest, LoadRealFile) {
    auto path = find_address_library(); // searches Skyrim Data folder
    if (path.empty()) GTEST_SKIP();
    
    mora::AddressLibrary lib;
    ASSERT_TRUE(lib.load(path));
    EXPECT_GT(lib.entry_count(), 100000u); // typically ~700K entries
    
    // Look up the allForms map pointer
    auto offset = lib.resolve(514351);
    ASSERT_TRUE(offset.has_value());
    EXPECT_GT(*offset, 0u);
}

TEST(AddressLibraryTest, KnownOffsets) {
    // Verify a few well-known offsets match expected values
    // (these are version-specific, so may need updating)
}
```

- [ ] **Step 2: Implement AddressLibrary reader**

```cpp
class AddressLibrary {
public:
    bool load(const std::filesystem::path& bin_path);
    std::optional<uint64_t> resolve(uint64_t id) const;
    size_t entry_count() const;
    
    // Skyrim version this library was built for
    std::array<int32_t, 4> skyrim_version() const;

private:
    std::unordered_map<uint64_t, uint64_t> offsets_; // id → offset
    std::array<int32_t, 4> version_{};
};
```

The varint decoding for format v2: read bytes where high bit = continuation. Accumulate into uint64. Apply as delta to previous value.

- [ ] **Step 3: Build, test, commit**

```bash
xmake build address_library_test && xmake run address_library_test
git add -A && git commit -m "feat: Address Library reader for resolving Skyrim runtime offsets"
```

---

### Task 2: CRC32 + BSTHashMap Lookup (Runtime Support)

**Files:**
- Create: `include/mora/rt/crc32.h` + `src/rt/crc32.cpp`
- Create: `include/mora/rt/bst_hashmap.h` + `src/rt/bst_hashmap.cpp`
- Create: `tests/crc32_test.cpp`

These implement Skyrim's exact CRC32 and BSTHashMap lookup algorithm. They'll be compiled to LLVM bitcode for LTO inlining, but are also usable as regular C++ for testing.

- [ ] **Step 1: Implement CRC32 matching Skyrim's**

The CRC32 table and algorithm from CommonLibSSE's `RE/C/CRC.h` — it's the standard CRC32 with polynomial 0xEDB88320.

```cpp
namespace mora::rt {

uint32_t bst_crc32(const void* data, size_t len);

// Convenience: hash a FormID
inline uint32_t hash_formid(uint32_t formid) {
    return bst_crc32(&formid, sizeof(formid));
}

} // namespace mora::rt
```

- [ ] **Step 2: Implement BSTHashMap lookup**

```cpp
namespace mora::rt {

// BSTHashMap memory layout (matches Skyrim's BSTScatterTable)
struct BSTEntry {
    uint32_t key;       // 0x00: FormID
    uint32_t pad;       // 0x04
    void*    value;     // 0x08: TESForm*
    BSTEntry* next;     // 0x10: chain pointer (nullptr=empty, sentinel=end)
};
static_assert(sizeof(BSTEntry) == 0x18);

struct BSTHashMapLayout {
    uint64_t pad00;         // 0x00
    uint32_t pad08;         // 0x08
    uint32_t capacity;      // 0x0C (always power of 2)
    uint32_t free;          // 0x10
    uint32_t good;          // 0x14
    BSTEntry* sentinel;     // 0x18
    uint64_t alloc_pad;     // 0x20
    BSTEntry* entries;      // 0x28
};

// Look up a FormID in a BSTHashMap. Returns TESForm* or nullptr.
void* bst_hashmap_lookup(const BSTHashMapLayout* map, uint32_t formid);

} // namespace mora::rt
```

Implementation:
```cpp
void* bst_hashmap_lookup(const BSTHashMapLayout* map, uint32_t formid) {
    if (!map || !map->entries || map->capacity == 0) return nullptr;
    
    uint32_t hash = hash_formid(formid);
    uint32_t idx = hash & (map->capacity - 1);
    
    BSTEntry* entry = &map->entries[idx];
    if (!entry->next) return nullptr; // empty slot
    
    const BSTEntry* sentinel = map->sentinel;
    do {
        if (entry->key == formid) return entry->value;
        entry = entry->next;
    } while (entry != sentinel);
    
    return nullptr;
}
```

- [ ] **Step 3: Test CRC32 against known values**

```cpp
TEST(CRC32Test, HashFormID) {
    // CRC32 of the 4 bytes of 0x00012EB7 (IronSword)
    uint32_t formid = 0x00012EB7;
    uint32_t hash = mora::rt::hash_formid(formid);
    // Verify against known CRC32 computation
    EXPECT_NE(hash, 0u);
    // Same input → same hash
    EXPECT_EQ(hash, mora::rt::hash_formid(formid));
}

TEST(CRC32Test, DifferentFormIDs) {
    EXPECT_NE(mora::rt::hash_formid(0x100), mora::rt::hash_formid(0x200));
}
```

- [ ] **Step 4: Build, test, commit**

```bash
xmake build crc32_test && xmake run crc32_test
git add -A && git commit -m "feat: CRC32 + BSTHashMap lookup matching Skyrim's implementation"
```

---

### Task 3: LLVM IR Emitter

**Files:**
- Create: `include/mora/codegen/ir_emitter.h`
- Create: `src/codegen/ir_emitter.cpp`
- Create: `tests/ir_emitter_test.cpp`

The IR emitter takes a resolved PatchSet and emits LLVM IR that applies all patches. This is the core of the codegen system.

- [ ] **Step 1: Define IR Emitter**

```cpp
class IREmitter {
public:
    IREmitter(llvm::LLVMContext& ctx, llvm::Module& mod,
              const AddressLibrary& addrlib);

    // Emit the patch application function
    // Generates: void apply_all_patches(void* skyrim_base)
    void emit_patch_function(const ResolvedPatchSet& patches,
                              StringPool& pool);

    // Emit static data (patch table, string table)
    void emit_patch_data(const ResolvedPatchSet& patches,
                          StringPool& pool);

    // Emit the plugin entry point (DllMain, SKSEPlugin_Load, DataLoaded handler)
    void emit_plugin_entry();

private:
    llvm::LLVMContext& ctx_;
    llvm::Module& mod_;
    const AddressLibrary& addrlib_;
    
    // Emit a single form lookup + field write
    llvm::Value* emit_form_lookup(llvm::IRBuilder<>& builder,
                                   llvm::Value* map_ptr, uint32_t formid);
    void emit_field_write(llvm::IRBuilder<>& builder,
                          llvm::Value* form_ptr, const FieldPatch& patch);
};
```

- [ ] **Step 2: Implement IR generation**

The emitted IR for `apply_all_patches`:

```llvm
define void @apply_all_patches(i8* %skyrim_base) {
entry:
    ; Load the global form map pointer
    ; Address Library resolved: ID 514351 → offset 0xNNNNNN
    %map_ptr_addr = getelementptr i8, i8* %skyrim_base, i64 OFFSET_514351
    %map_ptr_ptr = bitcast i8* %map_ptr_addr to %BSTHashMapLayout**
    %map = load %BSTHashMapLayout*, %BSTHashMapLayout** %map_ptr_ptr

    ; Patch 1: IronSword damage = 42
    %form1 = call i8* @bst_hashmap_lookup(%BSTHashMapLayout* %map, i32 77495) ; 0x12EB7
    %form1_nn = icmp ne i8* %form1, null
    br i1 %form1_nn, label %patch1, label %skip1
patch1:
    %dmg_ptr = getelementptr i8, i8* %form1, i64 200  ; offset 0xC8 = attack damage
    %dmg_field = bitcast i8* %dmg_ptr to i16*
    store i16 42, i16* %dmg_field
    br label %skip1
skip1:
    ; Patch 2: ...
    ; ... thousands more ...

    ret void
}
```

For LTO, also declare `@bst_hashmap_lookup` as an external function that will be linked from `mora_rt.bc`.

- [ ] **Step 3: Test IR generation**

```cpp
TEST(IREmitterTest, EmitSimplePatch) {
    llvm::LLVMContext ctx;
    llvm::Module mod("test", ctx);
    
    mora::AddressLibrary addrlib;
    // Use fake offset for testing
    
    mora::IREmitter emitter(ctx, mod, addrlib);
    
    mora::PatchSet ps;
    ps.add_patch(0x12EB7, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(42), {}, 0);
    auto resolved = ps.resolve();
    
    mora::StringPool pool;
    emitter.emit_patch_function(resolved, pool);
    
    // Verify the module has the function
    auto* func = mod.getFunction("apply_all_patches");
    ASSERT_NE(func, nullptr);
    
    // Verify it's valid IR
    std::string err;
    llvm::raw_string_ostream err_stream(err);
    EXPECT_FALSE(llvm::verifyModule(mod, &err_stream));
}
```

- [ ] **Step 4: Build, test, commit**

Add LLVM as a dependency in xmake:
```lua
add_requires("llvm")
-- or system LLVM:
add_includedirs("/usr/include/llvm-18")
add_linkdirs("/usr/lib/llvm-18/lib")
```

```bash
xmake build ir_emitter_test && xmake run ir_emitter_test
git add -A && git commit -m "feat: LLVM IR emitter for patch application code"
```

---

### Task 4: Compile mora_rt to Bitcode

**Files:**
- Create: `scripts/build_rt_bitcode.sh`

Compile the runtime support functions to LLVM bitcode for LTO linking.

- [ ] **Step 1: Create build script**

```bash
#!/bin/bash
# Compile mora_rt to LLVM bitcode for LTO linking into generated DLLs
clang-cl --target=x86_64-pc-windows-msvc \
    -imsvc "$HOME/.xwin/crt/include" \
    -imsvc "$HOME/.xwin/sdk/include/ucrt" \
    -imsvc "$HOME/.xwin/sdk/include/um" \
    -imsvc "$HOME/.xwin/sdk/include/shared" \
    -I include \
    /std:c++20 /O2 /DWIN32 /DNOMINMAX \
    -emit-llvm -c \
    -o build/mora_rt.bc \
    src/rt/bst_hashmap.cpp src/rt/crc32.cpp src/rt/form_ops.cpp src/rt/plugin_entry.cpp
```

The `-emit-llvm -c` flag produces LLVM bitcode instead of object code. This bitcode is then LTO-linked with the generated IR.

- [ ] **Step 2: Build and verify**

```bash
bash scripts/build_rt_bitcode.sh
llvm-dis build/mora_rt.bc -o /dev/stdout | head -20  # verify it's valid bitcode
```

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "feat: mora_rt compiled to LLVM bitcode for LTO"
```

---

### Task 5: DLL Builder — LTO Link to Final DLL

**Files:**
- Create: `include/mora/codegen/dll_builder.h`
- Create: `src/codegen/dll_builder.cpp`

Orchestrates the full pipeline: IR emission + LTO link with mora_rt.bc → MoraRuntime.dll

- [ ] **Step 1: Define DLLBuilder**

```cpp
class DLLBuilder {
public:
    DLLBuilder(const AddressLibrary& addrlib);

    // Full pipeline: patches → DLL
    bool build(const ResolvedPatchSet& patches,
               StringPool& pool,
               const std::filesystem::path& rt_bitcode_path,
               const std::filesystem::path& output_dll);

private:
    // Step 1: Generate IR module with patch data + application function
    std::unique_ptr<llvm::Module> generate_ir(const ResolvedPatchSet& patches,
                                                StringPool& pool,
                                                llvm::LLVMContext& ctx);

    // Step 2: Load mora_rt.bc and LTO-link with generated IR
    std::unique_ptr<llvm::Module> lto_link(std::unique_ptr<llvm::Module> patches_mod,
                                            const std::filesystem::path& rt_bc,
                                            llvm::LLVMContext& ctx);

    // Step 3: Compile linked module to object code
    bool compile_to_object(llvm::Module& mod,
                            const std::filesystem::path& obj_path);

    // Step 4: Link object to DLL
    bool link_dll(const std::filesystem::path& obj_path,
                   const std::filesystem::path& dll_path);

    const AddressLibrary& addrlib_;
};
```

- [ ] **Step 2: Implement the pipeline**

The LTO link uses `llvm::Linker::linkModules()` to merge mora_rt.bc into the generated IR module. Then the LLVM optimization pipeline runs (with LTO-level optimizations that inline across module boundaries). Finally, the LLVM target machine emits x86-64 object code, and lld-link produces the DLL.

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "feat: DLL builder orchestrates IR → LTO → DLL pipeline"
```

---

### Task 6: Wire into `mora compile --emit-dll`

**Files:**
- Modify: `src/main.cpp`

Add `--emit-dll` flag to the compile command. When present, instead of (or in addition to) emitting `.mora.patch`, emit a complete `MoraRuntime.dll`.

```
$ mora compile --emit-dll test_data/example.mora

  mora v0.1.0

  ✓ Parsing 1 files                                   <1ms
  ✓ Resolving 5 rules                                 <1ms
  ✓ Type checking 5 rules                             <1ms
  ✓ 5 static, 0 dynamic                               <1ms
  ✓ 7 plugins, 7 relations → 86781 facts             449ms
  ✓ 681 patches generated                             486ms
  ✓ Address Library loaded (714,251 entries)            12ms
  ✓ LLVM IR generated (681 patches)                     3ms
  ✓ LTO linking with mora_rt.bc                        45ms
  ✓ MoraRuntime.dll emitted                            22ms

    Output: MoraCache/MoraRuntime.dll (148 KB)
    681 patches baked into native code
    Estimated runtime: ~7ms at DataLoaded
```

- [ ] **Step 1: Add --emit-dll flag and codegen pipeline**

After the existing evaluate phase, if `--emit-dll` is set:
1. Load Address Library
2. Create LLVM context + module
3. Emit IR via IREmitter
4. LTO link with mora_rt.bc
5. Compile to DLL via DLLBuilder

- [ ] **Step 2: Test end-to-end**

```bash
xmake build mora
mora compile --emit-dll test_data/example.mora
file MoraCache/MoraRuntime.dll  # should be PE32+ DLL
```

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "feat: mora compile --emit-dll generates native SKSE plugin"
```

---

### Task 7: Form Operations Module (mora_rt)

**Files:**
- Create: `include/mora/rt/form_ops.h`
- Create: `src/rt/form_ops.cpp`
- Create: `src/rt/plugin_entry.cpp`

The form operations that get compiled to bitcode and LTO-linked. These use the ABI shim offsets to write form fields.

- [ ] **Step 1: Implement form_ops**

```cpp
namespace mora::rt {

// These functions get inlined by LTO into the generated patch code.
// They use the skyrim_abi.h offset constants.

void write_set_damage(void* form, uint16_t value);
void write_set_armor_rating(void* form, uint32_t value);
void write_set_value(void* form, int32_t value);
void write_set_weight(void* form, float value);

// Keyword array mutation — requires Skyrim heap allocation.
// For Phase 1, this is a no-op stub. Full implementation requires
// calling Skyrim's memory allocator via Address Library.
void write_keyword_add(void* form, uint32_t keyword_formid);

} // namespace mora::rt
```

- [ ] **Step 2: Implement plugin_entry**

```cpp
// Minimal SKSE plugin entry — called by SKSE loader
// The DataLoaded handler calls the generated apply_all_patches()

extern "C" void apply_all_patches(void* skyrim_base); // generated by IR emitter

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(void* skse) {
    // Register for DataLoaded message
    // On DataLoaded: call apply_all_patches(GetModuleHandle("SkyrimSE.exe"))
    return true;
}
```

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "feat: mora_rt form operations and plugin entry for LTO"
```

---

## Summary

This plan delivers the Mora LLVM codegen pipeline:

1. **Address Library Reader** — resolve Skyrim offsets at compile time
2. **CRC32 + BSTHashMap** — exact reimplementation of Skyrim's hash map lookup
3. **LLVM IR Emitter** — generate patch application code as LLVM IR
4. **mora_rt Bitcode** — runtime support compiled to LLVM bitcode
5. **DLL Builder** — LTO link IR + bitcode → native Windows DLL
6. **CLI: --emit-dll** — one command to go from .mora rules to SKSE plugin
7. **Form Operations** — field write functions for LTO inlining

**Performance target:**
- 112K patches applied in ~11ms (vs KID's 25s, SkyPatcher's 2-45s)
- Zero file parsing at runtime — all data baked into .rdata
- CRC32 hashes constant-folded by LLVM optimizer
- Hash map lookup inlined at every call site via LTO

**The command:**
```bash
mora compile --emit-dll my_rules.mora
# → MoraCache/MoraRuntime.dll (drop into Data/SKSE/Plugins/)
```
