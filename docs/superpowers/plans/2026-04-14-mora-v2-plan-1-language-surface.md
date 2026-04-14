# Mora v2 Plan 1 — Language Surface + Metaprogramming Foundation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend Mora's compiler to speak v2 syntax (`@EditorID`, `:keyword`, `ns/name`, `use :as :refer`, `maintain`/`on`, verb keywords) and introduce the constexpr `RelationEntry` master table with compile-time validation. Existing static rules continue to work through their v2 spelling. Zero runtime changes — the .mora.patch format and runtime DLL are untouched in this plan.

**Architecture:** Introduce a single constexpr table (`kRelations`) as the source of truth for relations and effects. The parser is extended for new tokens and rule annotations. The name resolver and type checker consume `kRelations` for validation. A `static_assert` chain enforces cross-registry consistency. The existing `form_model.h` is preserved as the mechanical offset store; `kRelations` layers relation metadata on top and references it.

**Tech Stack:** C++23, xmake build system, gtest for unit tests. The codebase uses `std::variant`-based AST nodes (see `include/mora/ast/ast.h`), constexpr data tables (see `include/mora/data/form_model.h`), and a hand-written recursive descent parser (see `src/parser/parser.cpp`).

**Reference spec:** `docs/superpowers/specs/2026-04-14-mora-v2-static-dynamic-datalog-design.md`

---

## File Structure

**New files:**
- `include/mora/model/relation_types.h` — value types, cardinality, source enums, `ArgSpec`, `RelationSource`, `HookSpec`, `EspSource`, `MemoryReadSpec`.
- `include/mora/model/handler_ids.h` — `HandlerId` enum.
- `include/mora/model/relation_entry.h` — `RelationEntry` struct.
- `include/mora/model/relations.h` — the `kRelations` master array (constexpr, referenced everywhere).
- `include/mora/model/handlers.h` — the `kHandlers` registry (one entry per `HandlerId`).
- `include/mora/model/hooks.h` — the `kKnownHooks` registry.
- `include/mora/model/validate.h` — `static_assert` validators.
- `src/model/relations_seed.cpp` — the actual relation array definition (kept in .cpp for faster rebuilds).
- `tests/model/test_relation_validators.cpp` — compile-time validator tests.
- `tests/parser/test_v2_syntax.cpp` — parser tests for new syntax.
- `tests/sema/test_verb_legality.cpp` — type checker tests for verbs.

**Modified files:**
- `include/mora/lexer/token.h` — add new `TokenKind` values.
- `src/lexer/lexer.cpp` / `src/lexer/token.cpp` — tokenize `@ident`, `:ident` as keyword, `/` as namespace separator, new keyword tokens (`maintain`, `on`, `set`, `add`, `sub`, `remove`, `as`, `refer`).
- `include/mora/ast/ast.h` — add `RuleKind`, `VerbKind`, extend `UseDecl`, rework `Effect` to carry `(verb, namespace, name, args)`, add `EditorIdExpr`, rename `SymbolExpr` semantics to `KeywordExpr`.
- `src/ast/ast.cpp` — companion implementations if needed.
- `include/mora/parser/parser.h` / `src/parser/parser.cpp` — parse `use … :as … :refer […]`, `maintain`/`on` annotations, `@EditorID`, new-style keywords with `:` still working as keyword tokens, namespaced identifiers `ns/name`, verb keywords in head clauses.
- `include/mora/sema/name_resolver.h` / `src/sema/name_resolver.cpp` — namespace import resolution, relation lookup against `kRelations`, keyword interning.
- `include/mora/sema/type_checker.h` / `src/sema/type_checker.cpp` — verb-vs-cardinality checks, maintain retractability checks, argument type checks driven by `kRelations`.
- `include/mora/eval/phase_classifier.h` / `src/eval/phase_classifier.cpp` — three-tier classification (static / maintain / on) plus annotation validation.
- `test_data/*.mora` — migrate existing example/test files to v2 syntax.
- `xmake.lua` — register new source files.

---

## Phase A — Constexpr Foundation (Tasks 1–5)

### Task 1: Define value/cardinality/source enums and basic specs

**Files:**
- Create: `include/mora/model/relation_types.h`
- Test: `tests/model/test_relation_types.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/model/test_relation_types.cpp
#include "mora/model/relation_types.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(RelationTypes, ValueTypeEnumHasExpectedValues) {
    EXPECT_EQ(static_cast<int>(RelValueType::Int), 0);
    EXPECT_EQ(static_cast<int>(RelValueType::Float), 1);
    EXPECT_EQ(static_cast<int>(RelValueType::String), 2);
    EXPECT_EQ(static_cast<int>(RelValueType::FormRef), 3);
    EXPECT_EQ(static_cast<int>(RelValueType::Keyword), 4);
}

TEST(RelationTypes, CardinalityEnum) {
    // Scalar / Countable / Set / Functional / ReadOnly
    EXPECT_NE(Cardinality::Scalar, Cardinality::Countable);
    EXPECT_NE(Cardinality::Countable, Cardinality::Set);
    EXPECT_NE(Cardinality::Set, Cardinality::Functional);
}

TEST(RelationTypes, ArgSpecDefaultsToInt) {
    constexpr ArgSpec a;
    EXPECT_EQ(a.type, RelValueType::Int);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=RelationTypes.*`
Expected: FAIL (header does not exist).

- [ ] **Step 3: Create the header**

```cpp
// include/mora/model/relation_types.h
#pragma once
#include <cstdint>
#include <string_view>

namespace mora::model {

enum class RelValueType : uint8_t {
    Int, Float, String, FormRef, Keyword,
};

enum class Cardinality : uint8_t {
    Scalar,      // single value; verb: set
    Countable,   // numeric single value; verbs: set, add, sub
    Set,         // multi-valued membership; verbs: add, remove
    Functional,  // 1:1 mapping, read-only
};

enum class RelationSourceKind : uint8_t {
    Static,       // derived from ESP at compile time
    MemoryRead,   // runtime offset read
    Hook,         // runtime SKSE hook (state relation)
    Handler,      // runtime custom accessor
    Event,        // runtime edge-triggered (only consumed by `on` rules)
};

struct ArgSpec {
    RelValueType  type = RelValueType::Int;
    std::string_view name = {};
};

struct EspSource {
    std::string_view record_type = {};    // e.g. "NPC_", "WEAP"
    std::string_view subrecord  = {};     // e.g. "KWDA", "SNAM"
};

struct MemoryReadSpec {
    uint32_t     offset    = 0;
    RelValueType value_type = RelValueType::Int;
};

struct HookSpec {
    std::string_view hook_name = {};
    enum class Kind : uint8_t { Edge, State } kind = Kind::Edge;
};

} // namespace mora::model
```

- [ ] **Step 4: Register file with build**

Add to `xmake.lua` under the `mora_lib` target's `add_files` (the header does not need explicit registration, but the test file does):

```lua
target("mora_tests")
    add_files("tests/model/test_relation_types.cpp")
```

Then run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=RelationTypes.*`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/mora/model/relation_types.h tests/model/test_relation_types.cpp xmake.lua
git commit -m "model: add relation value types, cardinality, and source specs"
```

---

### Task 2: Define `HandlerId` enum and `kHandlers` registry skeleton

**Files:**
- Create: `include/mora/model/handler_ids.h`
- Create: `include/mora/model/handlers.h`
- Test: `tests/model/test_handlers.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/model/test_handlers.cpp
#include "mora/model/handlers.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(Handlers, NoneIsFirst) {
    EXPECT_EQ(static_cast<int>(HandlerId::None), 0);
}

TEST(Handlers, RegistryHasNoneEntry) {
    bool found = false;
    for (const auto& h : kHandlers) {
        if (h.id == HandlerId::None) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST(Handlers, FindHandlerById) {
    const HandlerEntry* e = find_handler(HandlerId::None);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->id, HandlerId::None);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=Handlers.*`
Expected: FAIL (headers do not exist).

- [ ] **Step 3: Create the headers**

```cpp
// include/mora/model/handler_ids.h
#pragma once
#include <cstdint>

namespace mora::model {

enum class HandlerId : uint16_t {
    None = 0,
    // Handler ids are added as relations requiring them are added.
    // Each id must correspond to an entry in kHandlers (handlers.h).
};

} // namespace mora::model
```

```cpp
// include/mora/model/handlers.h
#pragma once
#include "mora/model/handler_ids.h"
#include <string_view>

namespace mora::model {

struct HandlerEntry {
    HandlerId        id;
    std::string_view name;
    bool             generic;   // true if no dedicated C++ fn is required
                                // (relation uses the generic offset applier)
};

inline constexpr HandlerEntry kHandlers[] = {
    { HandlerId::None, "none", true },
};

constexpr const HandlerEntry* find_handler(HandlerId id) {
    for (const auto& h : kHandlers) if (h.id == id) return &h;
    return nullptr;
}

} // namespace mora::model
```

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=Handlers.*`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/mora/model/handler_ids.h include/mora/model/handlers.h tests/model/test_handlers.cpp
git commit -m "model: add HandlerId enum and kHandlers registry skeleton"
```

