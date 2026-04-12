# Fact-Based INI Distribution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace 12,000 generated rules from SPID/KID INI files with facts in the FactDB, evaluated by ~20 generic distribution rules via bulk Datalog joins.

**Architecture:** INI parsers emit `(relation, Tuple)` pairs into the FactDB instead of AST Rule objects. A new `ini_distribution_rules.cpp` programmatically constructs the generic rules that join distribution facts against the form DB. The evaluator, Value type, and InClause are extended to support list-typed Values and `Element in ListVar` matching.

**Tech Stack:** C++20, existing Mora FactDB/Evaluator/StringPool infrastructure.

---

### Task 1: Add Value::Kind::List

**Files:**
- Modify: `include/mora/data/value.h`
- Modify: `src/data/value.cpp`
- Test: `tests/value_test.cpp` (new)

- [ ] **Step 1: Write failing test for list Value**

```cpp
// tests/value_test.cpp
#include <gtest/gtest.h>
#include "mora/data/value.h"
#include "mora/core/string_pool.h"

TEST(ValueTest, ListCreation) {
    std::vector<mora::Value> items = {
        mora::Value::make_formid(0x100),
        mora::Value::make_formid(0x200),
        mora::Value::make_formid(0x300),
    };
    auto list = mora::Value::make_list(std::move(items));

    EXPECT_EQ(list.kind(), mora::Value::Kind::List);
    EXPECT_EQ(list.as_list().size(), 3u);
    EXPECT_EQ(list.as_list()[0].as_formid(), 0x100u);
}

TEST(ValueTest, ListContains) {
    std::vector<mora::Value> items = {
        mora::Value::make_formid(0x100),
        mora::Value::make_formid(0x200),
    };
    auto list = mora::Value::make_list(std::move(items));
    auto needle = mora::Value::make_formid(0x200);
    auto miss = mora::Value::make_formid(0x999);

    EXPECT_TRUE(list.list_contains(needle));
    EXPECT_FALSE(list.list_contains(miss));
}

TEST(ValueTest, ListEquality) {
    auto a = mora::Value::make_list({mora::Value::make_formid(1)});
    auto b = mora::Value::make_list({mora::Value::make_formid(1)});
    auto c = mora::Value::make_list({mora::Value::make_formid(2)});

    EXPECT_EQ(a, b);
    EXPECT_FALSE(a == c);
}

TEST(ValueTest, ListHash) {
    auto a = mora::Value::make_list({mora::Value::make_formid(1)});
    auto b = mora::Value::make_list({mora::Value::make_formid(1)});
    EXPECT_EQ(a.hash(), b.hash());
}

TEST(ValueTest, ListMatchesVar) {
    auto list = mora::Value::make_list({mora::Value::make_formid(1)});
    auto var = mora::Value::make_var();
    EXPECT_TRUE(list.matches(var));
    EXPECT_TRUE(var.matches(list));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build && xmake test value_test`
Expected: FAIL — `make_list` not defined.

- [ ] **Step 3: Implement Value::Kind::List**

In `include/mora/data/value.h`, add to Kind enum and add list storage:

```cpp
enum class Kind { Var, FormID, Int, Float, String, Bool, List };

static Value make_list(std::vector<Value> items);
const std::vector<Value>& as_list() const;
bool list_contains(const Value& needle) const;
```

Since Value uses a union and `std::vector` has a non-trivial destructor, Value needs to switch from a plain union to a `std::variant`-like approach. The simplest: store the list as a `std::shared_ptr<std::vector<Value>>` in the union (pointer-sized, trivially copyable slot):

```cpp
union Data {
    uint32_t formid;
    int64_t  integer;
    double   floating;
    uint32_t string_index;
    bool     boolean;
    std::shared_ptr<std::vector<Value>>* list_ptr; // heap-allocated, managed manually

    Data() : integer(0) {}
} data_;
```

Actually cleaner: store the list separately and keep the union trivial:

```cpp
// In value.h, after the union:
std::shared_ptr<std::vector<Value>> list_;  // only set when kind_ == List
```

