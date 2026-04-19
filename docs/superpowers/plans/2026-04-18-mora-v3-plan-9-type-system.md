# Plan 9 — Type system + sema shrink

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the compile-time `TypeKind`/`MoraType` scalar-type system with a process-singleton `TypeRegistry` of `const Type*` pointers. Physical types (`Int32`, `Int64`, `Float64`, `Bool`, `String`, `Keyword`, `Bytes`, `Any`) are built-in singletons. Nominal tags (Skyrim's `FormID`, `NpcID`, `WeaponID`, `KeywordID`, ...) are registered by extensions via `ExtensionContext::register_nominal_type`. Delete `src/sema/type_checker.cpp` and all `MoraType`/`TypeKind` references. Sema keeps name resolution, arity checks, and binding analysis only — scalar type errors become a runtime concern for a later plan.

**Architecture:** Types are identified by pointer to a singleton `Type` instance. Singletons have process lifetime and are never destroyed. Nominal types point to their physical type via `Type::physical()`; physical types return `this`. Relation column specs store a `const Type*` — no more strings that parallel the type. The `NameResolver` stops tracking per-fact parameter types (pure arity); the `TypeChecker` goes away entirely. Unused-variable warnings, the one non-type-checking job inside `type_checker.cpp`, are dropped in this plan (acknowledged behavior change; revisit if missed).

**Tech Stack:** C++20, xmake, gtest.

**Branch:** `mora-v3-foundation`
**Base:** `e55de55` (HEAD, Plan 8 + polish)

**Scope note.** This plan does NOT introduce typed Vectors (`Int32Vector`, `StringVector`, etc.), `Column`s, or columnar FactDB. That's Plan 10. This plan only introduces the `Type` identity system and rewires the sema/schema plumbing onto it. The FactDB stays tuple-based.

**Behavior change called out.** Unused-variable warnings and scalar-type mismatch diagnostics (e.g. adding a string to an int) disappear in this plan. They relied on the TypeChecker's inference pass. Type errors will surface at runtime in a later plan when the vectorized evaluator performs nominal-tag checks at chunk boundaries. Document this in each milestone's commit body.

---

## Milestone 1 — Core type system + ExtensionContext::register_nominal_type

New primitive: the `Type` identity system. One commit. No behavior change yet — MoraType/TypeKind still exist in parallel.

### Task 1.1: Create `include/mora/core/type.h`

**Files:**
- Create: `include/mora/core/type.h`

- [ ] **Step 1: Write the header**

```cpp
#pragma once

#include <cstddef>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mora {

// A Type is a process-lifetime singleton identified by pointer.
// Physical types return themselves from `physical()`; nominal types
// return their underlying physical type. Types are never destroyed.
class Type {
public:
    virtual ~Type()                          = default;
    virtual std::string_view name() const    = 0;
    virtual const Type*      physical() const = 0;  // self for physical; underlying for nominal
    virtual size_t           element_bytes() const = 0;

    bool is_nominal() const { return physical() != this; }

protected:
    Type() = default;
};

// Built-in physical type singletons. Accessed via free functions to
// keep the Type class definition minimal.
namespace types {
    const Type* int32();
    const Type* int64();
    const Type* float64();
    const Type* boolean();
    const Type* string();    // interned StringId
    const Type* keyword();   // interned StringId with keyword sigil
    const Type* bytes();
    const Type* any();

    // Lookup by name. Physical names ("Int32", "String", ...) and any
    // nominal names previously registered via TypeRegistry are visible.
    // Returns nullptr on miss.
    const Type* get(std::string_view name);
}

// Process-singleton registry of nominal types. Extensions register
// domain-specific tags (e.g. "FormID") layered over a physical type.
// Registration is idempotent: registering the same name twice with
// the same physical returns the same pointer.
class TypeRegistry {
public:
    static TypeRegistry& instance();

    const Type* register_nominal(std::string_view name, const Type* physical);
    const Type* find(std::string_view name) const;

    TypeRegistry(const TypeRegistry&)            = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;

private:
    TypeRegistry();
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, const Type*> by_name_;  // physical + nominal
};

} // namespace mora
```

- [ ] **Step 2: Build just the header**

```
xmake build mora_lib 2>&1 | tail -5
```

Expected: succeeds (header-only add, nothing references it yet).

### Task 1.2: Implement `src/core/type.cpp`

**Files:**
- Create: `src/core/type.cpp`

- [ ] **Step 1: Write the implementation**

```cpp
#include "mora/core/type.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace mora {
namespace {

template <const char* Name, size_t ElemBytes>
class PhysicalType : public Type {
public:
    std::string_view name() const override { return Name; }
    const Type*      physical() const override { return this; }
    size_t           element_bytes() const override { return ElemBytes; }
};

constexpr char kInt32Name[]   = "Int32";
constexpr char kInt64Name[]   = "Int64";
constexpr char kFloat64Name[] = "Float64";
constexpr char kBoolName[]    = "Bool";
constexpr char kStringName[]  = "String";
constexpr char kKeywordName[] = "Keyword";
constexpr char kBytesName[]   = "Bytes";
constexpr char kAnyName[]     = "Any";

using Int32Type_   = PhysicalType<kInt32Name,   sizeof(int32_t)>;
using Int64Type_   = PhysicalType<kInt64Name,   sizeof(int64_t)>;
using Float64Type_ = PhysicalType<kFloat64Name, sizeof(double)>;
using BoolType_    = PhysicalType<kBoolName,    sizeof(bool)>;
using StringType_  = PhysicalType<kStringName,  sizeof(uint32_t)>;
using KeywordType_ = PhysicalType<kKeywordName, sizeof(uint32_t)>;
using BytesType_   = PhysicalType<kBytesName,   0>;   // variable-size
using AnyType_     = PhysicalType<kAnyName,     0>;   // polymorphic

class NominalType : public Type {
public:
    NominalType(std::string n, const Type* physical)
        : name_(std::move(n)), physical_(physical) {}
    std::string_view name() const override { return name_; }
    const Type*      physical() const override { return physical_; }
    size_t           element_bytes() const override {
        return physical_->element_bytes();
    }
private:
    std::string name_;
    const Type* physical_;
};

} // namespace

namespace types {

#define DEFINE_PHYSICAL(name, Class)                       \
    const Type* name() {                                   \
        static const Class instance;                       \
        return &instance;                                  \
    }

DEFINE_PHYSICAL(int32,   Int32Type_)
DEFINE_PHYSICAL(int64,   Int64Type_)
DEFINE_PHYSICAL(float64, Float64Type_)
DEFINE_PHYSICAL(boolean, BoolType_)
DEFINE_PHYSICAL(string,  StringType_)
DEFINE_PHYSICAL(keyword, KeywordType_)
DEFINE_PHYSICAL(bytes,   BytesType_)
DEFINE_PHYSICAL(any,     AnyType_)

#undef DEFINE_PHYSICAL

const Type* get(std::string_view name) {
    return TypeRegistry::instance().find(name);
}

} // namespace types

TypeRegistry::TypeRegistry() {
    by_name_.emplace(std::string(types::int32()->name()),   types::int32());
    by_name_.emplace(std::string(types::int64()->name()),   types::int64());
    by_name_.emplace(std::string(types::float64()->name()), types::float64());
    by_name_.emplace(std::string(types::boolean()->name()), types::boolean());
    by_name_.emplace(std::string(types::string()->name()),  types::string());
    by_name_.emplace(std::string(types::keyword()->name()), types::keyword());
    by_name_.emplace(std::string(types::bytes()->name()),   types::bytes());
    by_name_.emplace(std::string(types::any()->name()),     types::any());
}

TypeRegistry& TypeRegistry::instance() {
    static TypeRegistry inst;
    return inst;
}

const Type* TypeRegistry::register_nominal(std::string_view name,
                                             const Type* physical) {
    std::string const key(name);
    {
        std::shared_lock<std::shared_mutex> r(mu_);
        auto it = by_name_.find(key);
        if (it != by_name_.end()) {
            // Idempotent when physical matches; otherwise return existing
            // (do NOT overwrite; callers that race on names must agree).
            return it->second;
        }
    }
    std::unique_lock<std::shared_mutex> w(mu_);
    auto it = by_name_.find(key);
    if (it != by_name_.end()) return it->second;
    auto* t = new NominalType(key, physical);
    by_name_.emplace(key, t);
    return t;
}

const Type* TypeRegistry::find(std::string_view name) const {
    std::string const key(name);
    std::shared_lock<std::shared_mutex> r(mu_);
    auto it = by_name_.find(key);
    return it == by_name_.end() ? nullptr : it->second;
}

} // namespace mora
```

- [ ] **Step 2: Build**

```
xmake build mora_lib 2>&1 | tail -10
```

Expected: succeeds. `src/core/type.cpp` picked up by the existing `src/core/*.cpp` glob in `xmake.lua:173`.

### Task 1.3: Write core type tests

**Files:**
- Create: `tests/core/test_type.cpp` (note: `test_*.cpp` prefix — required by the `tests/**/test_*.cpp` glob at `xmake.lua:267`)

- [ ] **Step 1: Write the test**

```cpp
#include "mora/core/type.h"

#include <gtest/gtest.h>

namespace {

TEST(CoreType, PhysicalSingletonsAreStable) {
    EXPECT_EQ(mora::types::int32(),   mora::types::int32());
    EXPECT_EQ(mora::types::string(),  mora::types::string());
    EXPECT_NE(mora::types::string(),  mora::types::keyword());
    EXPECT_NE(mora::types::int32(),   mora::types::int64());
}

TEST(CoreType, PhysicalNamesAndShape) {
    EXPECT_EQ(mora::types::int32()->name(),   "Int32");
    EXPECT_EQ(mora::types::keyword()->name(), "Keyword");
    EXPECT_FALSE(mora::types::int32()->is_nominal());
    EXPECT_EQ(mora::types::int32()->physical(), mora::types::int32());
}

TEST(CoreType, TypeRegistryFindsPhysicals) {
    EXPECT_EQ(mora::types::get("Int32"),   mora::types::int32());
    EXPECT_EQ(mora::types::get("String"),  mora::types::string());
    EXPECT_EQ(mora::types::get("Keyword"), mora::types::keyword());
    EXPECT_EQ(mora::types::get("NotAType"), nullptr);
}

TEST(CoreType, RegisterNominalIsIdempotent) {
    auto& reg = mora::TypeRegistry::instance();
    auto const* a = reg.register_nominal("PlanNineTestTag", mora::types::int32());
    auto const* b = reg.register_nominal("PlanNineTestTag", mora::types::int32());
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a, b);
    EXPECT_TRUE(a->is_nominal());
    EXPECT_EQ(a->physical(), mora::types::int32());
    EXPECT_EQ(a->name(), "PlanNineTestTag");
    EXPECT_EQ(mora::types::get("PlanNineTestTag"), a);
}

} // namespace
```

- [ ] **Step 2: Build and run**

```
xmake build 2>&1 | tail -5
xmake run test_type 2>&1 | tail -15
```

Expected: 4 tests pass. (Test name uses a distinctive prefix so it doesn't clash with nominal names registered by Skyrim in later milestones — the registry is process-wide.)

### Task 1.4: Add `register_nominal_type` to `ExtensionContext`

**Files:**
- Modify: `include/mora/ext/extension.h`
- Modify: `src/ext/extension.cpp`
- Delete: `include/mora/ext/nominal_type.h` (the 12-line stub; consumers switch to `mora/core/type.h`)

- [ ] **Step 1: Update the header**

In `include/mora/ext/extension.h`, add forward-include near the top:

```cpp
#include "mora/core/type.h"
```

Add a public method in the `ExtensionContext` class body (near `register_relation`):

```cpp
// Register a nominal type tag (e.g. "FormID") layered over a physical
// type. Returns a stable singleton pointer. Idempotent: registering
// the same name twice returns the same pointer.
const Type* register_nominal_type(std::string_view name,
                                   const Type* physical);
```

Remove any remaining `#include "mora/ext/nominal_type.h"` anywhere in the repo (`grep -rn 'ext/nominal_type' src extensions tests include`).

- [ ] **Step 2: Implement the method**

In `src/ext/extension.cpp`, inside the class:

```cpp
const Type* ExtensionContext::register_nominal_type(std::string_view name,
                                                     const Type* physical) {
    return TypeRegistry::instance().register_nominal(name, physical);
}
```

- [ ] **Step 3: Delete the stub header**

```bash
rm include/mora/ext/nominal_type.h
```

- [ ] **Step 4: Build**

```
xmake build 2>&1 | tail -10
```

Expected: succeeds. No call sites exist yet — we're just exposing the API.

### Task 1.5: M1 commit

- [ ] **Step 1: Build + test green**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -5
```

Expected: all tests pass, 83 total (79 from Plan 8 + 4 new core type tests).

- [ ] **Step 2: Commit**

```bash
git add include/mora/core/type.h src/core/type.cpp \
        include/mora/ext/extension.h src/ext/extension.cpp \
        tests/core/test_type.cpp
git rm include/mora/ext/nominal_type.h
git commit -m "$(cat <<'EOF'
mora v3: introduce core type system + ExtensionContext::register_nominal_type

Add `include/mora/core/type.h` — a process-singleton Type identity
system. Physical types (Int32, Int64, Float64, Bool, String, Keyword,
Bytes, Any) are accessed via `mora::types::int32()` etc. A process-
singleton `TypeRegistry` manages nominal tags layered over physicals;
extensions register domain-specific nominals via
`ExtensionContext::register_nominal_type(name, physical)`.

No existing code path uses the new system yet — Plan 9 M2 migrates
the schema/resolver plumbing, and M3 deletes the old TypeKind/MoraType
machinery.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Milestone 2 — Migrate schemas + resolver to `const Type*`; register Skyrim nominals

Replace `MoraType` with `const Type*` across the schema/resolver pipeline. Skyrim's nominal tags become first-class. `MoraType`/`TypeKind` definitions survive this milestone but have no remaining callers once M2 lands (final removal is in M3 Task 3.1).

### Task 2.1: Map out every `MoraType` and `TypeKind` use site

**Files (read-only inventory):**
- `include/mora/ast/types.h` (`TypeKind`, `MoraType`)
- `src/ast/types.cpp`
- `include/mora/data/schema_registry.h` (`column_types: vector<MoraType>`)
- `src/data/schema_registry.cpp`
- `include/mora/sema/name_resolver.h` (`FactSignature.param_types: vector<MoraType>`)
- `src/sema/name_resolver.cpp`
- `include/mora/data/form_model.h` (form def tables use `TypeKind`)
- `extensions/skyrim_compile/src/register.cpp` (`to_ext_schema`)
- `tests/schema_registry_test.cpp`, `tests/name_resolver_test.cpp`

- [ ] **Step 1: Confirm the inventory**

```
grep -rln 'MoraType\|TypeKind' src include extensions tests
```

Expected output: the files listed above (plus `src/sema/type_checker.cpp` and `tests/type_checker_test.cpp`, which M3 deletes). If something else appears, add it to the list and handle it.

### Task 2.2: Replace `TypeKind` in `form_model.h` with type-name strings

**Files:**
- Modify: `include/mora/data/form_model.h`

`form_model.h` is compile-time constexpr data describing Skyrim form types. Today it stores `TypeKind type_kind;` per definition. Swap to `std::string_view type_name;` — the registration code resolves the string to a `const Type*` at runtime.

- [ ] **Step 1: Change FormTypeDef + ExistenceOnlyDef field types**

For each struct that has `TypeKind type_kind;`, change to:

```cpp
std::string_view type_name;   // e.g. "FormID", "NpcID"; resolved at registration time
```

For each literal that spells `TypeKind::Foo`, change to the string literal `"Foo"` — e.g. `TypeKind::FormID` → `"FormID"`, `TypeKind::NpcID` → `"NpcID"`, etc. Do this for all 12 nominals (FormID, NpcID, WeaponID, ArmorID, KeywordID, FactionID, SpellID, PerkID, QuestID, LocationID, CellID, RaceID) and any physical-type references (`TypeKind::Int` → `"Int64"` since Mora's old `Int` was a 64-bit integer; verify in `src/ast/types.cpp` what `Int` mapped to before committing).

Drop the `#include "mora/ast/types.h"` at the top of form_model.h.

- [ ] **Step 2: Build the skyrim extension**

```
xmake build mora_skyrim_compile 2>&1 | tail -20
```

Expected: failures in `schema_registry.cpp` and elsewhere that still read `form_model`'s `type_kind`. Task 2.3 fixes those.

### Task 2.3: `schema_registry.cpp` + `schema_registry.h` — swap `MoraType` for `const Type*`

**Files:**
- Modify: `include/mora/data/schema_registry.h`
- Modify: `src/data/schema_registry.cpp`

- [ ] **Step 1: Update the header**

Change:

```cpp
struct RelationSchema {
    StringId                name;
    std::vector<MoraType>   column_types;
    std::vector<size_t>     indexed_columns;
    std::vector<EspSource>  esp_sources;
};
```

to:

```cpp
struct RelationSchema {
    StringId                name;
    std::vector<const Type*> column_types;   // each points to a Type singleton
    std::vector<size_t>     indexed_columns;
    std::vector<EspSource>  esp_sources;
};
```

Add `#include "mora/core/type.h"` and remove `#include "mora/ast/types.h"`.

- [ ] **Step 2: Update every call site in `schema_registry.cpp`**

Replace every `MoraType::make(TypeKind::Foo)` (or equivalent factory) with `mora::types::get("Foo")`. The `get` function returns `nullptr` for unknown names; for known physicals it's fine. For nominals registered later (Task 2.5), the string lookup must succeed at registration time — ensure `register_all_nominal_types(ctx)` runs before `register_defaults()` in `register_skyrim` (Task 2.7 handles ordering).

List field (`TypeKind::List`) had element-type info in MoraType. Since there's no longer a list-wrapper type, decide: either (a) store the element `const Type*` in the RelationSchema metadata (not column_types), or (b) drop list-typing in sema altogether and treat list relations as just another arity — preferred because sema no longer does scalar typing. Go with (b): when you encounter `TypeKind::List`, use `mora::types::any()` as the column type.

- [ ] **Step 3: Build**

```
xmake build mora_lib 2>&1 | tail -10
xmake build mora_skyrim_compile 2>&1 | tail -10
```

Expected: `mora_lib` succeeds. `mora_skyrim_compile` may still fail if `register.cpp` depends on `MoraType` fields — that's Task 2.5.

### Task 2.4: `name_resolver` — drop `param_types`; keep arity only

**Files:**
- Modify: `include/mora/sema/name_resolver.h`
- Modify: `src/sema/name_resolver.cpp`

The NameResolver's `FactSignature` currently stores `std::vector<MoraType> param_types;` — used only by `type_checker.cpp` which we're deleting in M3. Replace with `size_t arity;`.

- [ ] **Step 1: Update `FactSignature`**

Change the struct to:

```cpp
struct FactSignature {
    StringId name;
    size_t   arity;
    bool     is_builtin = false;
};
```

Drop the `#include "mora/ast/types.h"` from the header.

- [ ] **Step 2: Update `register_builtins()` and population sites**

Wherever NameResolver constructed a `FactSignature` with `param_types = {...}`, change to `arity = N` where N was the length of the old vector. Built-ins (the bundled skyrim relations like `npc`, `has_keyword`, ...) go from listing their types to just stating their arity.

- [ ] **Step 3: Update places that read param_types**

Grep for `.param_types` and replace. Callers that walked the vector to check element-wise type compatibility just drop that logic — sema no longer checks scalar types. Callers that used `.size()` switch to `.arity`.

- [ ] **Step 4: Build**

```
xmake build mora_lib 2>&1 | tail -15
```

Expected: `mora_lib` builds clean. Any remaining errors are in `type_checker.cpp` (deleted in M3) — for now, surgically comment out the minimum to make `type_checker.cpp` compile OR just let it fail; M3 deletes it.

Preferred path: **don't patch `type_checker.cpp` here**. Instead, temporarily exclude it from the build in `xmake.lua` for the duration of M2:

Change the `mora_lib` `add_files` line in `xmake.lua:173-181` from:

```lua
add_files("..., src/sema/*.cpp, ...")
```

to:

```lua
add_files("..., src/sema/*.cpp|type_checker.cpp, ...")
```

(xmake's `|` is an exclusion operator in its glob syntax.) M3 re-enables the full `src/sema/*.cpp` glob — after actually deleting the file.

Same for `tests/type_checker_test.cpp` — exclude it in `xmake.lua` for M2, remove in M3. `grep -n type_checker_test xmake.lua` to find the glob; add an `|type_checker_test.cpp` exclusion.

### Task 2.5: Attach `const Type*` to `ColumnSpec`

**Files:**
- Modify: `include/mora/ext/relation_schema.h`

- [ ] **Step 1: Add the field**

Update `ColumnSpec`:

```cpp
struct ColumnSpec {
    std::string name;
    bool        indexed    = false;
    const Type* type       = nullptr;   // nominal or physical singleton; nullptr = legacy Plan 4 schema
};
```

Add `#include "mora/core/type.h"` at the top.

- [ ] **Step 2: Build**

```
xmake build mora_lib 2>&1 | tail -5
```

Expected: succeeds. Nothing populates `.type` yet; default `nullptr` preserves behavior.

### Task 2.6: Skyrim `types.cpp` — register nominal tags

**Files:**
- Create: `extensions/skyrim_compile/src/types.cpp`
- Create: `extensions/skyrim_compile/include/mora_skyrim_compile/types.h`

- [ ] **Step 1: Write the header**

```cpp
// extensions/skyrim_compile/include/mora_skyrim_compile/types.h
#pragma once

namespace mora::ext { class ExtensionContext; }

namespace mora_skyrim_compile {

// Register all Skyrim nominal type tags (FormID, NpcID, WeaponID, ...)
// with the given ExtensionContext. Must be called before
// register_defaults so the schema-building code can resolve type names.
void register_all_nominal_types(mora::ext::ExtensionContext& ctx);

} // namespace mora_skyrim_compile
```

- [ ] **Step 2: Write the implementation**

```cpp
// extensions/skyrim_compile/src/types.cpp
#include "mora_skyrim_compile/types.h"

#include "mora/core/type.h"
#include "mora/ext/extension.h"

namespace mora_skyrim_compile {

void register_all_nominal_types(mora::ext::ExtensionContext& ctx) {
    // FormID is the base nominal; per-form-type subtypes layer over the
    // same physical (Int32). A later plan can formalize nominal
    // subtyping if needed; for now each is a peer alias.
    auto const* i32 = mora::types::int32();

    ctx.register_nominal_type("FormID",     i32);
    ctx.register_nominal_type("NpcID",      i32);
    ctx.register_nominal_type("WeaponID",   i32);
    ctx.register_nominal_type("ArmorID",    i32);
    ctx.register_nominal_type("KeywordID",  i32);
    ctx.register_nominal_type("FactionID",  i32);
    ctx.register_nominal_type("SpellID",    i32);
    ctx.register_nominal_type("PerkID",     i32);
    ctx.register_nominal_type("QuestID",    i32);
    ctx.register_nominal_type("LocationID", i32);
    ctx.register_nominal_type("CellID",     i32);
    ctx.register_nominal_type("RaceID",     i32);
}

} // namespace mora_skyrim_compile
```

- [ ] **Step 3: Build**

```
xmake build mora_skyrim_compile 2>&1 | tail -5
```

Expected: succeeds.

### Task 2.7: Wire registration order in `register_skyrim`

**Files:**
- Modify: `extensions/skyrim_compile/src/register.cpp`

- [ ] **Step 1: Call `register_all_nominal_types` first**

At the top of `register_skyrim(ExtensionContext& ctx)`, add:

```cpp
#include "mora_skyrim_compile/types.h"
```

And as the first action:

```cpp
void register_skyrim(mora::ext::ExtensionContext& ctx) {
    register_all_nominal_types(ctx);      // must run first — schemas reference these by name

    // (existing body continues: bridge pool, register_defaults, to_ext_schema conversion, ...)
```

- [ ] **Step 2: Update `to_ext_schema`**

`to_ext_schema` (around `extensions/skyrim_compile/src/register.cpp:22-36` per investigation) currently discards type info. Update it to populate `ColumnSpec.type`:

```cpp
static mora::ext::RelationSchema to_ext_schema(
    const mora::RelationSchema& core,
    const mora::StringPool& pool)
{
    mora::ext::RelationSchema ext;
    ext.name      = std::string(pool.get(core.name));
    ext.is_output = /* existing: match on name or read from core */;

    for (size_t i = 0; i < core.column_types.size(); ++i) {
        mora::ext::ColumnSpec cs;
        cs.name    = "col" + std::to_string(i);
        cs.indexed = std::find(core.indexed_columns.begin(),
                                core.indexed_columns.end(), i)
                     != core.indexed_columns.end();
        cs.type    = core.column_types[i];   // const Type* singleton
        ext.columns.push_back(std::move(cs));
    }
    return ext;
}
```

- [ ] **Step 3: Build**

```
xmake build 2>&1 | tail -10
```

Expected: succeeds except tests that still reference `MoraType`/`TypeKind` (Task 2.8).

### Task 2.8: Update `tests/schema_registry_test.cpp` + `tests/name_resolver_test.cpp`

**Files:**
- Modify: `tests/schema_registry_test.cpp`
- Modify: `tests/name_resolver_test.cpp`

- [ ] **Step 1: Migrate assertions**

In `schema_registry_test.cpp`, assertions like `EXPECT_EQ(schema.column_types[0].kind, TypeKind::FormID)` become:

```cpp
EXPECT_EQ(schema.column_types[0], mora::types::get("FormID"));
```

or, for shape checks:

```cpp
EXPECT_EQ(schema.column_types[0]->name(),     "FormID");
EXPECT_EQ(schema.column_types[0]->physical(), mora::types::int32());
```

In `name_resolver_test.cpp`, assertions on `signature.param_types.size()` become `signature.arity`. Any assertion on `param_types[i].kind` is deleted — that information no longer exists.

Add `#include "mora/core/type.h"` where needed.

- [ ] **Step 2: Build + run these two test targets**

```
xmake build 2>&1 | tail -5
xmake run schema_registry_test && xmake run name_resolver_test 2>&1 | tail -20
```

Expected: schema + name-resolver tests pass.

### Task 2.9: M2 commit

- [ ] **Step 1: Full build + test**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -10
```

Expected: all tests pass except those inside `tests/type_checker_test.cpp` which are still excluded via the xmake glob exclusion from Task 2.4.

- [ ] **Step 2: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: migrate schemas + resolver to const Type*; register Skyrim nominals

RelationSchema.column_types is now vector<const Type*> pointing to
TypeRegistry singletons. form_model.h's TypeKind fields become string
type-names resolved at registration time. NameResolver's FactSignature
drops param_types in favor of arity only — sema no longer tracks
scalar types.

Skyrim nominals (FormID + 11 subtypes) register via the new
extensions/skyrim_compile/src/types.cpp, which runs first in
register_skyrim so schema-building code can resolve type names.

ColumnSpec gains a `const Type* type` field populated by to_ext_schema.

type_checker.cpp + type_checker_test.cpp are temporarily excluded
from the xmake build; Plan 9 M3 deletes them.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Milestone 3 — Delete the type checker; retire `TypeKind`/`MoraType`

Surgical deletion. All call sites already migrated in M2.

### Task 3.1: Delete type_checker + types.h/cpp

**Files deleted:**
- `src/sema/type_checker.cpp`
- `include/mora/sema/type_checker.h`
- `include/mora/ast/types.h`
- `src/ast/types.cpp`
- `tests/type_checker_test.cpp`

**Modified:**
- `xmake.lua` — remove the `|type_checker.cpp` and `|type_checker_test.cpp` exclusions added in M2.

- [ ] **Step 1: Delete the files**

```bash
rm src/sema/type_checker.cpp include/mora/sema/type_checker.h
rm include/mora/ast/types.h src/ast/types.cpp
rm tests/type_checker_test.cpp
```

- [ ] **Step 2: Restore clean xmake globs**

Revert the two `|type_checker*` exclusions in `xmake.lua` (from M2 Task 2.4) back to plain globs.

- [ ] **Step 3: Remove TypeChecker from `main.cpp`**

Find `run_check_pipeline` (around `src/main.cpp:212`). Delete:

```cpp
mora::TypeChecker checker(result.pool, result.diags, resolver);
for (auto& mod : result.modules) checker.check(mod);
```

And the `#include "mora/sema/type_checker.h"` at the top. Sema after this point is: parse → resolve.

- [ ] **Step 4: Scrub remaining includes**

```
grep -rn 'ast/types.h\|sema/type_checker.h\|MoraType\|TypeKind' src include extensions tests
```

Expected output: empty. Fix anything that remains. (LSP's `hover.cpp` reads `.column_types.size()` — that call survives intact because the new `column_types` is still a vector.)

### Task 3.2: LSP hover — verify it still works

**Files:**
- Read-only check: `src/lsp/handlers/hover.cpp`

- [ ] **Step 1: Verify**

```
grep -n 'column_types' src/lsp/handlers/hover.cpp
```

Expected: one hit reading `.size()`. That call is valid on `vector<const Type*>` as well.

- [ ] **Step 2: Optionally display type names in hover**

If you want to improve the hover text to show actual type names (e.g. "Columns: FormID, Keyword, Any"), that's a nice touch — but scope-creep for Plan 9. Defer. Note in commit body.

### Task 3.3: Update `tests/name_resolver_test.cpp` if still broken

- [ ] **Step 1: Final test fix-ups**

```
xmake build 2>&1 | tail -10
xmake test 2>&1 | tail -20
```

Expected: all tests pass. If anything breaks, it's a call site missed in the grep in Task 3.1 Step 4 — fix + re-run.

### Task 3.4: CLI smoke

- [ ] **Step 1: Run on `test_data/minimal`**

```
xmake run mora -- compile test_data/minimal --output-dir /tmp/mora-p9-smoke --sink parquet.snapshot=/tmp/mora-p9-smoke/out 2>&1 | tail -20
echo "exit: $?"
ls /tmp/mora-p9-smoke/
```

Expected: exit 0, parquet files present.

### Task 3.5: M3 commit

- [ ] **Step 1: Full build + test**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -10
```

Expected: all tests pass. Total test count is ~82 (plan 8's 79 + new core type tests (4) - type_checker's ~6).

- [ ] **Step 2: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: delete TypeChecker + MoraType/TypeKind — sema is name+arity only

Removes src/sema/type_checker.cpp (627 LOC), include/mora/sema/type_checker.h,
include/mora/ast/types.h (TypeKind enum), src/ast/types.cpp (MoraType),
and tests/type_checker_test.cpp. TypeChecker instantiation in
run_check_pipeline is gone — sema after this commit is just parse →
resolve.

Behavior changes:
- Unused-variable warnings dropped (they lived in TypeChecker).
- Scalar type mismatch diagnostics at sema time dropped. Type errors
  will surface at runtime once the vectorized evaluator lands (Plan 11+)
  and performs nominal-tag checks at chunk boundaries.

xmake globs for src/sema/*.cpp and tests/*_test.cpp restored — no more
per-file exclusions.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Verification (end of Plan 9)

1. `grep -rn 'MoraType\|TypeKind' src include extensions tests` empty.
2. `grep -rn 'ast/types.h\|sema/type_checker' src include extensions tests` empty.
3. `xmake test` — all test binaries pass.
4. CLI smoke on `test_data/minimal` produces parquet files.
5. Branch `mora-v3-foundation` is at 22 commits above master (19 previous + 3 M1/M2/M3 commits).
6. `include/mora/core/type.h` + `src/core/type.cpp` exist; `TypeRegistry::instance().find("FormID")` returns a valid nominal pointer once Skyrim has registered.

## Explicitly NOT in scope (for later plans)

- Typed `Vector` + `Column` + chunked FactDB (Plan 10)
- Vectorized evaluator rewrite (Plan 11)
- Nominal-tag checks at chunk boundaries (Plan 11, part of vectorized eval)
- Drop verb keywords from grammar (Plan 12)
- Structural conversion machinery (Int32 → Int64 → Float64) (Plan 10 or 11)
- Bring back unused-variable warnings in a lighter form (optional polish plan)

## Critical files

- `/home/tbaldrid/oss/mora/include/mora/core/type.h` — **new**. The type identity system.
- `/home/tbaldrid/oss/mora/src/core/type.cpp` — **new**.
- `/home/tbaldrid/oss/mora/include/mora/ext/extension.h` — **modified**. `register_nominal_type` public API.
- `/home/tbaldrid/oss/mora/src/data/schema_registry.cpp` — **modified**. MoraType → const Type*.
- `/home/tbaldrid/oss/mora/include/mora/data/form_model.h` — **modified**. TypeKind literals → strings.
- `/home/tbaldrid/oss/mora/src/sema/name_resolver.cpp` — **modified**. FactSignature arity-only.
- `/home/tbaldrid/oss/mora/extensions/skyrim_compile/src/types.cpp` — **new**. Nominal registrations.
- `/home/tbaldrid/oss/mora/extensions/skyrim_compile/src/register.cpp` — **modified**. Registration order + to_ext_schema type population.
- `/home/tbaldrid/oss/mora/src/sema/type_checker.cpp` — **deleted** in M3.
- `/home/tbaldrid/oss/mora/include/mora/ast/types.h` — **deleted** in M3.
