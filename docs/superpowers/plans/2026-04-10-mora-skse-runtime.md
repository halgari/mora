# Mora SKSE Runtime Plugin Implementation Plan (Plan 5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Mora SKSE runtime plugin — a single Windows DLL that replaces dozens of distributor plugins. At `DataLoaded`, it loads the pre-computed `.mora.patch` file and applies field-level diffs to game forms in milliseconds. For dynamic rules, it loads `.mora.rt` and evaluates them on NPC/cell load events.

**Architecture:** The runtime DLL shares core Mora code (FactDB, Value, PatchReader, Evaluator) compiled for Windows via clang-cl cross-compilation. A thin SKSE glue layer handles plugin initialization, event registration, and bridges between Mora's abstract patch operations and CommonLibSSE's concrete form mutation APIs. The build uses xmake with a separate Windows target alongside the existing Linux targets.

**Tech Stack:** C++20, xmake, clang-cl (cross-compile from Linux), lld-link, xwin Windows SDK, powerof3's CommonLibSSE, Address Library for SKSE.

---

## Cross-Compilation Setup

The toolchain (already verified working):
- **clang-cl** targeting `x86_64-pc-windows-msvc`
- **lld-link** for Windows PE/DLL linking
- **xwin SDK** at `~/.xwin/` (MSVC CRT headers/libs + Windows SDK)
- **CommonLibSSE** at `~/oss/CommonLibSSE/`

xmake will have two build configurations:
- `mora` (default, Linux): the CLI compiler tool
- `mora_runtime` (Windows cross-compile): the SKSE DLL

---

## File Structure

```
include/mora/
├── runtime/
│   ├── plugin.h            # SKSE plugin entry points
│   ├── patch_applier.h     # apply .mora.patch diffs to game forms
│   ├── form_bridge.h       # bridge between Mora FieldId/Value and CommonLibSSE types
│   └── dynamic_runner.h    # evaluate .mora.rt rules on events
src/runtime/
├── plugin.cpp              # SKSEPlugin_Load, messaging, initialization
├── patch_applier.cpp       # read .mora.patch, walk entries, mutate forms
├── form_bridge.cpp         # map FieldId → CommonLibSSE mutation calls
└── dynamic_runner.cpp      # event hooks, FactDB population from live forms, evaluation
```

The existing `src/data/`, `src/eval/`, `src/emit/` code is shared — compiled into both the Linux CLI lib and the Windows runtime DLL.

---

### Task 1: xmake Cross-Compile Configuration

**Files:**
- Modify: `xmake.lua`

Add a Windows DLL target that cross-compiles with clang-cl. This is build infrastructure only — no runtime code yet.

- [ ] **Step 1: Add Windows cross-compile target to xmake.lua**

Add a new target `mora_runtime` that:
- Uses `set_kind("shared")` (DLL)
- Targets Windows x64 via clang-cl
- Includes CommonLibSSE headers
- Links against the Windows SDK from xwin
- Compiles the shared Mora core code (data/, eval/, emit/) plus runtime-specific code (runtime/)
- Sets output to `MoraRuntime.dll`

```lua
-- Cross-compile target for SKSE runtime DLL
if is_plat("windows") or has_config("cross_windows") then
    target("mora_runtime")
        set_kind("shared")
        set_languages("c++20")
        set_targetdir("$(buildir)/windows/x86_64")
        set_filename("MoraRuntime.dll")

        -- Shared Mora core code
        add_files("src/core/*.cpp", "src/data/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp")
        add_includedirs("include", {public = true})

        -- Runtime-specific code
        add_files("src/runtime/*.cpp")
        add_includedirs("include")

        -- CommonLibSSE
        add_includedirs(path.join(os.getenv("HOME"), "oss/CommonLibSSE/include"))

        -- Windows SDK (xwin)
        local xwin = path.join(os.getenv("HOME"), ".xwin")
        add_sysincludedirs(path.join(xwin, "crt/include"))
        add_sysincludedirs(path.join(xwin, "sdk/include/ucrt"))
        add_sysincludedirs(path.join(xwin, "sdk/include/um"))
        add_sysincludedirs(path.join(xwin, "sdk/include/shared"))
        add_linkdirs(path.join(xwin, "crt/lib/x86_64"))
        add_linkdirs(path.join(xwin, "sdk/lib/um/x86_64"))
        add_linkdirs(path.join(xwin, "sdk/lib/ucrt/x86_64"))

        -- Compiler flags
        add_cxflags("/EHsc", "/permissive-", {force = true})
        add_defines("WIN32", "_WINDOWS", "NOMINMAX", "SKSE_SUPPORT_XBYAK")

        -- Linker flags
        add_ldflags("/DLL", {force = true})
    target_end()
end
```