In `src/data/value.cpp`:

```cpp
Value Value::make_list(std::vector<Value> items) {
    Value v;
    v.kind_ = Kind::List;
    v.list_ = std::make_shared<std::vector<Value>>(std::move(items));
    return v;
}

const std::vector<Value>& Value::as_list() const {
    assert(kind_ == Kind::List);
    return *list_;
}

bool Value::list_contains(const Value& needle) const {
    assert(kind_ == Kind::List);
    for (const auto& item : *list_) {
        if (item == needle) return true;
    }
    return false;
}
```

Update `operator==`:
```cpp
case Kind::List: return *list_ == *other.list_;
```

Update `hash()`:
```cpp
case Kind::List: {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const auto& v : *list_) h ^= v.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}
```

Update `matches()` — a list matches a var (already handled by the `is_var()` check), and two lists match if they're equal.

- [ ] **Step 4: Run tests to verify they pass**

Run: `xmake build && xmake test value_test`
Expected: All 5 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/mora/data/value.h src/data/value.cpp tests/value_test.cpp
git commit -m "feat: add Value::Kind::List for set-membership operations"
```

---

### Task 2: Evaluator support for `Element in ListVar`

**Files:**
- Modify: `src/eval/evaluator.cpp` (InClause handling, ~line 203)
- Test: `tests/evaluator_test.cpp` (add test)

- [ ] **Step 1: Write failing test**

Add to `tests/evaluator_test.cpp`:

```cpp
TEST_F(EvaluatorTest, InClauseWithListValue) {
    // Setup: a fact table with list-typed values
    // distribute(1, "keyword", [:kw1, :kw2])
    auto dist_rel = pool.intern("test_dist");
    db.configure_relation(dist_rel, 3, {0});
    auto kw1 = pool.intern(":kw1");
    auto kw2 = pool.intern(":kw2");
    db.add_fact(dist_rel, {
        Value::make_int(1),
        Value::make_string(pool.intern("keyword")),
        Value::make_list({Value::make_formid(0xA), Value::make_formid(0xB)}),
    });

    // has_keyword(0x100, 0xA) and has_keyword(0x200, 0xB)
    auto has_kw = pool.intern("has_keyword");
    db.add_fact(has_kw, {Value::make_formid(0x100), Value::make_formid(0xA)});
    db.add_fact(has_kw, {Value::make_formid(0x200), Value::make_formid(0xB)});

    // Rule: match(Form): test_dist(_, "keyword", KWList), has_keyword(Form, KW), KW in KWList => effect
    // This tests that "KW in KWList" works when KWList is a bound list from FactDB
    std::string source =
        "test_rule(Form):\n"
        "    test_dist(_, \"keyword\", KWList)\n"
        "    has_keyword(Form, KW)\n"
        "    KW in KWList\n"
        "    => add_keyword(Form, :Result)\n";

    // Parse, resolve, evaluate...
    // Expect 2 patches: 0x100 and 0x200 both get :Result
}
```

The exact test setup depends on how the existing evaluator tests work — follow the pattern in the existing `evaluator_test.cpp`.

- [ ] **Step 2: Implement InClause list-variable support**

In `src/eval/evaluator.cpp`, the `InClause` handler (~line 203) currently only handles literal value lists from source code:

```cpp
} else if constexpr (std::is_same_v<T, InClause>) {
    Value var_val = resolve_expr(*c.variable, bindings);
    for (const auto& val_expr : c.values) {
        Value v = resolve_expr(val_expr, bindings);
        if (var_val.matches(v)) {
            match_clauses(rule, order, step + 1, bindings, patches, priority);
            return;
        }
    }
}
```

Extend: if `c.values` has exactly one entry and it resolves to a List, iterate the list:

```cpp
} else if constexpr (std::is_same_v<T, InClause>) {
    Value var_val = resolve_expr(*c.variable, bindings);
    // Check if the RHS is a single expression that resolves to a list
    if (c.values.size() == 1) {
        Value rhs = resolve_expr(c.values[0], bindings);
        if (rhs.kind() == Value::Kind::List) {
            // Element in ListVar: check membership
            if (rhs.list_contains(var_val)) {
                match_clauses(rule, order, step + 1, bindings, patches, priority);
            }
            return;
        }
    }
    // Original path: literal value list
    for (const auto& val_expr : c.values) {
        Value v = resolve_expr(val_expr, bindings);
        if (var_val.matches(v)) {
            match_clauses(rule, order, step + 1, bindings, patches, priority);
            return;
        }
    }
}
```

- [ ] **Step 3: Run tests**

Run: `xmake build && xmake test evaluator_test`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add src/eval/evaluator.cpp tests/evaluator_test.cpp
git commit -m "feat: support Element in ListVar for list-typed FactDB values"
```