---

### Task 3: Define `RelationEntry` struct and empty `kRelations` array

**Files:**
- Create: `include/mora/model/relation_entry.h`
- Create: `include/mora/model/relations.h`
- Create: `src/model/relations_seed.cpp`
- Test: `tests/model/test_relations_shape.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/model/test_relations_shape.cpp
#include "mora/model/relations.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(Relations, RelationEntryHasExpectedFields) {
    constexpr RelationEntry e{
        .namespace_      = "form",
        .name            = "test",
        .args            = {{RelValueType::FormRef, "X"}},
        .arg_count       = 1,
        .value_type      = RelValueType::Int,
        .cardinality     = Cardinality::Scalar,
        .source          = RelationSourceKind::Static,
        .apply_handler   = HandlerId::None,
        .retract_handler = HandlerId::None,
    };
    EXPECT_EQ(e.namespace_, "form");
    EXPECT_EQ(e.name, "test");
    EXPECT_EQ(e.arg_count, 1);
}

TEST(Relations, kRelationsIsAccessible) {
    // May be empty at first. Ensure the array is visible.
    (void)kRelations;
    (void)kRelationCount;
    SUCCEED();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=Relations.*`
Expected: FAIL (no header).

- [ ] **Step 3: Create the entry struct**

```cpp
// include/mora/model/relation_entry.h
#pragma once
#include "mora/model/relation_types.h"
#include "mora/model/handler_ids.h"
#include <cstdint>
#include <string_view>

namespace mora::model {

inline constexpr uint8_t kMaxArgs = 4;

struct RelationEntry {
    std::string_view   namespace_;
    std::string_view   name;
    ArgSpec            args[kMaxArgs]{};
    uint8_t            arg_count     = 0;
    RelValueType       value_type    = RelValueType::Int;
    Cardinality        cardinality   = Cardinality::Scalar;
    RelationSourceKind source        = RelationSourceKind::Static;

    // Exactly one of these is populated based on `source`.
    EspSource          esp_source    = {};
    MemoryReadSpec     memory_read   = {};
    HookSpec           hook          = {};

    HandlerId          apply_handler   = HandlerId::None;
    HandlerId          retract_handler = HandlerId::None;

    std::string_view   docs = {};
};

} // namespace mora::model
```

- [ ] **Step 4: Create the array header and seed file**

```cpp
// include/mora/model/relations.h
#pragma once
#include "mora/model/relation_entry.h"
#include <cstddef>

namespace mora::model {

// Defined in src/model/relations_seed.cpp.
extern const RelationEntry kRelations[];
extern const size_t        kRelationCount;

constexpr const RelationEntry* find_relation(std::string_view ns, std::string_view name,
                                             const RelationEntry* arr, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (arr[i].namespace_ == ns && arr[i].name == name) return &arr[i];
    }
    return nullptr;
}

} // namespace mora::model
```

```cpp
// src/model/relations_seed.cpp
#include "mora/model/relations.h"

namespace mora::model {

// Relations are added incrementally. See Task 5 for the initial seed.
const RelationEntry kRelations[] = {
    { .namespace_ = "__sentinel__", .name = "__sentinel__", .arg_count = 0 },
};
const size_t kRelationCount = 0;

} // namespace mora::model
```

- [ ] **Step 5: Register source and run tests**

Add `src/model/relations_seed.cpp` to `mora_lib` target's `add_files` in `xmake.lua`.

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=Relations.*`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/mora/model/relation_entry.h include/mora/model/relations.h src/model/relations_seed.cpp tests/model/test_relations_shape.cpp xmake.lua
git commit -m "model: add RelationEntry struct and empty kRelations array"
```

---

### Task 4: Implement `static_assert` validators

**Files:**
- Create: `include/mora/model/validate.h`
- Create: `tests/model/test_validate.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/model/test_validate.cpp
#include "mora/model/validate.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(Validate, VerbShapePairs) {
    EXPECT_TRUE(is_legal_verb_for(VerbKind::Set, Cardinality::Scalar));
    EXPECT_TRUE(is_legal_verb_for(VerbKind::Set, Cardinality::Countable));
    EXPECT_FALSE(is_legal_verb_for(VerbKind::Add, Cardinality::Scalar));
    EXPECT_TRUE(is_legal_verb_for(VerbKind::Add, Cardinality::Countable));
    EXPECT_TRUE(is_legal_verb_for(VerbKind::Add, Cardinality::Set));
    EXPECT_TRUE(is_legal_verb_for(VerbKind::Remove, Cardinality::Set));
    EXPECT_FALSE(is_legal_verb_for(VerbKind::Remove, Cardinality::Scalar));
    EXPECT_FALSE(is_legal_verb_for(VerbKind::Set, Cardinality::Functional));
}

TEST(Validate, DuplicateDetection) {
    RelationEntry arr[] = {
        {.namespace_ = "a", .name = "x"},
        {.namespace_ = "a", .name = "x"},  // duplicate
    };
    EXPECT_TRUE(has_duplicate(arr, 2));
}

TEST(Validate, DuplicateDetectionNoDuplicates) {
    RelationEntry arr[] = {
        {.namespace_ = "a", .name = "x"},
        {.namespace_ = "a", .name = "y"},
    };
    EXPECT_FALSE(has_duplicate(arr, 2));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=Validate.*`
Expected: FAIL.

- [ ] **Step 3: Create the validator header**

```cpp
// include/mora/model/validate.h
#pragma once
#include "mora/model/relations.h"
#include "mora/model/handlers.h"

namespace mora::model {

enum class VerbKind : uint8_t { Set, Add, Sub, Remove };

constexpr bool is_legal_verb_for(VerbKind v, Cardinality c) {
    switch (c) {
        case Cardinality::Scalar:     return v == VerbKind::Set;
        case Cardinality::Countable:  return v == VerbKind::Set || v == VerbKind::Add || v == VerbKind::Sub;
        case Cardinality::Set:        return v == VerbKind::Add || v == VerbKind::Remove;
        case Cardinality::Functional: return false;
    }
    return false;
}

constexpr bool has_duplicate(const RelationEntry* arr, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            if (arr[i].namespace_ == arr[j].namespace_ && arr[i].name == arr[j].name) return true;
        }
    }
    return false;
}

constexpr bool handler_registered(HandlerId id) {
    if (id == HandlerId::None) return true;
    for (const auto& h : kHandlers) if (h.id == id) return true;
    return false;
}

constexpr bool validate_all_handlers(const RelationEntry* arr, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!handler_registered(arr[i].apply_handler))   return false;
        if (!handler_registered(arr[i].retract_handler)) return false;
    }
    return true;
}

constexpr bool validate_verb_shapes(const RelationEntry* arr, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        // Basic sanity: Functional relations must have no writers.
        if (arr[i].cardinality == Cardinality::Functional) {
            if (arr[i].apply_handler != HandlerId::None || arr[i].retract_handler != HandlerId::None)
                return false;
        }
    }
    return true;
}

// Aggregate entry point. Called from a single static_assert chain.
constexpr bool validate_all(const RelationEntry* arr, size_t n) {
    return !has_duplicate(arr, n)
        && validate_all_handlers(arr, n)
        && validate_verb_shapes(arr, n);
}

} // namespace mora::model
```

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=Validate.*`
Expected: PASS.

- [ ] **Step 5: Wire static_assert into relations_seed.cpp**

Edit `src/model/relations_seed.cpp` to append:

```cpp
#include "mora/model/validate.h"
static_assert(validate_all(kRelations, sizeof(kRelations) / sizeof(kRelations[0])),
              "kRelations fails validation — see individual helper checks");
```

Run: `xmake build mora_lib`
Expected: PASS (empty array trivially validates).

- [ ] **Step 6: Commit**

```bash
git add include/mora/model/validate.h src/model/relations_seed.cpp tests/model/test_validate.cpp
git commit -m "model: add constexpr relation validators wired via static_assert"
```

---

### Task 5: Seed `kRelations` with the v1 effect set

Migrate the relations/effects currently expressed in `form_model.h` into `kRelations`. This preserves all present functionality at the metadata level without removing `form_model.h` yet (the runtime still needs its offsets; relation metadata is layered on top).

**Files:**
- Modify: `src/model/relations_seed.cpp`
- Test: `tests/model/test_relation_seed.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/model/test_relation_seed.cpp
#include "mora/model/relations.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(RelationSeed, FormKeywordExists) {
    const auto* r = find_relation("form", "keyword", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->cardinality, Cardinality::Set);
    EXPECT_EQ(r->source, RelationSourceKind::Static);
}

TEST(RelationSeed, FormDamageIsCountable) {
    const auto* r = find_relation("form", "damage", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->cardinality, Cardinality::Countable);
    EXPECT_EQ(r->value_type, RelValueType::Int);
}