NOTE: This target won't build yet — we haven't created the runtime source files. The goal of this task is just to get the xmake configuration right so subsequent tasks can compile.

- [ ] **Step 2: Create placeholder runtime files**

```bash
mkdir -p include/mora/runtime src/runtime
```

Create `src/runtime/plugin.cpp`:
```cpp
// Placeholder — will be implemented in subsequent tasks
#ifdef _WIN32
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
#endif
```

- [ ] **Step 3: Verify the Linux build still works**

```bash
xmake build mora
xmake test
```
All existing tests must pass — the new target doesn't affect the Linux build.

- [ ] **Step 4: Test cross-compile (may fail on missing CommonLibSSE deps — that's OK for now)**

```bash
xmake f -p windows --toolchain=clang-cl
xmake build mora_runtime
```

If CommonLibSSE headers cause issues, note them — we'll address in subsequent tasks.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: xmake cross-compile configuration for SKSE runtime DLL"
```

---

### Task 2: Form Bridge — Map Mora Operations to CommonLibSSE

**Files:**
- Create: `include/mora/runtime/form_bridge.h`
- Create: `src/runtime/form_bridge.cpp`

The Form Bridge maps Mora's abstract patch operations (FieldId + FieldOp + Value) to concrete CommonLibSSE API calls. This is the critical translation layer.

- [ ] **Step 1: Define the FormBridge interface**

```cpp
// include/mora/runtime/form_bridge.h
#pragma once

#include "mora/eval/patch_set.h"
#include "mora/data/value.h"

#ifdef _WIN32
#include <RE/Skyrim.h>
#endif

namespace mora {

class FormBridge {
public:
    // Apply a single field patch to a game form
    static bool apply_patch(uint32_t formid, const FieldPatch& patch);

    // Apply all patches for a form
    static int apply_patches(uint32_t formid, const std::vector<FieldPatch>& patches);

    // Populate a FactDB from live game forms (for dynamic rule evaluation)
    static void populate_facts_for_npc(uint32_t formid, class FactDB& db, class StringPool& pool);

private:
#ifdef _WIN32
    static bool apply_keyword_add(RE::TESForm* form, uint32_t keyword_formid);
    static bool apply_keyword_remove(RE::TESForm* form, uint32_t keyword_formid);
    static bool apply_spell_add(RE::TESNPC* npc, uint32_t spell_formid);
    static bool apply_perk_add(RE::TESNPC* npc, uint32_t perk_formid);
    static bool apply_set_damage(RE::TESObjectWEAP* weapon, int64_t value);
    static bool apply_set_armor_rating(RE::TESObjectARMO* armor, int64_t value);
    static bool apply_set_name(RE::TESForm* form, const char* name);
    static bool apply_set_value(RE::TESForm* form, int64_t value);
    static bool apply_set_weight(RE::TESForm* form, double value);
#endif
};

} // namespace mora
```

- [ ] **Step 2: Implement the bridge**

Each method looks up the target form via `RE::TESForm::LookupByID()`, casts to the appropriate type, and calls the CommonLibSSE mutation API:

```cpp
// src/runtime/form_bridge.cpp
#include "mora/runtime/form_bridge.h"

#ifdef _WIN32
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#endif

namespace mora {

bool FormBridge::apply_patch(uint32_t formid, const FieldPatch& patch) {
#ifdef _WIN32
    auto* form = RE::TESForm::LookupByID(formid);
    if (!form) return false;

    switch (patch.field) {
        case FieldId::Keywords:
            if (patch.op == FieldOp::Add)
                return apply_keyword_add(form, patch.value.as_formid());
            else if (patch.op == FieldOp::Remove)
                return apply_keyword_remove(form, patch.value.as_formid());
            break;

        case FieldId::Spells:
            if (auto* npc = form->As<RE::TESNPC>())
                return apply_spell_add(npc, patch.value.as_formid());
            break;

        case FieldId::Perks:
            if (auto* npc = form->As<RE::TESNPC>())
                return apply_perk_add(npc, patch.value.as_formid());
            break;

        case FieldId::Damage:
            if (auto* weap = form->As<RE::TESObjectWEAP>())
                return apply_set_damage(weap, patch.value.as_int());
            break;

        case FieldId::ArmorRating:
            if (auto* armor = form->As<RE::TESObjectARMO>())
                return apply_set_armor_rating(armor, patch.value.as_int());
            break;

        case FieldId::Name:
            return apply_set_name(form, /* resolve string */ "");

        case FieldId::GoldValue:
            return apply_set_value(form, patch.value.as_int());

        case FieldId::Weight:
            return apply_set_weight(form, patch.value.as_float());

        default:
            break;
    }
#else
    (void)formid; (void)patch;
#endif
    return false;
}

int FormBridge::apply_patches(uint32_t formid, const std::vector<FieldPatch>& patches) {
    int applied = 0;
    for (const auto& p : patches) {
        if (apply_patch(formid, p)) applied++;
    }
    return applied;
}

#ifdef _WIN32

bool FormBridge::apply_keyword_add(RE::TESForm* form, uint32_t keyword_formid) {
    auto* kw_form = RE::TESForm::LookupByID<RE::BGSKeyword>(keyword_formid);
    if (!kw_form) return false;

    // BGSKeywordForm is a base class of NPC, WEAP, ARMO, etc.
    if (auto* kw_holder = form->As<RE::BGSKeywordForm>()) {
        return kw_holder->AddKeyword(kw_form);
    }
    return false;
}

bool FormBridge::apply_keyword_remove(RE::TESForm* form, uint32_t keyword_formid) {
    auto* kw_form = RE::TESForm::LookupByID<RE::BGSKeyword>(keyword_formid);
    if (!kw_form) return false;

    if (auto* kw_holder = form->As<RE::BGSKeywordForm>()) {
        return kw_holder->RemoveKeyword(kw_form);
    }
    return false;
}

bool FormBridge::apply_spell_add(RE::TESNPC* npc, uint32_t spell_formid) {
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spell_formid);
    if (!spell) return false;

    auto* spell_list = npc->GetSpellList();
    if (!spell_list) return false;

    return spell_list->AddSpell(spell);
}

bool FormBridge::apply_perk_add(RE::TESNPC* npc, uint32_t perk_formid) {
    auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(perk_formid);
    if (!perk) return false;

    return npc->AddPerk(perk, 1);
}

bool FormBridge::apply_set_damage(RE::TESObjectWEAP* weapon, int64_t value) {
    weapon->attackDamage = static_cast<uint16_t>(value);
    return true;
}

bool FormBridge::apply_set_armor_rating(RE::TESObjectARMO* armor, int64_t value) {
    armor->armorRating = static_cast<uint32_t>(value);
    return true;
}

bool FormBridge::apply_set_name(RE::TESForm* form, const char* name) {
    if (auto* named = form->As<RE::TESFullName>()) {
        named->SetFullName(name);
        return true;
    }
    return false;
}

bool FormBridge::apply_set_value(RE::TESForm* form, int64_t value) {
    if (auto* valued = form->As<RE::TESValueForm>()) {
        valued->value = static_cast<int32_t>(value);
        return true;
    }
    return false;
}

bool FormBridge::apply_set_weight(RE::TESForm* form, double value) {
    if (auto* weighted = form->As<RE::TESWeightForm>()) {
        weighted->weight = static_cast<float>(value);
        return true;
    }
    return false;
}

#endif // _WIN32

void FormBridge::populate_facts_for_npc(uint32_t formid, FactDB& db, StringPool& pool) {
#ifdef _WIN32
    auto* form = RE::TESForm::LookupByID<RE::TESNPC>(formid);
    if (!form) return;

    auto npc_rel = pool.intern("npc");
    db.add_fact(npc_rel, {Value::make_formid(formid)});

    // Keywords
    auto has_kw = pool.intern("has_keyword");
    for (uint32_t i = 0; i < form->GetNumKeywords(); i++) {
        auto opt_kw = form->GetKeywordAt(i);
        if (opt_kw && *opt_kw) {
            db.add_fact(has_kw, {Value::make_formid(formid),
                                  Value::make_formid((*opt_kw)->GetFormID())});
        }
    }

    // Factions
    auto has_faction = pool.intern("has_faction");
    for (auto& fr : form->factions) {
        if (fr.faction) {
            db.add_fact(has_faction, {Value::make_formid(formid),
                                      Value::make_formid(fr.faction->GetFormID())});
        }
    }

    // Level
    auto current_level = pool.intern("current_level");
    // Note: base level from actorBaseData; current level requires Actor reference
    auto base_level = pool.intern("base_level");
    // ACBS data for level is complex — simplified for Phase 1

    // Race
    auto race_of = pool.intern("race_of");
    if (form->GetRace()) {
        db.add_fact(race_of, {Value::make_formid(formid),
                               Value::make_formid(form->GetRace()->GetFormID())});
    }
#else
    (void)formid; (void)db; (void)pool;
#endif
}

} // namespace mora
```

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "feat: form bridge maps Mora patches to CommonLibSSE mutations"
```