---

### Task 3: SPID fact emitter

**Files:**
- Create: `include/mora/import/ini_facts.h`
- Create: `src/import/ini_facts.cpp`
- Test: `tests/ini_facts_test.cpp` (new)

This replaces the rule-generating SPID parser with a fact-emitting one. The existing `spid_parser.cpp` stays for the `mora import` pretty-printer; the new code is a parallel path for `compile`.

- [ ] **Step 1: Define IniFactSet and emitter interface**

```cpp
// include/mora/import/ini_facts.h
#pragma once
#include "mora/eval/fact_db.h"
#include "mora/import/ini_common.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

namespace mora {

struct IniFactStats {
    size_t spid_rules = 0;
    size_t kid_rules = 0;
    size_t facts_emitted = 0;
};

// Parse SPID _DISTR.ini files and emit facts into the FactDB.
// Returns the number of distribution rules emitted.
size_t emit_spid_facts(const std::string& path, FactDB& db,
                        StringPool& pool, DiagBag& diags,
                        uint32_t& next_rule_id);

// Parse KID _KID.ini files and emit facts into the FactDB.
size_t emit_kid_facts(const std::string& path, FactDB& db,
                       StringPool& pool, DiagBag& diags,
                       uint32_t& next_rule_id);

// Register the SPID/KID fact relations in the FactDB with proper indexes.
void configure_ini_relations(FactDB& db, StringPool& pool);

} // namespace mora
```

- [ ] **Step 2: Write test for SPID fact emission**

```cpp
// tests/ini_facts_test.cpp
#include <gtest/gtest.h>
#include "mora/import/ini_facts.h"
#include <fstream>
#include <filesystem>

class IniFactsTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db{pool};

    void SetUp() override {
        mora::configure_ini_relations(db, pool);
    }
};

TEST_F(IniFactsTest, SpidSimpleKeyword) {
    // Write a temp SPID file
    auto path = std::filesystem::temp_directory_path() / "test_DISTR.ini";
    {
        std::ofstream f(path);
        f << "Keyword = TargetKW|FilterKW1,FilterKW2|NONE|NONE|NONE|NONE|NONE\n";
    }

    uint32_t next_id = 1;
    auto count = mora::emit_spid_facts(path.string(), db, pool, diags, next_id);
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(next_id, 2u);

    // Verify spid_dist fact exists
    auto spid_dist = pool.intern("spid_dist");
    auto results = db.query(spid_dist, {
        mora::Value::make_int(1),
        mora::Value::make_var(),
        mora::Value::make_var(),
    });
    EXPECT_EQ(results.size(), 1u);

    // Verify spid_filter fact with list value
    auto spid_filter = pool.intern("spid_filter");
    auto filter_results = db.query(spid_filter, {
        mora::Value::make_int(1),
        mora::Value::make_var(),
        mora::Value::make_var(),
    });
    EXPECT_EQ(filter_results.size(), 1u);
    // The third column should be a list with 2 entries
    EXPECT_EQ(filter_results[0][2].kind(), mora::Value::Kind::List);
    EXPECT_EQ(filter_results[0][2].as_list().size(), 2u);

    std::filesystem::remove(path);
}
```

- [ ] **Step 3: Implement SPID fact emission**