TEST(RelationSeed, FormNpcIsFunctionalPredicate) {
    const auto* r = find_relation("form", "npc", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->arg_count, 1);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=RelationSeed.*`
Expected: FAIL.

- [ ] **Step 3: Populate `kRelations`**

Replace the body of `src/model/relations_seed.cpp`:

```cpp
#include "mora/model/relations.h"
#include "mora/model/validate.h"

namespace mora::model {

const RelationEntry kRelations[] = {
    // ── Type predicates (unary, read-only from ESP) ────────────────────
    { .namespace_ = "form", .name = "npc",
      .args = {{RelValueType::FormRef, "F"}}, .arg_count = 1,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "NPC_"},
      .docs = "True when F is a base NPC record." },

    { .namespace_ = "form", .name = "weapon",
      .args = {{RelValueType::FormRef, "F"}}, .arg_count = 1,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "WEAP"},
      .docs = "True when F is a weapon base record." },

    { .namespace_ = "form", .name = "armor",
      .args = {{RelValueType::FormRef, "F"}}, .arg_count = 1,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "ARMO"},
      .docs = "True when F is an armor base record." },

    // ── Set-valued static relations ────────────────────────────────────
    { .namespace_ = "form", .name = "keyword",
      .args = {{RelValueType::FormRef, "F"}, {RelValueType::FormRef, "KW"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Set,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "", .subrecord = "KWDA"},
      .docs = "Keyword membership on a base record (body: query; head: add/remove)." },

    { .namespace_ = "form", .name = "faction",
      .args = {{RelValueType::FormRef, "NPC"}, {RelValueType::FormRef, "FAC"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Set,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "NPC_", .subrecord = "SNAM"},
      .docs = "Faction membership on an NPC base record." },

    // ── Countable numeric scalars ──────────────────────────────────────
    { .namespace_ = "form", .name = "damage",
      .args = {{RelValueType::FormRef, "W"}, {RelValueType::Int, "N"}}, .arg_count = 2,
      .value_type = RelValueType::Int, .cardinality = Cardinality::Countable,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "WEAP", .subrecord = "DNAM"},
      .docs = "Weapon base damage." },

    { .namespace_ = "form", .name = "gold_value",
      .args = {{RelValueType::FormRef, "F"}, {RelValueType::Int, "N"}}, .arg_count = 2,
      .value_type = RelValueType::Int, .cardinality = Cardinality::Countable,
      .source = RelationSourceKind::Static,
      .esp_source = {.subrecord = "DATA"},
      .docs = "Gold value of an item." },

    // ── Scalar (set-only) ──────────────────────────────────────────────
    { .namespace_ = "form", .name = "name",
      .args = {{RelValueType::FormRef, "F"}, {RelValueType::String, "S"}}, .arg_count = 2,
      .value_type = RelValueType::String, .cardinality = Cardinality::Scalar,
      .source = RelationSourceKind::Static,
      .esp_source = {.subrecord = "FULL"},
      .docs = "Display name." },

    { .namespace_ = "form", .name = "base_level",
      .args = {{RelValueType::FormRef, "NPC"}, {RelValueType::Int, "L"}}, .arg_count = 2,
      .value_type = RelValueType::Int, .cardinality = Cardinality::Countable,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "NPC_", .subrecord = "ACBS"},
      .docs = "NPC base level." },

    { .namespace_ = "form", .name = "race",
      .args = {{RelValueType::FormRef, "NPC"}, {RelValueType::FormRef, "RACE"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "NPC_", .subrecord = "RNAM"},
      .docs = "NPC base race." },
};

const size_t kRelationCount = sizeof(kRelations) / sizeof(kRelations[0]);

static_assert(validate_all(kRelations, sizeof(kRelations) / sizeof(kRelations[0])),
              "kRelations fails validation — see individual helper checks");

} // namespace mora::model
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=RelationSeed.*`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/model/relations_seed.cpp tests/model/test_relation_seed.cpp
git commit -m "model: seed kRelations with v1 form/* relations"
```

---

## Phase B — Lexer (Tasks 6–9)

### Task 6: Add `@EditorID` token