---

### Task 3: Patch Applier — Load and Apply .mora.patch

**Files:**
- Create: `include/mora/runtime/patch_applier.h`
- Create: `src/runtime/patch_applier.cpp`

The Patch Applier reads the `.mora.patch` binary file, walks the entries, and calls FormBridge for each patch. This is what runs at `DataLoaded` for the fast-path static patches.

- [ ] **Step 1: Define PatchApplier**

```cpp
// include/mora/runtime/patch_applier.h
#pragma once

#include "mora/emit/patch_reader.h"
#include "mora/runtime/form_bridge.h"
#include "mora/core/string_pool.h"
#include <filesystem>
#include <string>

namespace mora {

struct ApplyResult {
    size_t patches_applied = 0;
    size_t patches_failed = 0;
    size_t forms_modified = 0;
    double elapsed_ms = 0;
};

class PatchApplier {
public:
    PatchApplier(StringPool& pool);

    // Load and apply a .mora.patch file
    ApplyResult apply(const std::filesystem::path& patch_path);

    // Check if the patch file is stale (load order changed)
    bool is_stale(const std::filesystem::path& lock_path) const;

private:
    StringPool& pool_;
};

} // namespace mora
```

- [ ] **Step 2: Implement PatchApplier**

```cpp
// src/runtime/patch_applier.cpp
#include "mora/runtime/patch_applier.h"
#include "mora/emit/lock_file.h"
#include <chrono>
#include <fstream>

namespace mora {

PatchApplier::PatchApplier(StringPool& pool) : pool_(pool) {}

ApplyResult PatchApplier::apply(const std::filesystem::path& patch_path) {
    ApplyResult result;
    auto start = std::chrono::steady_clock::now();

    std::ifstream in(patch_path, std::ios::binary);
    if (!in.is_open()) return result;

    PatchReader reader(pool_);
    auto patch_file = reader.read(in);
    if (!patch_file) return result;

    for (const auto& rp : patch_file->patches) {
        int applied = FormBridge::apply_patches(rp.target_formid, rp.fields);
        result.patches_applied += applied;
        result.patches_failed += (rp.fields.size() - applied);
        if (applied > 0) result.forms_modified++;
    }

    auto end = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

    return result;
}

bool PatchApplier::is_stale(const std::filesystem::path& lock_path) const {
    // Runtime staleness check — compare lock file hashes against current load order
    // For Phase 1, always return false (trust the lock file)
    (void)lock_path;
    return false;
}

} // namespace mora
```

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "feat: patch applier loads and applies .mora.patch at runtime"
```

---

### Task 4: Dynamic Runner — Evaluate .mora.rt Rules on Events

**Files:**
- Create: `include/mora/runtime/dynamic_runner.h`
- Create: `src/runtime/dynamic_runner.cpp`

The Dynamic Runner handles rules that couldn't be frozen — those depending on runtime state like NPC location, equipped items, or quest progress. It loads `.mora.rt`, registers for SKSE events, and evaluates matching rules when triggered.

- [ ] **Step 1: Define DynamicRunner**

```cpp
// include/mora/runtime/dynamic_runner.h
#pragma once