In `src/import/ini_facts.cpp`:

```cpp
#include "mora/import/ini_facts.h"
#include <fstream>
#include <algorithm>

namespace mora {

void configure_ini_relations(FactDB& db, StringPool& pool) {
    auto id = [&](const char* s) { return pool.intern(s); };

    // SPID relations
    db.configure_relation(id("spid_dist"), 3, {0, 1});    // RuleID, DistType, Target
    db.configure_relation(id("spid_filter"), 3, {0, 1});   // RuleID, FilterKind, ValueList
    db.configure_relation(id("spid_exclude"), 3, {0, 1});  // RuleID, FilterKind, ValueList
    db.configure_relation(id("spid_level"), 3, {0});        // RuleID, Min, Max

    // KID relations
    db.configure_relation(id("kid_dist"), 3, {0, 2});      // RuleID, TargetKW, ItemType
    db.configure_relation(id("kid_filter"), 3, {0, 1});
    db.configure_relation(id("kid_exclude"), 3, {0, 1});
}

size_t emit_spid_facts(const std::string& path, FactDB& db,
                        StringPool& pool, DiagBag& diags,
                        uint32_t& next_rule_id) {
    std::ifstream file(path);
    if (!file.is_open()) return 0;

    auto spid_dist = pool.intern("spid_dist");
    auto spid_filter = pool.intern("spid_filter");
    auto spid_exclude = pool.intern("spid_exclude");
    auto spid_level = pool.intern("spid_level");

    size_t count = 0;
    std::string line;
    int line_num = 0;

    while (std::getline(file, line)) {
        line_num++;
        auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;

        auto eq_pos = trimmed.find('=');
        if (eq_pos == std::string_view::npos) continue;

        auto type_str = std::string(trim(trimmed.substr(0, eq_pos)));
        auto value_str = std::string(trim(trimmed.substr(eq_pos + 1)));

        // Determine distribution type
        std::string type_lower = type_str;
        std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (type_lower != "keyword" && type_lower != "spell" &&
            type_lower != "perk" && type_lower != "item" &&
            type_lower != "faction") {
            continue;
        }

        auto fields = split_pipes(value_str);
        if (fields.empty()) continue;

        uint32_t rule_id = next_rule_id++;
        auto rid = Value::make_int(rule_id);

        // Field 0: target form
        auto target_ref = parse_form_ref(fields[0]);
        std::string target_sym = target_ref.is_editor_id()
            ? target_ref.editor_id : target_ref.to_mora_symbol();
        // Store as string (symbol resolution happens during evaluation)
        auto target_val = Value::make_string(pool.intern(target_sym));

        db.add_fact(spid_dist, {rid,
            Value::make_string(pool.intern(type_lower)),
            target_val});

        // Field 1: string filters (keywords / editor_ids)
        if (fields.size() > 1 && fields[1] != "NONE" && !fields[1].empty()) {
            auto entries = parse_filter_entries(fields[1]);

            std::vector<Value> includes;
            std::vector<Value> excludes;

            for (const auto& entry : entries) {
                Value v;
                if (entry.ref.is_editor_id()) {
                    v = Value::make_string(pool.intern(entry.ref.editor_id));
                } else {
                    v = Value::make_string(pool.intern(entry.ref.to_mora_symbol()));
                }

                if (entry.mode == FilterEntry::Mode::Exclude) {
                    excludes.push_back(std::move(v));
                } else {
                    includes.push_back(std::move(v));
                }
            }

            if (!includes.empty()) {
                db.add_fact(spid_filter, {rid,
                    Value::make_string(pool.intern("keyword")),
                    Value::make_list(std::move(includes))});
            }
            if (!excludes.empty()) {
                db.add_fact(spid_exclude, {rid,
                    Value::make_string(pool.intern("keyword")),
                    Value::make_list(std::move(excludes))});
            }
        }

        // Field 2: form filters (races, factions, etc.)
        if (fields.size() > 2 && fields[2] != "NONE" && !fields[2].empty()) {
            auto entries = parse_filter_entries(fields[2]);

            std::vector<Value> includes;
            std::vector<Value> excludes;

            for (const auto& entry : entries) {
                Value v;
                if (entry.ref.is_editor_id()) {
                    v = Value::make_string(pool.intern(entry.ref.editor_id));
                } else {
                    v = Value::make_string(pool.intern(entry.ref.to_mora_symbol()));
                }

                if (entry.mode == FilterEntry::Mode::Exclude) {
                    excludes.push_back(std::move(v));
                } else {
                    includes.push_back(std::move(v));
                }
            }

            if (!includes.empty()) {
                db.add_fact(spid_filter, {rid,
                    Value::make_string(pool.intern("form")),
                    Value::make_list(std::move(includes))});
            }
            if (!excludes.empty()) {
                db.add_fact(spid_exclude, {rid,
                    Value::make_string(pool.intern("form")),
                    Value::make_list(std::move(excludes))});
            }
        }

        // Field 3: level range
        if (fields.size() > 3 && fields[3] != "NONE" && !fields[3].empty()) {
            auto range = parse_level_range(fields[3]);
            if (range.has_min || range.has_max) {
                db.add_fact(spid_level, {rid,
                    Value::make_int(range.has_min ? range.min : 0),
                    Value::make_int(range.has_max ? range.max : 0)});
            }
        }

        count++;
    }
    return count;
}
```