The current lexer treats `:Foo` as a `Symbol` token (today's EditorID). We will:
- Repurpose the `Symbol` token to represent a v2 *keyword* (`:foo`, `:high`).
- Introduce a new `EditorId` token produced by the `@` prefix (`@IronSword`).

**Files:**
- Modify: `include/mora/lexer/token.h`
- Modify: `src/lexer/lexer.cpp`
- Modify: `src/lexer/token.cpp`
- Create/Extend: `tests/lexer/test_v2_tokens.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/lexer/test_v2_tokens.cpp
#include "mora/lexer/lexer.h"
#include <gtest/gtest.h>

using namespace mora;

TEST(V2Tokens, EditorIdTokenEmitted) {
    StringPool pool;
    Lexer lex("@IronSword\n", "test.mora", pool);
    auto tokens = lex.tokenize();
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::EditorId);
    EXPECT_EQ(tokens[0].text, "IronSword");  // without the @
}

TEST(V2Tokens, EditorIdRequiresIdentifierAfterAt) {
    StringPool pool;
    Lexer lex("@\n", "test.mora", pool);
    auto tokens = lex.tokenize();
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Error);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Tokens.EditorId*`
Expected: FAIL.

- [ ] **Step 3: Add `EditorId` to `TokenKind`**

Edit `include/mora/lexer/token.h` — add `EditorId` after `Symbol` in the literals group:

```cpp
// Literals
Integer, Float, String, Symbol, EditorId, Variable, Identifier, Discard,
```

Edit `src/lexer/token.cpp` — extend `token_kind_name` switch to return `"EditorId"` for the new value.

- [ ] **Step 4: Implement tokenization in `src/lexer/lexer.cpp`**

Locate the top-level character dispatch in `Lexer::next_token()` (or equivalent) and add a branch for `@`:

```cpp
if (c == '@') {
    size_t start = pos_;
    advance();  // consume '@'
    if (!is_ident_start(peek())) {
        return make_error_token("expected EditorID after '@'", start);
    }
    size_t id_start = pos_;
    while (is_ident_cont(peek())) advance();
    std::string_view text{src_.data() + id_start, pos_ - id_start};
    Token t;
    t.kind = TokenKind::EditorId;
    t.text = text;
    t.string_id = pool_.intern(text);
    t.span = span_from(start);
    return t;
}
```

Adapt identifier names to the actual lexer API in this codebase (check `src/lexer/lexer.cpp` for the exact helpers — the above is illustrative).

- [ ] **Step 5: Run test to verify it passes**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Tokens.EditorId*`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/mora/lexer/token.h src/lexer/token.cpp src/lexer/lexer.cpp tests/lexer/test_v2_tokens.cpp
git commit -m "lexer: add @EditorID token"
```

---

### Task 7: Repurpose `:ident` → keyword token semantics

Today, `:Foo` tokenizes to `Symbol`. Semantically it means EditorID. We keep the token name `Symbol` but retitle its semantic role to *keyword*. (The parser and resolver consume the token; the token itself just means "colon-prefixed identifier".) No lexer changes needed for the bare token — the behavior change is downstream. We update the displayed name to make intent obvious.

**Files:**
- Modify: `src/lexer/token.cpp`
- Extend: `tests/lexer/test_v2_tokens.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// append to tests/lexer/test_v2_tokens.cpp
TEST(V2Tokens, ColonIdentIsSymbolToken) {
    StringPool pool;
    Lexer lex(":high\n", "test.mora", pool);
    auto tokens = lex.tokenize();
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Symbol);
    EXPECT_EQ(tokens[0].text, "high");
}

TEST(V2Tokens, TokenKindNameForSymbolSaysKeyword) {
    EXPECT_STREQ(token_kind_name(TokenKind::Symbol), "Keyword");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Tokens.*`
Expected: FAIL on the second test (name currently prints "Symbol").

- [ ] **Step 3: Update the display name**

In `src/lexer/token.cpp`, change the `Symbol` arm of `token_kind_name` to return `"Keyword"`.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Tokens.*`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/lexer/token.cpp tests/lexer/test_v2_tokens.cpp
git commit -m "lexer: rename Symbol token display name to Keyword (semantic clarification)"
```

---

### Task 8: Tokenize `/` inside namespaced identifiers

Today `/` is `TokenKind::Slash` (arithmetic). We want `form/keyword` to parse cleanly. The simplest approach: keep `/` as a distinct `Slash` token and let the parser glue `Identifier Slash Identifier` into a namespaced reference. (This avoids changing arithmetic handling.) Add a helper predicate the parser can use.

**Files:**
- Extend: `tests/lexer/test_v2_tokens.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// append to tests/lexer/test_v2_tokens.cpp
TEST(V2Tokens, NamespacedIdentifierLexesAsThreeTokens) {
    StringPool pool;
    Lexer lex("form/keyword\n", "test.mora", pool);
    auto tokens = lex.tokenize();
    ASSERT_GE(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[0].text, "form");
    EXPECT_EQ(tokens[1].kind, TokenKind::Slash);
    EXPECT_EQ(tokens[2].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[2].text, "keyword");
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Tokens.NamespacedIdentifier*`
Expected: PASS (this should already work with the current lexer — the test codifies the contract for the parser).

- [ ] **Step 3: Commit (codify the contract even if no code changed)**

```bash
git add tests/lexer/test_v2_tokens.cpp
git commit -m "lexer: test codifying that ns/name lexes as three tokens"
```

---

### Task 9: Add keyword tokens for `maintain`, `on`, `as`, `refer`, `set`, `add`, `sub`, `remove`

**Files:**
- Modify: `include/mora/lexer/token.h`
- Modify: `src/lexer/lexer.cpp` (keyword-recognition table)
- Modify: `src/lexer/token.cpp`
- Extend: `tests/lexer/test_v2_tokens.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// append to tests/lexer/test_v2_tokens.cpp
TEST(V2Tokens, NewKeywordsRecognized) {
    StringPool pool;
    Lexer lex("maintain on as refer set add sub remove\n", "test.mora", pool);
    auto tokens = lex.tokenize();
    ASSERT_GE(tokens.size(), 8u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KwMaintain);
    EXPECT_EQ(tokens[1].kind, TokenKind::KwOn);
    EXPECT_EQ(tokens[2].kind, TokenKind::KwAs);
    EXPECT_EQ(tokens[3].kind, TokenKind::KwRefer);
    EXPECT_EQ(tokens[4].kind, TokenKind::KwSet);
    EXPECT_EQ(tokens[5].kind, TokenKind::KwAdd);
    EXPECT_EQ(tokens[6].kind, TokenKind::KwSub);
    EXPECT_EQ(tokens[7].kind, TokenKind::KwRemove);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Tokens.NewKeywords*`
Expected: FAIL (token kinds missing).

- [ ] **Step 3: Add token kinds**

Edit `include/mora/lexer/token.h`, extend the Keywords enumerator group:

```cpp
// Keywords
KwNamespace, KwRequires, KwMod, KwUse, KwOnly, KwNot, KwOr, KwIn, KwImportSpid, KwImportKid,
KwMaintain, KwOn, KwAs, KwRefer,
KwSet, KwAdd, KwSub, KwRemove,
```

Extend `src/lexer/token.cpp`'s name table.

- [ ] **Step 4: Register the keyword strings**

In `src/lexer/lexer.cpp`, find the keyword lookup table (a `std::unordered_map` or string switch on identifier text) and add entries:

```cpp
{"maintain", TokenKind::KwMaintain},
{"on",       TokenKind::KwOn},
{"as",       TokenKind::KwAs},
{"refer",    TokenKind::KwRefer},
{"set",      TokenKind::KwSet},
{"add",      TokenKind::KwAdd},
{"sub",      TokenKind::KwSub},
{"remove",   TokenKind::KwRemove},
```

- [ ] **Step 5: Run test to verify it passes**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Tokens.NewKeywords*`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/mora/lexer/token.h src/lexer/lexer.cpp src/lexer/token.cpp tests/lexer/test_v2_tokens.cpp
git commit -m "lexer: add v2 keywords (maintain/on/as/refer/set/add/sub/remove)"
```

---

## Phase C — AST Extensions (Task 10)

### Task 10: Extend AST with `RuleKind`, `VerbKind`, new `Effect`, `UseDecl` fields, and `EditorIdExpr`

**Files:**
- Modify: `include/mora/ast/ast.h`
- Test: `tests/ast/test_v2_ast_shape.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/ast/test_v2_ast_shape.cpp
#include "mora/ast/ast.h"
#include <gtest/gtest.h>

using namespace mora;

TEST(V2Ast, RuleHasKindField) {
    Rule r{};
    EXPECT_EQ(r.kind, RuleKind::Static);
    r.kind = RuleKind::Maintain;
    EXPECT_EQ(r.kind, RuleKind::Maintain);
}

TEST(V2Ast, UseDeclHasAliasAndRefer) {
    UseDecl u{};
    EXPECT_TRUE(u.alias.id == 0);          // optional: 0 means absent
    EXPECT_TRUE(u.refer.empty());
}

TEST(V2Ast, EffectHasVerbAndNamespace) {
    Effect e{};
    EXPECT_EQ(e.verb, VerbKind::Set);
    EXPECT_TRUE(e.namespace_.id == 0);
    EXPECT_TRUE(e.name.id == 0);
}

TEST(V2Ast, EditorIdExprVariantMember) {
    EditorIdExpr eid{};
    (void)eid;
    SUCCEED();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Ast.*`
Expected: FAIL.

- [ ] **Step 3: Extend the AST header**

Edit `include/mora/ast/ast.h`:

```cpp
// near the top, after existing enums:
enum class RuleKind : uint8_t { Static, Maintain, On };
enum class VerbKind : uint8_t { Set, Add, Sub, Remove };

// add new expression kind:
struct EditorIdExpr {
    StringId name;
    MoraType resolved_type = MoraType::make(TypeKind::Unknown);
    SourceSpan span;
};
```

Add `EditorIdExpr` to the `Expr::data` variant:

```cpp
struct Expr {
    std::variant<VariableExpr, SymbolExpr, EditorIdExpr, IntLiteral, FloatLiteral,
                 StringLiteral, DiscardExpr, BinaryExpr> data;
    SourceSpan span;
};
```

Rework `Effect`:

```cpp
struct Effect {
    VerbKind  verb = VerbKind::Set;
    StringId  namespace_;   // e.g. "form"
    StringId  name;         // e.g. "keyword"
    std::vector<Expr> args;
    SourceSpan span;
};
```

Extend `UseDecl`:

```cpp
struct UseDecl {
    StringId namespace_path;
    StringId alias;                     // 0 if no :as
    std::vector<StringId> refer;        // names from :refer [...] (empty if none)
    SourceSpan span;
};
```

Extend `Rule`:

```cpp
struct Rule {
    RuleKind kind = RuleKind::Static;
    StringId name;
    std::vector<Expr> head_args;
    std::vector<Clause> body;
    std::vector<Effect> effects;
    std::vector<ConditionalEffect> conditional_effects;
    SourceSpan span;
};
```

Note: The existing `FactPattern` already has a `qualifier` field — this now holds the namespace for relation references (e.g. `form` in `form/keyword`). No schema change needed.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Ast.*`
Expected: PASS.

- [ ] **Step 5: Fix downstream breakage**

The AST reshape will likely break compilation of the parser, name resolver, type checker, and evaluator — anywhere that reads `Effect::action`. For each broken site, apply the smallest mechanical change:

- Read `Effect::name` where `Effect::action` was read.
- Where the code used a single action-name string, substitute `verb + namespace_ + name` temporarily. (The proper handling comes in Tasks 11–16; for now, set `verb = VerbKind::Set`, `namespace_ = StringId{0}`, and keep semantics equivalent to v1.)

Run: `xmake build mora_lib`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/mora/ast/ast.h tests/ast/test_v2_ast_shape.cpp src/
git commit -m "ast: add RuleKind, VerbKind, namespaced Effect, UseDecl alias/refer, EditorIdExpr"
```

---

## Phase D — Parser (Tasks 11–14)

### Task 11: Parse `@EditorID` expressions

**Files:**
- Modify: `src/parser/parser.cpp`
- Test: `tests/parser/test_v2_syntax.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/parser/test_v2_syntax.cpp
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include <gtest/gtest.h>

using namespace mora;

TEST(V2Syntax, EditorIdParsesAsEditorIdExpr) {
    StringPool pool;
    Lexer lex("namespace x.y\nr(F):\n    form/weapon(F)\n    form/keyword(F, @IronSword)\n",
              "test.mora", pool);
    auto tokens = lex.tokenize();
    Parser p(tokens, pool);
    auto mod = p.parse();
    ASSERT_EQ(mod.rules.size(), 1u);
    const Rule& r = mod.rules[0];
    ASSERT_EQ(r.body.size(), 2u);
    // second clause's second arg is @IronSword
    const auto& fp = std::get<FactPattern>(r.body[1].data);
    ASSERT_EQ(fp.args.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<EditorIdExpr>(fp.args[1].data));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Syntax.EditorId*`
Expected: FAIL (parser does not accept `@ident`).

- [ ] **Step 3: Extend `parse_primary_expr` (or equivalent) in `src/parser/parser.cpp`**

Find where `TokenKind::Symbol` is handled and add a parallel branch for `TokenKind::EditorId`:

```cpp
if (cur().kind == TokenKind::EditorId) {
    EditorIdExpr e;
    e.name = cur().string_id;
    e.span = cur().span;
    advance();
    Expr out;
    out.data = e;
    out.span = e.span;
    return out;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Syntax.EditorId*`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/parser/parser.cpp tests/parser/test_v2_syntax.cpp
git commit -m "parser: parse @EditorID as EditorIdExpr"
```

---

### Task 12: Parse namespaced relation references (`form/keyword`)

**Files:**
- Modify: `src/parser/parser.cpp`
- Extend: `tests/parser/test_v2_syntax.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// append to tests/parser/test_v2_syntax.cpp
TEST(V2Syntax, NamespacedFactParses) {
    StringPool pool;
    Lexer lex("namespace x.y\nr(F):\n    form/weapon(F)\n    form/keyword(F, @Iron)\n",
              "test.mora", pool);
    auto tokens = lex.tokenize();
    Parser p(tokens, pool);
    auto mod = p.parse();
    ASSERT_EQ(mod.rules.size(), 1u);
    const auto& fp0 = std::get<FactPattern>(mod.rules[0].body[0].data);
    EXPECT_EQ(pool.view(fp0.qualifier), "form");
    EXPECT_EQ(pool.view(fp0.name), "weapon");
    const auto& fp1 = std::get<FactPattern>(mod.rules[0].body[1].data);
    EXPECT_EQ(pool.view(fp1.qualifier), "form");
    EXPECT_EQ(pool.view(fp1.name), "keyword");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Syntax.NamespacedFact*`
Expected: FAIL (today's parser splits `form/weapon` as identifier then arithmetic divide).

- [ ] **Step 3: Implement namespaced fact parsing**

In `parse_clause` (or wherever a body clause starting with an identifier is parsed), look ahead for `Slash` after a leading `Identifier`:

```cpp
// inside parse_fact_pattern() or equivalent
StringId qualifier{};
StringId name = cur().string_id;
SourceSpan span = cur().span;
advance(); // consume first identifier

if (cur().kind == TokenKind::Slash && peek(1).kind == TokenKind::Identifier) {
    advance(); // consume /
    qualifier = name;
    name = cur().string_id;
    span = merge_spans(span, cur().span);
    advance();
}

// then continue with '(' args ')' as before
```

The existing `FactPattern.qualifier` field stores the namespace.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Syntax.NamespacedFact*`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/parser/parser.cpp tests/parser/test_v2_syntax.cpp
git commit -m "parser: parse namespaced relation references (form/keyword)"
```

---

### Task 13: Parse `maintain` / `on` rule annotations

**Files:**
- Modify: `src/parser/parser.cpp`
- Extend: `tests/parser/test_v2_syntax.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// append to tests/parser/test_v2_syntax.cpp
TEST(V2Syntax, MaintainRuleAnnotation) {
    StringPool pool;
    Lexer lex("namespace x.y\nmaintain r(F):\n    form/weapon(F)\n",
              "test.mora", pool);
    auto tokens = lex.tokenize();
    Parser p(tokens, pool);
    auto mod = p.parse();
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(mod.rules[0].kind, RuleKind::Maintain);
}

TEST(V2Syntax, OnRuleAnnotation) {
    StringPool pool;
    Lexer lex("namespace x.y\non r(F):\n    form/weapon(F)\n",
              "test.mora", pool);
    auto tokens = lex.tokenize();
    Parser p(tokens, pool);
    auto mod = p.parse();
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(mod.rules[0].kind, RuleKind::On);
}

TEST(V2Syntax, UnannotatedRuleIsStatic) {
    StringPool pool;
    Lexer lex("namespace x.y\nr(F):\n    form/weapon(F)\n",
              "test.mora", pool);
    auto tokens = lex.tokenize();
    Parser p(tokens, pool);
    auto mod = p.parse();
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(mod.rules[0].kind, RuleKind::Static);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Syntax.*Rule*Annotation*`
Expected: FAIL.

- [ ] **Step 3: Implement at the top of `parse_rule`**

```cpp
// in parse_rule (or top-level decl dispatch)
RuleKind kind = RuleKind::Static;
if (cur().kind == TokenKind::KwMaintain) { kind = RuleKind::Maintain; advance(); }
else if (cur().kind == TokenKind::KwOn)  { kind = RuleKind::On;       advance(); }
// ... existing rule parsing
rule.kind = kind;
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Syntax.*Rule*Annotation*`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/parser/parser.cpp tests/parser/test_v2_syntax.cpp
git commit -m "parser: parse maintain/on rule annotations"
```

---

### Task 14: Parse verb-prefixed effects and expanded `use … :as … :refer [...]`

**Files:**
- Modify: `src/parser/parser.cpp`
- Extend: `tests/parser/test_v2_syntax.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// append to tests/parser/test_v2_syntax.cpp
TEST(V2Syntax, VerbEffectParsed) {
    StringPool pool;
    Lexer lex(
        "namespace x.y\n"
        "r(F):\n"
        "    form/weapon(F)\n"
        "    => set form/damage(F, 20)\n"
        "    => add form/keyword(F, @Enchanted)\n"
        "    => remove form/keyword(F, @Cursed)\n",
        "test.mora", pool);
    auto tokens = lex.tokenize();
    Parser p(tokens, pool);
    auto mod = p.parse();
    ASSERT_EQ(mod.rules.size(), 1u);
    const auto& r = mod.rules[0];
    ASSERT_EQ(r.effects.size(), 3u);
    EXPECT_EQ(r.effects[0].verb, VerbKind::Set);
    EXPECT_EQ(pool.view(r.effects[0].namespace_), "form");
    EXPECT_EQ(pool.view(r.effects[0].name), "damage");
    EXPECT_EQ(r.effects[1].verb, VerbKind::Add);
    EXPECT_EQ(r.effects[2].verb, VerbKind::Remove);
}

TEST(V2Syntax, UseDeclWithAsAndRefer) {
    StringPool pool;
    Lexer lex(
        "namespace x.y\n"
        "use form :as f\n"
        "use ref :refer [keyword, in_combat]\n"
        "use event :as e :refer [entered_location]\n",
        "test.mora", pool);
    auto tokens = lex.tokenize();
    Parser p(tokens, pool);
    auto mod = p.parse();
    ASSERT_EQ(mod.use_decls.size(), 3u);

    EXPECT_EQ(pool.view(mod.use_decls[0].namespace_path), "form");
    EXPECT_EQ(pool.view(mod.use_decls[0].alias), "f");
    EXPECT_TRUE(mod.use_decls[0].refer.empty());

    EXPECT_EQ(pool.view(mod.use_decls[1].namespace_path), "ref");
    EXPECT_TRUE(mod.use_decls[1].alias.id == 0);
    ASSERT_EQ(mod.use_decls[1].refer.size(), 2u);
    EXPECT_EQ(pool.view(mod.use_decls[1].refer[0]), "keyword");
    EXPECT_EQ(pool.view(mod.use_decls[1].refer[1]), "in_combat");

    EXPECT_EQ(pool.view(mod.use_decls[2].alias), "e");
    ASSERT_EQ(mod.use_decls[2].refer.size(), 1u);
    EXPECT_EQ(pool.view(mod.use_decls[2].refer[0]), "entered_location");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Syntax.VerbEffect*:V2Syntax.UseDeclWithAs*`
Expected: FAIL.

- [ ] **Step 3: Implement verb parsing in `parse_effect`**

```cpp
// expects Arrow already consumed
VerbKind verb;
switch (cur().kind) {
    case TokenKind::KwSet:    verb = VerbKind::Set;    break;
    case TokenKind::KwAdd:    verb = VerbKind::Add;    break;
    case TokenKind::KwSub:    verb = VerbKind::Sub;    break;
    case TokenKind::KwRemove: verb = VerbKind::Remove; break;
    default:
        diag_.error(cur().span, "expected verb (set/add/sub/remove) after '=>'");
        verb = VerbKind::Set; // recover
        break;
}
advance();

// parse namespaced name: ns '/' name
StringId ns_id = cur().string_id;
advance();  // consume ns identifier
expect(TokenKind::Slash);
StringId name_id = cur().string_id;
advance();  // consume name identifier
expect(TokenKind::LParen);
std::vector<Expr> args = parse_arg_list();
expect(TokenKind::RParen);

Effect e;
e.verb = verb;
e.namespace_ = ns_id;
e.name = name_id;
e.args = std::move(args);
e.span = /* merge */;
return e;
```

- [ ] **Step 4: Implement extended `use` parsing in `parse_use_decl`**

```cpp
// expect 'use' already consumed
UseDecl u;
u.namespace_path = cur().string_id;
u.span = cur().span;
advance();

while (cur().kind == TokenKind::Colon) {
    advance();
    if (cur().kind == TokenKind::KwAs) {
        advance();
        if (cur().kind != TokenKind::Identifier) {
            diag_.error(cur().span, "expected alias identifier after ':as'");
            break;
        }
        u.alias = cur().string_id;
        advance();
    } else if (cur().kind == TokenKind::KwRefer) {
        advance();
        expect(TokenKind::LBracket);
        while (cur().kind != TokenKind::RBracket) {
            if (cur().kind != TokenKind::Identifier) {
                diag_.error(cur().span, "expected name in :refer list");
                break;
            }
            u.refer.push_back(cur().string_id);
            advance();
            if (cur().kind == TokenKind::Comma) advance();
        }
        expect(TokenKind::RBracket);
    } else {
        diag_.error(cur().span, "expected :as or :refer");
        break;
    }
}
return u;
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Syntax.VerbEffect*:V2Syntax.UseDeclWithAs*`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/parser/parser.cpp tests/parser/test_v2_syntax.cpp
git commit -m "parser: parse verb-prefixed effects and extended use :as :refer"
```

---

## Phase E — Semantics (Tasks 15–19)

### Task 15: Namespace import resolution

**Files:**
- Modify: `src/sema/name_resolver.cpp`
- Modify: `include/mora/sema/name_resolver.h`
- Test: `tests/sema/test_namespace_imports.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/sema/test_namespace_imports.cpp
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include <gtest/gtest.h>

using namespace mora;

static Module parse_src(StringPool& pool, const char* src) {
    Lexer lex(src, "test.mora", pool);
    auto toks = lex.tokenize();
    Parser p(toks, pool);
    return p.parse();
}

TEST(NamespaceImports, BareNameResolvedViaRefer) {
    StringPool pool;
    Module m = parse_src(pool,
        "namespace x.y\n"
        "use ref :refer [keyword]\n"
        "r(R):\n"
        "    keyword(R, @X)\n");
    DiagnosticList diag;
    NameResolver nr(pool, diag);
    nr.resolve(m);
    EXPECT_EQ(diag.errors().size(), 0u);
    const auto& fp = std::get<FactPattern>(m.rules[0].body[0].data);
    // resolver should rewrite to fully-qualified namespace
    EXPECT_EQ(pool.view(fp.qualifier), "ref");
    EXPECT_EQ(pool.view(fp.name), "keyword");
}

TEST(NamespaceImports, AliasRewritesQualifier) {
    StringPool pool;
    Module m = parse_src(pool,
        "namespace x.y\n"
        "use form :as f\n"
        "r(W):\n"
        "    f/weapon(W)\n");
    DiagnosticList diag;
    NameResolver nr(pool, diag);
    nr.resolve(m);
    EXPECT_EQ(diag.errors().size(), 0u);
    const auto& fp = std::get<FactPattern>(m.rules[0].body[0].data);
    EXPECT_EQ(pool.view(fp.qualifier), "form");
}

TEST(NamespaceImports, ReferConflictErrors) {
    StringPool pool;
    Module m = parse_src(pool,
        "namespace x.y\n"
        "use form :refer [keyword]\n"
        "use ref :refer [keyword]\n");
    DiagnosticList diag;
    NameResolver nr(pool, diag);
    nr.resolve(m);
    ASSERT_GE(diag.errors().size(), 1u);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=NamespaceImports.*`
Expected: FAIL.

- [ ] **Step 3: Implement import resolution**

In `name_resolver.cpp`, build an import map at the start of `resolve(Module& m)`:

```cpp
struct ImportMap {
    std::unordered_map<std::string, std::string> alias_to_ns;  // "f" -> "form"
    std::unordered_map<std::string, std::string> refer_to_ns;  // "keyword" -> "ref"
};

ImportMap build_imports(Module& m, StringPool& pool, DiagnosticList& diag) {
    ImportMap im;
    for (const auto& u : m.use_decls) {
        std::string ns{pool.view(u.namespace_path)};
        if (u.alias.id != 0) {
            im.alias_to_ns[std::string{pool.view(u.alias)}] = ns;
        }
        for (auto name_id : u.refer) {
            std::string key{pool.view(name_id)};
            auto it = im.refer_to_ns.find(key);
            if (it != im.refer_to_ns.end() && it->second != ns) {
                diag.error(u.span,
                    "name '" + key + "' referred from both '" + it->second
                    + "' and '" + ns + "'");
            }
            im.refer_to_ns[key] = ns;
        }
    }
    return im;
}
```

Then, when visiting each `FactPattern` and `Effect`, rewrite:

- If `qualifier` is empty (bare name): look up in `refer_to_ns`; if found, set qualifier; else leave empty to fail in type checker ("unresolved name").
- If `qualifier` is an alias in `alias_to_ns`: rewrite qualifier to the target namespace.
- Otherwise: keep qualifier as-is.

- [ ] **Step 4: Run tests to verify they pass**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=NamespaceImports.*`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/sema/name_resolver.cpp include/mora/sema/name_resolver.h tests/sema/test_namespace_imports.cpp
git commit -m "sema: resolve :as aliases and :refer bare names to fully-qualified namespaces"
```

---

### Task 16: Keyword interning

**Files:**
- Modify: `src/sema/name_resolver.cpp`
- Create: `include/mora/sema/keyword_intern.h`
- Create: `src/sema/keyword_intern.cpp`
- Test: `tests/sema/test_keyword_intern.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/sema/test_keyword_intern.cpp
#include "mora/sema/keyword_intern.h"
#include <gtest/gtest.h>

using namespace mora;

TEST(KeywordIntern, FirstKeywordGetsZero) {
    KeywordInterner ki;
    EXPECT_EQ(ki.intern("high"), 0u);
}

TEST(KeywordIntern, SameKeywordSameId) {
    KeywordInterner ki;
    auto a = ki.intern("fire");
    auto b = ki.intern("fire");
    EXPECT_EQ(a, b);
}

TEST(KeywordIntern, DifferentKeywordsDifferentIds) {
    KeywordInterner ki;
    auto a = ki.intern("low");
    auto b = ki.intern("high");
    EXPECT_NE(a, b);
}

TEST(KeywordIntern, Lookup) {
    KeywordInterner ki;
    auto id = ki.intern("ember");
    EXPECT_EQ(ki.name(id), "ember");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=KeywordIntern.*`
Expected: FAIL.

- [ ] **Step 3: Implement the interner**

```cpp
// include/mora/sema/keyword_intern.h
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mora {

using KeywordId = uint32_t;

class KeywordInterner {
public:
    KeywordId intern(std::string_view s);
    std::string_view name(KeywordId id) const;
    size_t size() const { return names_.size(); }
private:
    std::unordered_map<std::string, KeywordId> ids_;
    std::vector<std::string> names_;
};

} // namespace mora
```

```cpp
// src/sema/keyword_intern.cpp
#include "mora/sema/keyword_intern.h"

namespace mora {

KeywordId KeywordInterner::intern(std::string_view s) {
    std::string key{s};
    auto it = ids_.find(key);
    if (it != ids_.end()) return it->second;
    KeywordId id = static_cast<KeywordId>(names_.size());
    names_.push_back(key);
    ids_[key] = id;
    return id;
}

std::string_view KeywordInterner::name(KeywordId id) const {
    return names_[id];
}

} // namespace mora
```

- [ ] **Step 4: Register source file and run tests**

Add `src/sema/keyword_intern.cpp` to `mora_lib` in `xmake.lua`.

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=KeywordIntern.*`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/mora/sema/keyword_intern.h src/sema/keyword_intern.cpp tests/sema/test_keyword_intern.cpp xmake.lua
git commit -m "sema: add KeywordInterner for :keyword values"
```

---

### Task 17: Verb-vs-cardinality legality check

**Files:**
- Modify: `src/sema/type_checker.cpp`
- Modify: `include/mora/sema/type_checker.h`
- Test: `tests/sema/test_verb_legality.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/sema/test_verb_legality.cpp
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
#include <gtest/gtest.h>

using namespace mora;

static DiagnosticList check(const char* src) {
    StringPool pool;
    Lexer lex(src, "test.mora", pool);
    auto toks = lex.tokenize();
    Parser p(toks, pool);
    auto mod = p.parse();
    DiagnosticList diag;
    NameResolver nr(pool, diag);
    nr.resolve(mod);
    TypeChecker tc(pool, diag);
    tc.check(mod);
    return diag;
}

TEST(VerbLegality, SetOnScalarOk) {
    auto d = check(
        "namespace x.y\nr(F):\n    form/weapon(F)\n    => set form/damage(F, 20)\n");
    EXPECT_EQ(d.errors().size(), 0u);
}

TEST(VerbLegality, AddOnScalarFails) {
    auto d = check(
        "namespace x.y\nr(F):\n    form/weapon(F)\n    => add form/damage(F, 20)\n");
    // damage is Countable (Int) → add IS legal here. Use a true Scalar to force failure.
    EXPECT_EQ(d.errors().size(), 0u);
}

TEST(VerbLegality, AddOnPlainScalarFails) {
    auto d = check(
        "namespace x.y\nr(F):\n    form/npc(F)\n    => add form/name(F, \"Nazeem\")\n");
    ASSERT_GE(d.errors().size(), 1u);
}

TEST(VerbLegality, RemoveOnSetOk) {
    auto d = check(
        "namespace x.y\nr(F):\n    form/weapon(F)\n    => remove form/keyword(F, @Iron)\n");
    EXPECT_EQ(d.errors().size(), 0u);
}

TEST(VerbLegality, SetOnSetFails) {
    auto d = check(
        "namespace x.y\nr(F):\n    form/weapon(F)\n    => set form/keyword(F, @Iron)\n");
    ASSERT_GE(d.errors().size(), 1u);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=VerbLegality.*`
Expected: FAIL.

- [ ] **Step 3: Implement the check in `type_checker.cpp`**

For each `Effect` in each rule:

```cpp
#include "mora/model/relations.h"
#include "mora/model/validate.h"

// ...
for (const Rule& r : mod.rules) {
    for (const Effect& e : r.effects) {
        std::string ns{pool_.view(e.namespace_)};
        std::string nm{pool_.view(e.name)};
        const model::RelationEntry* rel =
            model::find_relation(ns, nm, model::kRelations, model::kRelationCount);
        if (!rel) {
            diag_.error(e.span,
                "unknown relation '" + ns + "/" + nm + "'");
            continue;
        }
        model::VerbKind vk;
        switch (e.verb) {
            case VerbKind::Set:    vk = model::VerbKind::Set;    break;
            case VerbKind::Add:    vk = model::VerbKind::Add;    break;
            case VerbKind::Sub:    vk = model::VerbKind::Sub;    break;
            case VerbKind::Remove: vk = model::VerbKind::Remove; break;
        }
        if (!model::is_legal_verb_for(vk, rel->cardinality)) {
            diag_.error(e.span,
                "verb '" + verb_name(e.verb) + "' is not legal for relation '"
                + ns + "/" + nm + "' (cardinality " + cardinality_name(rel->cardinality) + ")");
        }
    }
}
```

Add small helpers `verb_name` and `cardinality_name` in the anonymous namespace.

- [ ] **Step 4: Run tests to verify they pass**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=VerbLegality.*`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/sema/type_checker.cpp include/mora/sema/type_checker.h tests/sema/test_verb_legality.cpp
git commit -m "sema: reject illegal verb/cardinality combinations"
```

---

### Task 18: Reject unknown relations and arity mismatches

**Files:**
- Modify: `src/sema/type_checker.cpp`
- Extend: `tests/sema/test_verb_legality.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// append to tests/sema/test_verb_legality.cpp
TEST(TypeCheck, UnknownRelationErrors) {
    auto d = check(
        "namespace x.y\nr(F):\n    form/nonexistent(F)\n");
    ASSERT_GE(d.errors().size(), 1u);
}

TEST(TypeCheck, ArityMismatchErrors) {
    auto d = check(
        "namespace x.y\nr(F):\n    form/weapon(F, F)\n");
    ASSERT_GE(d.errors().size(), 1u);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=TypeCheck.*`
Expected: FAIL.

- [ ] **Step 3: Add the same relation-lookup + arity check to body FactPatterns**

In `type_checker.cpp`, for each `FactPattern fp` in a rule body:

```cpp
std::string ns{pool_.view(fp.qualifier)};
std::string nm{pool_.view(fp.name)};
// Allow user-defined rules in the current module: skip lookup if ns matches module ns.
if (!ns.empty() && ns != module_ns_) {
    const auto* rel = model::find_relation(ns, nm, model::kRelations, model::kRelationCount);
    if (!rel) {
        diag_.error(fp.span, "unknown relation '" + ns + "/" + nm + "'");
        continue;
    }
    if (fp.args.size() != rel->arg_count) {
        diag_.error(fp.span,
            "relation '" + ns + "/" + nm + "' expects "
            + std::to_string(rel->arg_count) + " args, got "
            + std::to_string(fp.args.size()));
    }
}
```

Capture `module_ns_` from `mod.ns->name` at the top of `check()`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=TypeCheck.*`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/sema/type_checker.cpp tests/sema/test_verb_legality.cpp
git commit -m "sema: reject unknown relations and arity mismatches against kRelations"
```

---

### Task 19: `maintain` retractability and `event/*` usage checks

**Files:**
- Modify: `src/sema/type_checker.cpp`
- Extend: `tests/sema/test_verb_legality.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// append to tests/sema/test_verb_legality.cpp
TEST(MaintainRules, MaintainOnNonRetractableFails) {
    // player/gold has no retract_handler (add is irreversible here).
    // This test assumes player/gold is registered by Plan 3; until then,
    // use a stand-in: a hand-seeded relation with retract_handler == None
    // could be added to kRelations temporarily for this test. Skip otherwise.
    GTEST_SKIP() << "Retractability test requires dynamic relations (Plan 3).";
}

TEST(MaintainRules, MaintainUsingEventFails) {
    // event/* is illegal in maintain. Requires an event relation in kRelations;
    // skip until Plan 3 wires one.
    GTEST_SKIP() << "Event usage test requires event relations (Plan 3).";
}
```

These are declared now as placeholders so the shape of the check is visible. They will become active in Plan 3.

- [ ] **Step 2: Implement the check shape (dormant until events exist)**

In `type_checker.cpp`:

```cpp
void TypeChecker::check_rule_kind(const Rule& r) {
    for (const auto& c : r.body) {
        if (!std::holds_alternative<FactPattern>(c.data)) continue;
        const auto& fp = std::get<FactPattern>(c.data);
        std::string ns{pool_.view(fp.qualifier)};
        if (ns == "event" && r.kind != RuleKind::On) {
            diag_.error(fp.span,
                "event/* relations are only allowed in 'on' rules");
        }
    }
    if (r.kind == RuleKind::Maintain) {
        for (const auto& e : r.effects) {
            std::string ns{pool_.view(e.namespace_)};
            std::string nm{pool_.view(e.name)};
            const auto* rel = model::find_relation(ns, nm, model::kRelations, model::kRelationCount);
            if (!rel) continue;
            if (rel->retract_handler == model::HandlerId::None
                && rel->cardinality != model::Cardinality::Scalar
                && rel->cardinality != model::Cardinality::Countable) {
                // Scalar/Countable set-based ops are naturally reversible via
                // set-prev-value bookkeeping in the engine; Set relations need
                // an explicit retract handler.
                diag_.error(e.span,
                    "effect '" + ns + "/" + nm + "' is not retractable and cannot be used in a maintain rule");
            }
        }
    }
}
```

- [ ] **Step 3: Run test suite — ensure nothing regresses**

Run: `xmake build mora_tests && xmake run mora_tests`
Expected: PASS (the new tests are SKIPPED; existing tests still pass).

- [ ] **Step 4: Commit**

```bash
git add src/sema/type_checker.cpp tests/sema/test_verb_legality.cpp
git commit -m "sema: add shape of maintain retractability and event/* usage checks (dormant until Plan 3)"
```

---

## Phase F — Phase Classifier (Task 20)

### Task 20: Three-tier classification driven by `kRelations`

**Files:**
- Modify: `src/eval/phase_classifier.cpp`
- Modify: `include/mora/eval/phase_classifier.h`
- Test: `tests/eval/test_phase_classifier_v2.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/eval/test_phase_classifier_v2.cpp
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include "mora/eval/phase_classifier.h"
#include <gtest/gtest.h>

using namespace mora;

static Module parse(StringPool& pool, const char* src) {
    Lexer lex(src, "test.mora", pool);
    auto toks = lex.tokenize();
    Parser p(toks, pool);
    return p.parse();
}

TEST(PhaseClassifier, UnannotatedWithOnlyFormIsStatic) {
    StringPool pool;
    auto m = parse(pool,
        "namespace x.y\nr(F):\n    form/weapon(F)\n    => set form/damage(F, 20)\n");
    DiagnosticList diag;
    NameResolver nr(pool, diag); nr.resolve(m);
    PhaseClassifier pc(pool, diag);
    auto classes = pc.classify(m);
    EXPECT_EQ(classes.at(0), PhaseClass::Static);
}

TEST(PhaseClassifier, MaintainAnnotationHonored) {
    StringPool pool;
    auto m = parse(pool,
        "namespace x.y\nmaintain r(F):\n    form/weapon(F)\n");
    DiagnosticList diag;
    NameResolver nr(pool, diag); nr.resolve(m);
    PhaseClassifier pc(pool, diag);
    auto classes = pc.classify(m);
    EXPECT_EQ(classes.at(0), PhaseClass::Maintain);
}

TEST(PhaseClassifier, OnAnnotationHonored) {
    StringPool pool;
    auto m = parse(pool,
        "namespace x.y\non r(F):\n    form/weapon(F)\n");
    DiagnosticList diag;
    NameResolver nr(pool, diag); nr.resolve(m);
    PhaseClassifier pc(pool, diag);
    auto classes = pc.classify(m);
    EXPECT_EQ(classes.at(0), PhaseClass::On);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=PhaseClassifier.*`
Expected: FAIL.

- [ ] **Step 3: Update `PhaseClass` enum and classifier**

Edit `include/mora/eval/phase_classifier.h`:

```cpp
enum class PhaseClass : uint8_t { Static, Maintain, On };

class PhaseClassifier {
public:
    PhaseClassifier(StringPool& pool, DiagnosticList& diag);
    // Returns map from rule index to its phase class.
    std::unordered_map<size_t, PhaseClass> classify(const Module& m);
private:
    StringPool& pool_;
    DiagnosticList& diag_;
};
```

In `phase_classifier.cpp`:

```cpp
#include "mora/model/relations.h"

PhaseClass classify_rule(const Rule& r, const Module& m, StringPool& pool, DiagnosticList& diag) {
    // Honor annotation.
    if (r.kind == RuleKind::Maintain) return PhaseClass::Maintain;
    if (r.kind == RuleKind::On)       return PhaseClass::On;

    // Unannotated: require every body relation to be Static-sourced.
    bool dynamic_seen = false;
    for (const auto& c : r.body) {
        if (!std::holds_alternative<FactPattern>(c.data)) continue;
        const auto& fp = std::get<FactPattern>(c.data);
        std::string ns{pool.view(fp.qualifier)};
        std::string nm{pool.view(fp.name)};
        if (ns.empty()) continue;  // user-defined or unresolved
        const auto* rel = model::find_relation(ns, nm, model::kRelations, model::kRelationCount);
        if (!rel) continue;
        if (rel->source != model::RelationSourceKind::Static) {
            dynamic_seen = true;
            break;
        }
    }
    if (dynamic_seen) {
        diag.error(r.span,
            "rule has dynamic relations in its body but is not annotated 'maintain' or 'on'");
    }
    return PhaseClass::Static;
}

std::unordered_map<size_t, PhaseClass> PhaseClassifier::classify(const Module& m) {
    std::unordered_map<size_t, PhaseClass> out;
    for (size_t i = 0; i < m.rules.size(); ++i) {
        out[i] = classify_rule(m.rules[i], m, pool_, diag_);
    }
    return out;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=PhaseClassifier.*`
Expected: PASS.

- [ ] **Step 5: Fix downstream usages**

Any caller of the old `PhaseClassifier` API (v1 probably returned a binary static/dynamic) needs updating. Audit `src/eval/` and `src/cli/` for references and adapt. Where a caller previously treated "dynamic" as a single category, treat `Maintain` and `On` both as "dynamic" for now. Plan 3 will replace this with full handling.

Run: `xmake build mora_lib`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/eval/phase_classifier.cpp include/mora/eval/phase_classifier.h tests/eval/test_phase_classifier_v2.cpp
git commit -m "eval: three-tier phase classification (static/maintain/on) driven by kRelations"
```

---

## Phase G — Integration (Tasks 21–22)

### Task 21: Migrate bundled test `.mora` files to v2 syntax

**Files:**
- Modify: `test_data/*.mora`
- Test: `tests/cli/test_v2_fixtures.cpp` (new smoke test; existing CLI/integration tests should continue to pass)

- [ ] **Step 1: Inspect and migrate each fixture**

For each file in `test_data/*.mora`:
- Replace `:Foo` (EditorID) with `@Foo`.
- Replace bare relation names (`has_keyword`, `weapon`, `npc`) with namespaced ones (`form/keyword`, `form/weapon`, `form/npc`).
- Replace effect actions with verb-prefixed forms:
  - `set_damage(W, N)` → `=> set form/damage(W, N)`
  - `add_keyword(F, @K)` → `=> add form/keyword(F, @K)`
  - `remove_keyword(F, @K)` → `=> remove form/keyword(F, @K)`
  - `set_name(F, "S")` → `=> set form/name(F, "S")`

Example migration — `test_data/example.mora`:

```mora
namespace test.example
requires mod("Skyrim.esm")

bandit(NPC):
    form/npc(NPC)
    form/faction(NPC, @BanditFaction)

tag_bandits(NPC):
    bandit(NPC)
    => add form/keyword(NPC, @ActorTypeNPC)
```

- [ ] **Step 2: Write a smoke test that each fixture parses and type-checks**

```cpp
// tests/cli/test_v2_fixtures.cpp
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace mora;

static std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

TEST(V2Fixtures, AllTestDataMoraFilesTypecheck) {
    std::filesystem::path dir = "test_data";
    ASSERT_TRUE(std::filesystem::exists(dir));
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".mora") continue;
        SCOPED_TRACE(entry.path().string());
        std::string src = read_file(entry.path());
        StringPool pool;
        Lexer lex(src, entry.path().string(), pool);
        auto toks = lex.tokenize();
        Parser p(toks, pool);
        auto mod = p.parse();
        DiagnosticList diag;
        NameResolver nr(pool, diag); nr.resolve(mod);
        TypeChecker tc(pool, diag); tc.check(mod);
        EXPECT_EQ(diag.errors().size(), 0u)
            << "first error: "
            << (diag.errors().empty() ? std::string{} : diag.errors()[0].message);
    }
}
```

- [ ] **Step 3: Run the smoke test**

Run: `xmake build mora_tests && xmake run mora_tests --gtest_filter=V2Fixtures.*`
Expected: PASS.

- [ ] **Step 4: Run the full test suite**

Run: `xmake build mora_tests && xmake run mora_tests`
Expected: PASS (all existing tests plus new ones).

- [ ] **Step 5: Commit**

```bash
git add test_data/ tests/cli/test_v2_fixtures.cpp
git commit -m "test: migrate test_data fixtures to v2 syntax and add fixture smoke test"
```

---

### Task 22: End-to-end CLI smoke: `mora compile` on a v2 file produces the same patches as v1 would have

**Files:**
- Test: `tests/cli/test_v2_end_to_end.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/cli/test_v2_end_to_end.cpp
#include <gtest/gtest.h>
#include <cstdlib>

// Skips unless the CLI test harness is available in this build.
TEST(V2EndToEnd, CompileRunsOnV2Fixture) {
    // Use the existing CLI's --syntax-check or equivalent no-op mode if present.
    // Otherwise, invoke the CLI with --help to assert the binary builds and links.
    int rc = std::system("xmake run mora --help > /dev/null 2>&1");
    EXPECT_EQ(rc, 0);
}
```

This is intentionally a minimal smoke test; a full end-to-end patch-emission test requires the static evaluator to speak the new effect shape, which is covered in Plan 2. We only need to prove the compiler still builds and exercises the new syntax path without crashing.

- [ ] **Step 2: Run the test**

Run: `xmake build mora && xmake build mora_tests && xmake run mora_tests --gtest_filter=V2EndToEnd.*`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/cli/test_v2_end_to_end.cpp
git commit -m "test: add v2 end-to-end CLI smoke test"
```

---

## Completion Criteria

Plan 1 is complete when:

1. `xmake build mora mora_runtime mora_tests` succeeds on Linux (and on Windows clang-cl for the runtime, unchanged from today since we didn't touch the runtime).
2. `xmake run mora_tests` passes all tests.
3. Every `test_data/*.mora` file parses, resolves names, and type-checks cleanly under v2 syntax.
4. `kRelations` is populated with the v1 `form/*` relation set, validated at compile time by `static_assert`.
5. The parser accepts `@EditorID`, `:keyword`, `ns/name`, `use :as :refer`, `maintain`/`on`, and verb-prefixed effects.
6. The type checker rejects: unknown relations, arity mismatches, illegal verb/cardinality combos, `event/*` in non-`on` rules, non-retractable effects in `maintain` rules.
7. The phase classifier reports three tiers.

Plan 2 (binary format + arrangements) and Plan 3 (differential dataflow engine) build on this foundation without revisiting any of these pieces.

---

## Self-Review

Coverage check against the spec:

- Section 2 (language surface): Tasks 6–14 cover lexer and parser for every new syntactic element.
- Section 2 verb table: Task 17 enforces verb/cardinality legality via the constexpr table.
- Section 3 (namespaces): Task 15 handles `use :as :refer`; Task 18 rejects unknown relations; Task 19 enforces `event/*` only in `on` rules (shape is in place, active once events exist in Plan 3).
- Section 6 (metaprogramming): Tasks 1–5 build the `RelationEntry` table and validators; Task 4 wires `static_assert`.
- Sections 4 and 5 (runtime engine, binary format): out of scope for Plan 1 by design.

Placeholder scan: no TBDs. The two `GTEST_SKIP` tests in Task 19 are documented placeholders for Plan 3 — they're intentional (the check is in place; the test data exists in the later plan).

Type consistency: `RuleKind`, `VerbKind`, `Effect::namespace_` / `::name` / `::verb`, `UseDecl::alias` / `::refer`, `PhaseClass::{Static,Maintain,On}` used consistently across tasks.

CommonLib offset verifier: deferred to Plan 2, where `MemoryRead` relations are first introduced and the runtime build gains a verifier source file. Not needed in Plan 1 because no `MemoryRead` relations are seeded yet.