#include "mora/emit/rt_writer.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/evaluator.h"
#include "mora/runtime/form_bridge.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <filesystem>
#include <vector>

namespace mora {

class DynamicRunner {
public:
    DynamicRunner(StringPool& pool, DiagBag& diags);

    // Load the .mora.rt file
    bool load(const std::filesystem::path& rt_path);

    // Called when an NPC loads into a cell
    void on_npc_load(uint32_t npc_formid);

    // Called at DataLoaded for on_data_loaded trigger rules
    void on_data_loaded();

    size_t rules_loaded() const { return rule_count_; }

private:
    StringPool& pool_;
    DiagBag& diags_;
    size_t rule_count_ = 0;
    // Phase 1: the .mora.rt bytecode format is metadata-only,
    // so dynamic evaluation is a stub that logs rule triggers.
    // Full evaluation requires the parsed AST (Plan 6 or later).
};

} // namespace mora
```

- [ ] **Step 2: Implement DynamicRunner (stub for Phase 1)**

For Phase 1, the dynamic runner loads the `.mora.rt` header to know how many rules exist and what triggers they need, but doesn't actually evaluate them. Full bytecode evaluation is a future enhancement.

```cpp
// src/runtime/dynamic_runner.cpp
#include "mora/runtime/dynamic_runner.h"
#include <fstream>
#include <cstring>