- [ ] **Step 4: Implement KID fact emission** (same file, similar pattern)

```cpp
size_t emit_kid_facts(const std::string& path, FactDB& db,
                       StringPool& pool, DiagBag& diags,
                       uint32_t& next_rule_id) {
    // Same structure as emit_spid_facts:
    // - Parse each line
    // - Extract keyword target + item type + filters
    // - Emit kid_dist, kid_filter, kid_exclude facts
    // Follow the pattern from emit_spid_facts above.
    // Key difference: KID line format is "Keyword = target|type|filters|traits|chance"
    // and the relation is kid_dist(RuleID, TargetKeyword, ItemType)
}
```

- [ ] **Step 5: Run tests, commit**

Run: `xmake build && xmake test ini_facts_test`

```bash
git add include/mora/import/ini_facts.h src/import/ini_facts.cpp tests/ini_facts_test.cpp
git commit -m "feat: SPID/KID fact emitters for fact-based INI distribution"
```

---

### Task 4: Generic distribution rules

**Files:**
- Create: `include/mora/import/ini_distribution_rules.h`
- Create: `src/import/ini_distribution_rules.cpp`
- Test: `tests/ini_distribution_rules_test.cpp` (new)

- [ ] **Step 1: Define interface**

```cpp
// include/mora/import/ini_distribution_rules.h
#pragma once
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"

namespace mora {

// Build a Module containing the generic SPID/KID distribution rules.
// These rules join spid_dist/kid_dist facts against the form DB
// to produce patches.
Module build_ini_distribution_rules(StringPool& pool);

} // namespace mora
```

- [ ] **Step 2: Implement rule construction**

In `src/import/ini_distribution_rules.cpp`, programmatically build the AST for rules like:

```
_spid_kw_by_keyword(NPC):
    spid_dist(RuleID, "keyword", Target)
    spid_filter(RuleID, "keyword", KWList)
    npc(NPC)
    has_keyword(NPC, KW)
    KW in KWList
    => add_keyword(NPC, Target)
```

Use AST builder helpers (FactPattern, InClause, Effect construction). One function per distribution type + filter kind combination. The function returns a Module with ~20 rules covering:

- SPID keyword/spell/perk/item/faction × filter-by-keyword
- SPID keyword/spell/perk/item/faction × filter-by-form (race/faction)
- SPID keyword/spell/perk/item/faction × no-filter
- KID keyword × per-item-type (weapon/armor/ammo/book/etc.)

- [ ] **Step 3: Write integration test**

Test that fact emission + generic rules + evaluation produces the expected patches when given a simple SPID INI file and a FactDB with some NPCs/keywords.

- [ ] **Step 4: Run tests, commit**

```bash
git add include/mora/import/ini_distribution_rules.h src/import/ini_distribution_rules.cpp tests/ini_distribution_rules_test.cpp
git commit -m "feat: generic distribution rules for fact-based INI evaluation"
```

---

### Task 5: Wire into compile pipeline

**Files:**
- Modify: `src/main.cpp` (replace `import_ini_files` with fact emission path in `cmd_compile`)
- Modify: `src/sema/name_resolver.cpp` (register SPID/KID fact relations)

- [ ] **Step 1: Register new relations in name resolver**

Add to `register_builtins()` in `src/sema/name_resolver.cpp`:

```cpp
// SPID distribution facts
reg("spid_dist",    { t(T::Int), t(T::String), t(T::String) });
reg("spid_filter",  { t(T::Int), t(T::String), t(T::FormID) }); // 3rd col is List but FormID for type-check
reg("spid_exclude", { t(T::Int), t(T::String), t(T::FormID) });
reg("spid_level",   { t(T::Int), t(T::Int), t(T::Int) });

// KID distribution facts
reg("kid_dist",     { t(T::Int), t(T::String), t(T::String) });
reg("kid_filter",   { t(T::Int), t(T::String), t(T::FormID) });
reg("kid_exclude",  { t(T::Int), t(T::String), t(T::FormID) });
```

- [ ] **Step 2: Replace import_ini_files in cmd_compile**

In `src/main.cpp`, replace the current `import_ini_files` usage in `run_check_pipeline` (for compile path) with:

```cpp
// After loading ESPs into FactDB:
mora::configure_ini_relations(db, cr.pool);

uint32_t next_rule_id = 1;
size_t spid_count = 0, kid_count = 0;

auto spid_files = find_files_by_suffix(target_path, "_DISTR.ini");
auto kid_files = find_files_by_suffix(target_path, "_KID.ini");

for (auto& path : spid_files) {
    spid_count += mora::emit_spid_facts(path.string(), db, cr.pool, cr.diags, next_rule_id);
}
for (auto& path : kid_files) {
    kid_count += mora::emit_kid_facts(path.string(), db, cr.pool, cr.diags, next_rule_id);
}

// Add generic distribution rules
auto ini_rules_mod = mora::build_ini_distribution_rules(cr.pool);
cr.modules.push_back(std::move(ini_rules_mod));
```

The key change: INI facts go into the FactDB (alongside ESP facts), and the generic rules go into the module list. The evaluator joins them naturally.

- [ ] **Step 3: Keep old path for `mora check` and `mora import`**

The `mora check` command can still use the old rule-based import for syntax checking (no FactDB needed). The `mora import` command still pretty-prints rules. Only `mora compile` uses the fact-based path.

- [ ] **Step 4: Test end-to-end**

Run: `./build/linux/x86_64/release/mora compile ~/joj_sync --data-dir ~/joj_sync --no-color`

Verify:
- Evaluation phase completes in seconds, not minutes
- Correct number of patches generated
- No crashes or hangs

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp src/sema/name_resolver.cpp
git commit -m "feat: wire fact-based INI distribution into compile pipeline"
```

---

### Task 6: Cleanup and benchmarking

**Files:**
- Modify: `src/main.cpp` (remove debug slow-rule printing)
- Run: benchmarks against ~/joj_sync

- [ ] **Step 1: Remove debug progress printing from evaluator**

Clean up the stderr debug output (`[N/M] rule_name (N clauses)`) added during profiling.

- [ ] **Step 2: Benchmark**

```bash
time ./build/linux/x86_64/release/mora compile ~/joj_sync --data-dir ~/joj_sync --no-color
```

Target: evaluation phase under 10 seconds for 12K INI distributions + 3500 ESPs.

- [ ] **Step 3: Run full test suite**

```bash
xmake test
```

All tests pass.

- [ ] **Step 4: Final commit**

```bash
git commit -m "perf: fact-based INI distribution - evaluation from minutes to seconds"
git push
```