namespace mora {

DynamicRunner::DynamicRunner(StringPool& pool, DiagBag& diags)
    : pool_(pool), diags_(diags) {}

bool DynamicRunner::load(const std::filesystem::path& rt_path) {
    std::ifstream in(rt_path, std::ios::binary);
    if (!in.is_open()) return false;

    // Read header: magic "MORT" + version u16 + rule_count u32
    char magic[4];
    in.read(magic, 4);
    if (std::memcmp(magic, "MORT", 4) != 0) return false;

    uint16_t version;
    in.read(reinterpret_cast<char*>(&version), 2);

    uint32_t count;
    in.read(reinterpret_cast<char*>(&count), 4);
    rule_count_ = count;

    return true;
}

void DynamicRunner::on_npc_load(uint32_t npc_formid) {
    // Phase 1 stub: log that we would evaluate dynamic rules for this NPC
    (void)npc_formid;
}

void DynamicRunner::on_data_loaded() {
    // Phase 1 stub: log that data-loaded trigger rules would fire
}

} // namespace mora
```

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "feat: dynamic runner stub for .mora.rt rule evaluation"
```

---

### Task 5: SKSE Plugin Entry Point

**Files:**
- Rewrite: `src/runtime/plugin.cpp`

The main SKSE plugin that ties everything together: initialization, event registration, and the `DataLoaded` handler that triggers patch application.

- [ ] **Step 1: Implement the SKSE plugin**

```cpp
// src/runtime/plugin.cpp
#ifdef _WIN32

#include "mora/runtime/patch_applier.h"
#include "mora/runtime/dynamic_runner.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace {
    // Find MoraCache directory relative to the Data folder
    fs::path find_mora_cache() {
        // SKSE plugins run from Data/SKSE/Plugins/
        // MoraCache is in Data/MoraCache/
        fs::path data_dir = fs::current_path() / "Data";
        if (!fs::exists(data_dir)) {
            // Try relative to the plugin DLL location
            data_dir = fs::path(".");
        }
        return data_dir / "MoraCache";
    }

    void on_message(SKSE::MessagingInterface::Message* msg) {
        if (msg->type != SKSE::MessagingInterface::kDataLoaded) return;

        SKSE::log::info("Mora: DataLoaded — applying patches");

        auto cache_dir = find_mora_cache();
        if (!fs::exists(cache_dir)) {
            SKSE::log::warn("Mora: No MoraCache found at {}", cache_dir.string());
            return;
        }

        mora::StringPool pool;
        mora::DiagBag diags;

        // Apply static patches
        auto patch_path = cache_dir / "mora.patch";
        if (fs::exists(patch_path)) {
            mora::PatchApplier applier(pool);
            auto result = applier.apply(patch_path);
            SKSE::log::info("Mora: Applied {} patches to {} forms in {:.1f}ms",
                           result.patches_applied, result.forms_modified, result.elapsed_ms);
            if (result.patches_failed > 0) {
                SKSE::log::warn("Mora: {} patches failed to apply", result.patches_failed);
            }
        }

        // Load dynamic rules
        auto rt_path = cache_dir / "mora.rt";
        if (fs::exists(rt_path)) {
            mora::DynamicRunner runner(pool, diags);
            if (runner.load(rt_path)) {
                runner.on_data_loaded();
                SKSE::log::info("Mora: Loaded {} dynamic rules", runner.rules_loaded());
            }
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);
    SKSE::log::info("Mora runtime v0.1.0 loaded");

    auto* messaging = SKSE::GetMessagingInterface();
    if (messaging) {
        messaging->RegisterListener(on_message);
    }

    return true;
}

#else
// Non-Windows stub for compilation testing
namespace mora { void runtime_stub() {} }
#endif
```

- [ ] **Step 2: Add plugin version info**

Create `src/runtime/version.rc` or use the `SKSEPluginVersion` macro:

```cpp
// Add to plugin.cpp before SKSEPluginLoad:
extern "C" __declspec(dllexport)
constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion({ 0, 1, 0, 0 });
    v.PluginName("Mora");
    v.AuthorName("Mora Project");
    v.UsesAddressLibrary();
    v.HasNoStructUse();
    return v;
}();
```

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "feat: SKSE plugin entry point with DataLoaded handler"
```

---

### Task 6: Build and Test the DLL

**Files:**
- Modify: `xmake.lua` (finalize cross-compile settings)

- [ ] **Step 1: Build the runtime DLL**

```bash
xmake f -p windows --cross=x86_64-pc-windows-msvc --toolchain=clang-cl
xmake build mora_runtime
```

Address any CommonLibSSE compilation issues:
- Missing headers → add include paths
- MSVC-specific intrinsics → add compatibility flags
- Address Library dependency → may need to stub or include

- [ ] **Step 2: Verify DLL exports**

```bash
llvm-objdump -p build/windows/x86_64/MoraRuntime.dll | grep -A 20 "Export Table"
```

Should show `SKSEPlugin_Load` and `SKSEPlugin_Version` exports.

- [ ] **Step 3: Verify Linux build still works**

```bash
xmake f -p linux
xmake build
xmake test
```

All 26+ tests must still pass.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "feat: cross-compiled SKSE runtime DLL verified"
```

---

### Task 7: Integration Test — Deploy to Skyrim

**Files:**
- Create: `scripts/deploy_runtime.sh`

A deployment script that copies the built DLL and cache files to the Skyrim installation for testing.

- [ ] **Step 1: Create deployment script**

```bash
#!/bin/bash
# Deploy Mora runtime to Skyrim for testing
SKYRIM_DATA="$HOME/.local/share/Steam/steamapps/common/Skyrim Special Edition/Data"

# Copy runtime DLL
mkdir -p "$SKYRIM_DATA/SKSE/Plugins"
cp build/windows/x86_64/MoraRuntime.dll "$SKYRIM_DATA/SKSE/Plugins/"

# Compile and copy cache
./build/linux/x86_64/release/mora compile test_data/example.mora
cp -r test_data/MoraCache "$SKYRIM_DATA/"

echo "Deployed to $SKYRIM_DATA"
echo "  SKSE/Plugins/MoraRuntime.dll"
echo "  MoraCache/mora.patch"
echo "  MoraCache/mora.rt"
echo "  MoraCache/mora.lock"
```

- [ ] **Step 2: Test in Skyrim (manual)**

Launch Skyrim SE via Steam/Proton with SKSE. Check the SKSE log for Mora output:
```
Mora runtime v0.1.0 loaded
Mora: DataLoaded — applying patches
Mora: Applied N patches to M forms in X.Xms
```

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "feat: deployment script for Skyrim testing"
```

---

## Summary

This plan delivers:
- **Cross-compile toolchain**: clang-cl + lld-link + xwin SDK targeting Windows x64 from Linux
- **Form Bridge**: Maps Mora FieldId/FieldOp/Value to CommonLibSSE API calls (keywords, spells, perks, damage, armor rating, name, value, weight)
- **Patch Applier**: Loads `.mora.patch` binary, applies field-level diffs to game forms at `DataLoaded`
- **Dynamic Runner**: Loads `.mora.rt`, registers for events (stub for Phase 1 — full evaluation later)
- **SKSE Plugin**: Entry point with version info, messaging listener, DataLoaded handler
- **Deployment script**: Copy DLL + cache to Skyrim for testing

**What this enables:**
- Pre-computed patches applied in milliseconds at game startup (vs. 5+ minutes with current patchers)
- Single DLL replacing dozens of distributor plugins
- Same Mora core code running on both Linux (compiler) and Windows (runtime)

**Deferred to future work:**
- Full bytecode VM for dynamic rule evaluation (the .mora.rt format is metadata-only in Phase 1)
- NPC cell-load event hooks for dynamic rules
- Hot-reload support (recompile while game is running)
- Stale cache fallback (interpret all rules at runtime when cache is outdated)

**Key risk:** CommonLibSSE may have MSVC-specific code that doesn't compile with clang-cl. Task 6 is where we discover and address any compatibility issues. The fallback is to build CommonLibSSE separately on a Windows machine and link against it.
