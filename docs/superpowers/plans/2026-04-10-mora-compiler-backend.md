# Mora Compiler Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Mora compiler backend so that `mora compile` evaluates static rules against an in-memory fact database, produces `.spatch` and `.mora.rt` binary files with field-level conflict resolution, and supports `mora inspect` and `mora info` commands.

**Architecture:** A FactDB holds game data as typed tuples. The PhaseClassifier tags each rule as static or dynamic. The Evaluator runs static rules against the FactDB, producing PatchSet entries. The SpatchWriter serializes the conflict-resolved PatchSet to binary. The RtWriter serializes dynamic rules to bytecode. Since we don't have a real ESP parser yet (Plan 3), the FactDB is populated from test data.

**Tech Stack:** C++20, xmake, Google Test. No new dependencies.

---

## File Structure

```
include/mora/
├── eval/
│   ├── fact_db.h            # in-memory fact database (typed EAV tuples)
│   ├── phase_classifier.h   # classify rules as static vs dynamic
│   ├── evaluator.h          # evaluate static rules against FactDB
│   └── patch_set.h          # collected patches with conflict resolution
├── emit/
│   ├── spatch_writer.h      # write .spatch binary format
│   ├── spatch_reader.h      # read .spatch for inspect command
│   ├── rt_writer.h          # write .mora.rt bytecode format
│   └── lock_file.h          # .mora.lock staleness detection
src/
├── eval/
│   ├── fact_db.cpp
│   ├── phase_classifier.cpp
│   ├── evaluator.cpp
│   └── patch_set.cpp
├── emit/
│   ├── spatch_writer.cpp
│   ├── spatch_reader.cpp
│   ├── rt_writer.cpp
│   └── lock_file.cpp
tests/
├── fact_db_test.cpp
├── phase_classifier_test.cpp
├── evaluator_test.cpp
├── patch_set_test.cpp
├── spatch_roundtrip_test.cpp
└── rt_writer_test.cpp
```

---

### Task 1: FactDB — In-Memory Fact Database

**Files:**
- Create: `include/mora/eval/fact_db.h`
- Create: `src/eval/fact_db.cpp`
- Create: `tests/fact_db_test.cpp`

The FactDB stores game data as typed tuples that rules can query against. Each fact is a relation name + argument values. For Phase 1, we populate it manually from test data; Plan 3 will populate it from ESP files.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/fact_db_test.cpp
#include <gtest/gtest.h>
#include "mora/eval/fact_db.h"
#include "mora/core/string_pool.h"

class FactDBTest : public ::testing::Test {
protected:
    mora::StringPool pool;
};

TEST_F(FactDBTest, AddAndQueryFact) {
    mora::FactDB db(pool);
    auto npc_rel = pool.intern("npc");
    auto bandit_id = mora::Value::make_formid(0x0003B547);
    db.add_fact(npc_rel, {bandit_id});

    auto results = db.query(npc_rel, {mora::Value::make_var()});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0][0].as_formid(), 0x0003B547u);
}

TEST_F(FactDBTest, QueryWithFilter) {
    mora::FactDB db(pool);
    auto has_kw = pool.intern("has_keyword");
    db.add_fact(has_kw, {mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    db.add_fact(has_kw, {mora::Value::make_formid(0x100), mora::Value::make_formid(0x300)});
    db.add_fact(has_kw, {mora::Value::make_formid(0x999), mora::Value::make_formid(0x200)});

    // Query: has_keyword(0x100, ?)
    auto results = db.query(has_kw, {mora::Value::make_formid(0x100), mora::Value::make_var()});
    ASSERT_EQ(results.size(), 2u);
}

TEST_F(FactDBTest, QueryNoResults) {
    mora::FactDB db(pool);
    auto npc_rel = pool.intern("npc");
    auto results = db.query(npc_rel, {mora::Value::make_var()});
    EXPECT_TRUE(results.empty());
}

TEST_F(FactDBTest, MultipleRelations) {
    mora::FactDB db(pool);
    auto npc_rel = pool.intern("npc");
    auto weapon_rel = pool.intern("weapon");
    db.add_fact(npc_rel, {mora::Value::make_formid(0x100)});
    db.add_fact(weapon_rel, {mora::Value::make_formid(0x200)});

    EXPECT_EQ(db.query(npc_rel, {mora::Value::make_var()}).size(), 1u);
    EXPECT_EQ(db.query(weapon_rel, {mora::Value::make_var()}).size(), 1u);
}

TEST_F(FactDBTest, IntAndStringValues) {
    mora::FactDB db(pool);
    auto base_level = pool.intern("base_level");
    auto name_rel = pool.intern("name");

    db.add_fact(base_level, {mora::Value::make_formid(0x100), mora::Value::make_int(25)});
    db.add_fact(name_rel, {mora::Value::make_formid(0x100), mora::Value::make_string(pool.intern("Bandit"))});

    auto results = db.query(base_level, {mora::Value::make_formid(0x100), mora::Value::make_var()});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0][1].as_int(), 25);

    auto name_results = db.query(name_rel, {mora::Value::make_formid(0x100), mora::Value::make_var()});
    ASSERT_EQ(name_results.size(), 1u);
    EXPECT_EQ(pool.get(name_results[0][1].as_string()), "Bandit");
}

TEST_F(FactDBTest, NegationQuery) {
    mora::FactDB db(pool);
    auto has_kw = pool.intern("has_keyword");
    db.add_fact(has_kw, {mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});

    // has_keyword(0x100, 0x200) exists
    EXPECT_TRUE(db.has_fact(has_kw, {mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)}));
    // has_keyword(0x100, 0x999) does not
    EXPECT_FALSE(db.has_fact(has_kw, {mora::Value::make_formid(0x100), mora::Value::make_formid(0x999)}));
}

TEST_F(FactDBTest, FactCount) {
    mora::FactDB db(pool);
    auto npc_rel = pool.intern("npc");
    db.add_fact(npc_rel, {mora::Value::make_formid(0x100)});
    db.add_fact(npc_rel, {mora::Value::make_formid(0x200)});
    EXPECT_EQ(db.fact_count(npc_rel), 2u);
    EXPECT_EQ(db.fact_count(), 2u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build fact_db_test && xmake run fact_db_test`
Expected: FAIL — header not found

- [ ] **Step 3: Implement Value type and FactDB**

```cpp
// include/mora/eval/fact_db.h
#pragma once

#include "mora/core/string_pool.h"
#include <cstdint>
#include <variant>
#include <vector>
#include <unordered_map>

namespace mora {

// A runtime value in the fact database
class Value {
public:
    enum class Kind { Var, FormID, Int, Float, String, Bool };

    static Value make_var()                    { Value v; v.kind_ = Kind::Var; return v; }
    static Value make_formid(uint32_t id)      { Value v; v.kind_ = Kind::FormID; v.formid_ = id; return v; }
    static Value make_int(int64_t i)           { Value v; v.kind_ = Kind::Int; v.int_ = i; return v; }
    static Value make_float(double f)          { Value v; v.kind_ = Kind::Float; v.float_ = f; return v; }
    static Value make_string(StringId s)       { Value v; v.kind_ = Kind::String; v.string_ = s; return v; }
    static Value make_bool(bool b)             { Value v; v.kind_ = Kind::Bool; v.bool_ = b; return v; }

    Kind kind() const { return kind_; }
    bool is_var() const { return kind_ == Kind::Var; }

    uint32_t as_formid() const { return formid_; }
    int64_t as_int() const { return int_; }
    double as_float() const { return float_; }
    StringId as_string() const { return string_; }
    bool as_bool() const { return bool_; }

    bool matches(const Value& other) const;  // true if other is var, or values equal

private:
    Kind kind_ = Kind::Var;
    uint32_t formid_ = 0;
    int64_t int_ = 0;
    double float_ = 0.0;
    StringId string_;
    bool bool_ = false;
};

using Tuple = std::vector<Value>;

class FactDB {
public:
    explicit FactDB(StringPool& pool);

    void add_fact(StringId relation, Tuple values);

    // Query: returns all tuples matching the pattern.
    // Pattern values that are Var match anything; concrete values must match exactly.
    std::vector<Tuple> query(StringId relation, const Tuple& pattern) const;

    // Check if a specific fully-ground fact exists
    bool has_fact(StringId relation, const Tuple& values) const;

    size_t fact_count(StringId relation) const;
    size_t fact_count() const;

    const std::vector<Tuple>& get_relation(StringId relation) const;

private:
    StringPool& pool_;
    std::unordered_map<uint32_t, std::vector<Tuple>> relations_; // key: StringId.index
    static const std::vector<Tuple> empty_;
};

} // namespace mora
```

```cpp
// src/eval/fact_db.cpp
#include "mora/eval/fact_db.h"

namespace mora {

const std::vector<Tuple> FactDB::empty_;

FactDB::FactDB(StringPool& pool) : pool_(pool) {}

bool Value::matches(const Value& other) const {
    if (is_var() || other.is_var()) return true;
    if (kind_ != other.kind_) return false;
    switch (kind_) {
        case Kind::FormID: return formid_ == other.formid_;
        case Kind::Int:    return int_ == other.int_;
        case Kind::Float:  return float_ == other.float_;
        case Kind::String: return string_ == other.string_;
        case Kind::Bool:   return bool_ == other.bool_;
        default:           return false;
    }
}

void FactDB::add_fact(StringId relation, Tuple values) {
    relations_[relation.index].push_back(std::move(values));
}

std::vector<Tuple> FactDB::query(StringId relation, const Tuple& pattern) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return {};

    std::vector<Tuple> results;
    for (const auto& tuple : it->second) {
        if (tuple.size() != pattern.size()) continue;
        bool match = true;
        for (size_t i = 0; i < pattern.size(); i++) {
            if (!pattern[i].matches(tuple[i])) {
                match = false;
                break;
            }
        }
        if (match) results.push_back(tuple);
    }
    return results;
}

bool FactDB::has_fact(StringId relation, const Tuple& values) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return false;

    for (const auto& tuple : it->second) {
        if (tuple.size() != values.size()) continue;
        bool match = true;
        for (size_t i = 0; i < values.size(); i++) {
            if (!values[i].matches(tuple[i])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

size_t FactDB::fact_count(StringId relation) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return 0;
    return it->second.size();
}

size_t FactDB::fact_count() const {
    size_t total = 0;
    for (const auto& [_, tuples] : relations_) {
        total += tuples.size();
    }
    return total;
}

const std::vector<Tuple>& FactDB::get_relation(StringId relation) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return empty_;
    return it->second;
}

} // namespace mora
```

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build fact_db_test && xmake run fact_db_test`
Expected: All 7 tests PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: in-memory fact database for rule evaluation"
```

---

### Task 2: Phase Classifier

**Files:**
- Create: `include/mora/eval/phase_classifier.h`
- Create: `src/eval/phase_classifier.cpp`
- Create: `tests/phase_classifier_test.cpp`

The phase classifier inspects each rule and determines if it's static (can be frozen) or dynamic (must run at runtime). Per the spec, this is determined by what the rule writes to — but in Phase 1, we use a simpler heuristic: if the rule body references any known instance-level fact, it's dynamic; otherwise it's static.

Known instance facts: `current_level`, `current_location`, `current_cell`, `equipped`, `in_inventory`, `quest_stage`, `is_alive`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/phase_classifier_test.cpp
#include <gtest/gtest.h>
#include "mora/eval/phase_classifier.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"

class PhaseClassifierTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::Module parse(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        return parser.parse_module();
    }
};

TEST_F(PhaseClassifierTest, StaticRule) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "    => add_keyword(NPC, :TestKeyword)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);

    mora::PhaseClassifier classifier(pool);
    auto result = classifier.classify(mod.rules[0]);
    EXPECT_EQ(result, mora::Phase::Static);
}

TEST_F(PhaseClassifierTest, DynamicRuleFromInstanceFact) {
    auto mod = parse(
        "merchant_goods(NPC):\n"
        "    npc(NPC)\n"
        "    current_location(NPC, Loc)\n"
        "    => add_item(NPC, :TradeGoods)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);

    mora::PhaseClassifier classifier(pool);
    auto result = classifier.classify(mod.rules[0]);
    EXPECT_EQ(result, mora::Phase::Dynamic);
}

TEST_F(PhaseClassifierTest, DerivedRuleNoEffectIsStatic) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);

    mora::PhaseClassifier classifier(pool);
    auto result = classifier.classify(mod.rules[0]);
    EXPECT_EQ(result, mora::Phase::Static);
}

TEST_F(PhaseClassifierTest, ClassifyModule) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "    => add_keyword(NPC, :TestKeyword)\n"
        "\n"
        "dynamic_rule(NPC):\n"
        "    npc(NPC)\n"
        "    current_level(NPC, Level)\n"
        "    Level >= 30\n"
        "    => add_perk(NPC, :TestPerk)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);

    mora::PhaseClassifier classifier(pool);
    auto results = classifier.classify_module(mod);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].phase, mora::Phase::Static);
    EXPECT_EQ(results[0].rule_name, mod.rules[0].name);
    EXPECT_EQ(results[1].phase, mora::Phase::Dynamic);
    EXPECT_EQ(results[1].rule_name, mod.rules[1].name);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build phase_classifier_test && xmake run phase_classifier_test`
Expected: FAIL

- [ ] **Step 3: Implement PhaseClassifier**

```cpp
// include/mora/eval/phase_classifier.h
#pragma once

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include <vector>
#include <unordered_set>

namespace mora {

enum class Phase { Static, Dynamic };

struct RuleClassification {
    StringId rule_name;
    Phase phase;
    std::string reason; // e.g., "reads current_location" or "fully static"
};

class PhaseClassifier {
public:
    explicit PhaseClassifier(StringPool& pool);

    Phase classify(const Rule& rule) const;
    std::vector<RuleClassification> classify_module(const Module& mod) const;

private:
    bool is_instance_fact(StringId name) const;
    bool body_has_instance_facts(const Rule& rule) const;

    std::unordered_set<uint32_t> instance_facts_; // StringId.index of instance-level facts
};

} // namespace mora
```

```cpp
// src/eval/phase_classifier.cpp
#include "mora/eval/phase_classifier.h"
#include <variant>

namespace mora {

PhaseClassifier::PhaseClassifier(StringPool& pool) {
    // Register known instance-level facts
    const char* instance_names[] = {
        "current_level", "current_location", "current_cell",
        "equipped", "in_inventory", "quest_stage", "is_alive"
    };
    for (const char* name : instance_names) {
        instance_facts_.insert(pool.intern(name).index);
    }
}

bool PhaseClassifier::is_instance_fact(StringId name) const {
    return instance_facts_.count(name.index) > 0;
}

bool PhaseClassifier::body_has_instance_facts(const Rule& rule) const {
    for (const auto& clause : rule.body) {
        if (auto* fact = std::get_if<FactPattern>(&clause.data)) {
            if (is_instance_fact(fact->name)) return true;
        }
    }
    return false;
}

Phase PhaseClassifier::classify(const Rule& rule) const {
    if (body_has_instance_facts(rule)) return Phase::Dynamic;
    return Phase::Static;
}

std::vector<RuleClassification> PhaseClassifier::classify_module(const Module& mod) const {
    std::vector<RuleClassification> results;
    for (const auto& rule : mod.rules) {
        RuleClassification rc;
        rc.rule_name = rule.name;
        rc.phase = classify(rule);

        if (rc.phase == Phase::Dynamic) {
            // Find the instance fact that made it dynamic
            for (const auto& clause : rule.body) {
                if (auto* fact = std::get_if<FactPattern>(&clause.data)) {
                    if (is_instance_fact(fact->name)) {
                        rc.reason = "reads instance fact";
                        break;
                    }
                }
            }
        } else {
            rc.reason = "fully static";
        }
        results.push_back(std::move(rc));
    }
    return results;
}

} // namespace mora
```

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build phase_classifier_test && xmake run phase_classifier_test`
Expected: All 4 tests PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: phase classifier tags rules as static or dynamic"
```

---

### Task 3: PatchSet — Collected Patches with Conflict Resolution

**Files:**
- Create: `include/mora/eval/patch_set.h`
- Create: `src/eval/patch_set.cpp`
- Create: `tests/patch_set_test.cpp`

The PatchSet collects field-level patches and resolves conflicts via last-write-wins ordering. Each patch targets a (FormID, field) pair with an operation (set, add, remove).

- [ ] **Step 1: Write the failing test**

```cpp
// tests/patch_set_test.cpp
#include <gtest/gtest.h>
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"

class PatchSetTest : public ::testing::Test {
protected:
    mora::StringPool pool;
};

TEST_F(PatchSetTest, AddSinglePatch) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(25), pool.intern("my_mod"), 0);

    auto patches = ps.get_patches_for(0x100);
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].field, mora::FieldId::Damage);
    EXPECT_EQ(patches[0].value.as_int(), 25);
}

TEST_F(PatchSetTest, LastWriteWins) {
    mora::PatchSet ps;
    // Mod at position 1 writes first
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(18), pool.intern("requiem"), 1);
    // Mod at position 2 writes second — wins
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(15), pool.intern("my_mod"), 2);

    auto resolved = ps.resolve();
    auto patches = resolved.get_patches_for(0x100);
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].value.as_int(), 15);
}

TEST_F(PatchSetTest, DifferentFieldsNoConflict) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(15), pool.intern("mod_a"), 1);
    ps.add_patch(0x100, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(pool.intern("Rusty Sword")), pool.intern("mod_b"), 2);

    auto resolved = ps.resolve();
    auto patches = resolved.get_patches_for(0x100);
    EXPECT_EQ(patches.size(), 2u);
}

TEST_F(PatchSetTest, AddKeywordDoesNotConflict) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Keywords, mora::FieldOp::Add,
                 mora::Value::make_formid(0xAAA), pool.intern("mod_a"), 1);
    ps.add_patch(0x100, mora::FieldId::Keywords, mora::FieldOp::Add,
                 mora::Value::make_formid(0xBBB), pool.intern("mod_b"), 2);

    auto resolved = ps.resolve();
    auto patches = resolved.get_patches_for(0x100);
    // Both add operations survive — they don't conflict
    EXPECT_EQ(patches.size(), 2u);
}

TEST_F(PatchSetTest, ConflictReport) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(18), pool.intern("requiem"), 1);
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(15), pool.intern("my_mod"), 2);

    auto resolved = ps.resolve();
    auto conflicts = resolved.get_conflicts();
    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts[0].target_formid, 0x100u);
    EXPECT_EQ(conflicts[0].field, mora::FieldId::Damage);
    EXPECT_EQ(conflicts[0].entries.size(), 2u);
}

TEST_F(PatchSetTest, SortedByFormID) {
    mora::PatchSet ps;
    ps.add_patch(0x300, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(1), pool.intern("mod"), 0);
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(2), pool.intern("mod"), 0);
    ps.add_patch(0x200, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(3), pool.intern("mod"), 0);

    auto all = ps.resolve().all_patches_sorted();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].target_formid, 0x100u);
    EXPECT_EQ(all[1].target_formid, 0x200u);
    EXPECT_EQ(all[2].target_formid, 0x300u);
}

TEST_F(PatchSetTest, TotalPatchCount) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(1), pool.intern("mod"), 0);
    ps.add_patch(0x200, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(pool.intern("x")), pool.intern("mod"), 0);
    EXPECT_EQ(ps.resolve().patch_count(), 2u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build patch_set_test && xmake run patch_set_test`
Expected: FAIL

- [ ] **Step 3: Implement PatchSet**

```cpp
// include/mora/eval/patch_set.h
#pragma once

#include "mora/eval/fact_db.h"
#include "mora/core/string_pool.h"
#include <cstdint>
#include <map>
#include <vector>

namespace mora {

enum class FieldId : uint16_t {
    Name = 1, Damage, ArmorRating, GoldValue, Weight,
    Keywords, Factions, Perks, Spells, Items,
    Level, Race, EditorId,
};

enum class FieldOp : uint8_t {
    Set = 0,    // overwrite value
    Add = 1,    // append to collection
    Remove = 2, // remove from collection
};

struct FieldPatch {
    FieldId field;
    FieldOp op;
    Value value;
    StringId source_mod;  // which mod produced this patch
    uint32_t priority;    // load order position (higher = wins)
};

struct ResolvedPatch {
    uint32_t target_formid;
    std::vector<FieldPatch> fields;
};

struct ConflictEntry {
    Value value;
    StringId source_mod;
    uint32_t priority;
};

struct Conflict {
    uint32_t target_formid;
    FieldId field;
    std::vector<ConflictEntry> entries; // all competing values, ordered by priority
};

class ResolvedPatchSet {
public:
    std::vector<FieldPatch> get_patches_for(uint32_t formid) const;
    std::vector<ResolvedPatch> all_patches_sorted() const;
    std::vector<Conflict> get_conflicts() const;
    size_t patch_count() const;

    // Internal — used by PatchSet::resolve()
    std::map<uint32_t, std::vector<FieldPatch>> patches_;
    std::vector<Conflict> conflicts_;
};

class PatchSet {
public:
    void add_patch(uint32_t target_formid, FieldId field, FieldOp op,
                   Value value, StringId source_mod, uint32_t priority);

    ResolvedPatchSet resolve() const;

private:
    struct RawPatch {
        uint32_t target_formid;
        FieldPatch field_patch;
    };
    std::vector<RawPatch> patches_;
};

} // namespace mora
```

```cpp
// src/eval/patch_set.cpp
#include "mora/eval/patch_set.h"
#include <algorithm>
#include <unordered_map>

namespace mora {

void PatchSet::add_patch(uint32_t target_formid, FieldId field, FieldOp op,
                         Value value, StringId source_mod, uint32_t priority) {
    FieldPatch fp;
    fp.field = field;
    fp.op = op;
    fp.value = std::move(value);
    fp.source_mod = source_mod;
    fp.priority = priority;
    patches_.push_back({target_formid, std::move(fp)});
}

ResolvedPatchSet PatchSet::resolve() const {
    ResolvedPatchSet result;

    // Group by (formid, field)
    struct Key {
        uint32_t formid;
        FieldId field;
        bool operator<(const Key& o) const {
            if (formid != o.formid) return formid < o.formid;
            return static_cast<uint16_t>(field) < static_cast<uint16_t>(o.field);
        }
    };

    std::map<Key, std::vector<const FieldPatch*>> grouped;
    for (const auto& raw : patches_) {
        grouped[{raw.target_formid, raw.field_patch.field}].push_back(&raw.field_patch);
    }

    for (const auto& [key, patches] : grouped) {
        if (patches.size() == 1 || patches[0]->op == FieldOp::Add || patches[0]->op == FieldOp::Remove) {
            // Add/Remove ops don't conflict — keep all
            for (const auto* p : patches) {
                result.patches_[key.formid].push_back(*p);
            }
        } else {
            // Set ops on same field — last-write-wins by priority
            const FieldPatch* winner = patches[0];
            for (const auto* p : patches) {
                if (p->priority >= winner->priority) {
                    winner = p;
                }
            }
            result.patches_[key.formid].push_back(*winner);

            // Record conflict if multiple writers
            if (patches.size() > 1) {
                Conflict c;
                c.target_formid = key.formid;
                c.field = key.field;
                for (const auto* p : patches) {
                    c.entries.push_back({p->value, p->source_mod, p->priority});
                }
                std::sort(c.entries.begin(), c.entries.end(),
                          [](const auto& a, const auto& b) { return a.priority < b.priority; });
                result.conflicts_.push_back(std::move(c));
            }
        }
    }

    return result;
}

std::vector<FieldPatch> ResolvedPatchSet::get_patches_for(uint32_t formid) const {
    auto it = patches_.find(formid);
    if (it == patches_.end()) return {};
    return it->second;
}

std::vector<ResolvedPatch> ResolvedPatchSet::all_patches_sorted() const {
    std::vector<ResolvedPatch> result;
    for (const auto& [formid, fields] : patches_) {
        result.push_back({formid, fields});
    }
    return result;
}

std::vector<Conflict> ResolvedPatchSet::get_conflicts() const {
    return conflicts_;
}

size_t ResolvedPatchSet::patch_count() const {
    size_t count = 0;
    for (const auto& [_, fields] : patches_) {
        count += fields.size();
    }
    return count;
}

} // namespace mora
```

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build patch_set_test && xmake run patch_set_test`
Expected: All 7 tests PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: patch set with field-level conflict resolution"
```

---

### Task 4: Evaluator — Static Rule Evaluation

**Files:**
- Create: `include/mora/eval/evaluator.h`
- Create: `src/eval/evaluator.cpp`
- Create: `tests/evaluator_test.cpp`

The evaluator takes parsed rules and a populated FactDB, evaluates static rules by matching body clauses against the fact database, and produces a PatchSet from the effects.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/evaluator_test.cpp
#include <gtest/gtest.h>
#include "mora/eval/evaluator.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_set.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"

class EvaluatorTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::Module parse(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        auto mod = parser.parse_module();
        mora::NameResolver resolver(pool, diags);
        resolver.resolve(mod);
        return mod;
    }

    // Build a small test FactDB with some NPCs and weapons
    mora::FactDB make_test_db() {
        mora::FactDB db(pool);

        auto npc = pool.intern("npc");
        auto weapon = pool.intern("weapon");
        auto has_faction = pool.intern("has_faction");
        auto has_keyword = pool.intern("has_keyword");
        auto base_level = pool.intern("base_level");

        // Two NPCs
        db.add_fact(npc, {mora::Value::make_formid(0x100)});
        db.add_fact(npc, {mora::Value::make_formid(0x200)});

        // NPC 0x100 is in BanditFaction (0xAAA)
        db.add_fact(has_faction, {mora::Value::make_formid(0x100), mora::Value::make_formid(0xAAA)});

        // NPC 0x100 level 25, NPC 0x200 level 10
        db.add_fact(base_level, {mora::Value::make_formid(0x100), mora::Value::make_int(25)});
        db.add_fact(base_level, {mora::Value::make_formid(0x200), mora::Value::make_int(10)});

        // A weapon
        db.add_fact(weapon, {mora::Value::make_formid(0x300)});
        db.add_fact(has_keyword, {mora::Value::make_formid(0x300), mora::Value::make_formid(0xBBB)}); // silver

        return db;
    }
};

TEST_F(EvaluatorTest, SimpleEffectRule) {
    auto mod = parse(
        "add_kw(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :TestFaction)\n"
        "    => add_keyword(NPC, :TestKeyword)\n"
    );

    auto db = make_test_db();
    // :TestFaction => 0xAAA, :TestKeyword => 0xCCC
    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("TestFaction"), 0xAAA);
    evaluator.set_symbol_formid(pool.intern("TestKeyword"), 0xCCC);

    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();

    // Only NPC 0x100 has faction 0xAAA, so only one patch
    auto patches = resolved.get_patches_for(0x100);
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].field, mora::FieldId::Keywords);
    EXPECT_EQ(patches[0].op, mora::FieldOp::Add);
    EXPECT_EQ(patches[0].value.as_formid(), 0xCCCu);

    // NPC 0x200 should have no patches
    EXPECT_TRUE(resolved.get_patches_for(0x200).empty());
}

TEST_F(EvaluatorTest, DerivedRuleComposition) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "\n"
        "tag_bandit(NPC):\n"
        "    bandit(NPC)\n"
        "    => add_keyword(NPC, :IsBandit)\n"
    );

    auto db = make_test_db();
    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("BanditFaction"), 0xAAA);
    evaluator.set_symbol_formid(pool.intern("IsBandit"), 0xDDD);

    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();

    // NPC 0x100 has BanditFaction, so it gets tagged
    auto patches = resolved.get_patches_for(0x100);
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].value.as_formid(), 0xDDDu);
}

TEST_F(EvaluatorTest, ConditionalEffect) {
    auto mod = parse(
        "bandit_gear(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "    base_level(NPC, Level)\n"
        "    Level >= 20 => add_item(NPC, :SilverSword)\n"
        "    Level < 20 => add_item(NPC, :IronSword)\n"
    );

    auto db = make_test_db();
    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("BanditFaction"), 0xAAA);
    evaluator.set_symbol_formid(pool.intern("SilverSword"), 0xE01);
    evaluator.set_symbol_formid(pool.intern("IronSword"), 0xE02);

    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();

    // NPC 0x100 is level 25 and in BanditFaction — gets SilverSword
    auto patches = resolved.get_patches_for(0x100);
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].value.as_formid(), 0xE01u);

    // NPC 0x200 is not in BanditFaction — no patches
    EXPECT_TRUE(resolved.get_patches_for(0x200).empty());
}

TEST_F(EvaluatorTest, NegationInBody) {
    auto mod = parse(
        "non_silver(W):\n"
        "    weapon(W)\n"
        "    not has_keyword(W, :Silver)\n"
        "    => add_keyword(W, :NonSilver)\n"
    );

    auto db = make_test_db();
    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("Silver"), 0xBBB);
    evaluator.set_symbol_formid(pool.intern("NonSilver"), 0xFFF);

    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();

    // Weapon 0x300 HAS keyword 0xBBB (Silver), so it should NOT match
    EXPECT_TRUE(resolved.get_patches_for(0x300).empty());
}

TEST_F(EvaluatorTest, RuleMatchCount) {
    auto mod = parse(
        "tag_all(NPC):\n"
        "    npc(NPC)\n"
        "    => add_keyword(NPC, :Tagged)\n"
    );

    auto db = make_test_db();
    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("Tagged"), 0xFFF);

    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();

    // 2 NPCs in the DB
    EXPECT_EQ(resolved.patch_count(), 2u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build evaluator_test && xmake run evaluator_test`
Expected: FAIL

- [ ] **Step 3: Implement Evaluator**

```cpp
// include/mora/eval/evaluator.h
#pragma once

#include "mora/ast/ast.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <unordered_map>

namespace mora {

// Maps variable names to bound values during rule evaluation
using Bindings = std::unordered_map<uint32_t, Value>; // key: StringId.index

class Evaluator {
public:
    Evaluator(StringPool& pool, DiagBag& diags, const FactDB& db);

    // Map symbolic references (:EditorID) to FormIDs
    void set_symbol_formid(StringId symbol_name, uint32_t formid);

    // Evaluate all static rules in the module, return patches
    PatchSet evaluate_static(const Module& mod);

private:
    // Evaluate a single rule, adding patches to the PatchSet
    void evaluate_rule(const Rule& rule, PatchSet& patches, uint32_t priority);

    // Recursively match body clauses, collecting bindings.
    // clause_idx is the current clause being matched.
    // On success (all clauses matched), call apply_effects.
    void match_clauses(const Rule& rule, size_t clause_idx,
                       Bindings& bindings, PatchSet& patches,
                       uint32_t priority);

    // Check a single fact pattern against the DB with current bindings
    std::vector<Bindings> match_fact_pattern(const FactPattern& pattern,
                                              const Bindings& bindings);

    // Evaluate a guard expression with current bindings
    bool evaluate_guard(const Expr& expr, const Bindings& bindings);

    // Apply effects with current bindings to produce patches
    void apply_effects(const Rule& rule, const Bindings& bindings,
                       PatchSet& patches, uint32_t priority);

    // Resolve an Expr to a Value using current bindings
    Value resolve_expr(const Expr& expr, const Bindings& bindings);

    // Map an effect action name to a FieldId + FieldOp
    std::pair<FieldId, FieldOp> action_to_field(StringId action) const;

    StringPool& pool_;
    DiagBag& diags_;
    const FactDB& db_;
    FactDB derived_facts_; // facts derived from rules without effects
    std::unordered_map<uint32_t, uint32_t> symbol_formids_; // symbol name -> formid
    StringId current_mod_name_;
};

} // namespace mora
```

Implementation guidance for `src/eval/evaluator.cpp`:

The key algorithm is `match_clauses` which implements a simple Datalog-style bottom-up evaluation:

1. **evaluate_static()**: For each rule in the module, first evaluate derived rules (those without effects) to populate `derived_facts_`, then evaluate rules with effects to produce patches.

2. **evaluate_rule()**: Starts `match_clauses` at clause index 0 with empty bindings.

3. **match_clauses()**: If clause_idx >= body.size(), all clauses matched — call `apply_effects` for unconditional effects and evaluate conditional effects. Otherwise:
   - If clause is a FactPattern (not negated): query the DB (and derived_facts_) for matching tuples, extend bindings for each match, recurse with clause_idx+1.
   - If clause is a negated FactPattern: check that no matching tuple exists; if so, recurse.
   - If clause is a GuardClause: evaluate the expression with current bindings; if true, recurse.

4. **match_fact_pattern()**: Build a query pattern from the fact's args, substituting bound variables. Query both `db_` and `derived_facts_`. For each result, produce a new Bindings with unbound variables filled in.

5. **apply_effects()**: For each effect, resolve args to values, map the action name to FieldId/FieldOp, add to PatchSet.

6. **action_to_field()**: Maps action names like "add_keyword" to (Keywords, Add), "set_damage" to (Damage, Set), "add_item" to (Items, Add), etc.

7. **resolve_expr()**: VariableExpr → look up in bindings. SymbolExpr → look up in symbol_formids_. Literals → direct conversion. BinaryExpr → evaluate recursively.

8. **evaluate_guard()**: Resolve both sides of a comparison, compare. Supports >=, <=, ==, !=, <, > for numeric types.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build evaluator_test && xmake run evaluator_test`
Expected: All 5 tests PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: rule evaluator for static Datalog evaluation against fact database"
```

---

### Task 5: .spatch Binary Writer

**Files:**
- Create: `include/mora/emit/spatch_writer.h`
- Create: `src/emit/spatch_writer.cpp`
- Create: `include/mora/emit/spatch_reader.h`
- Create: `src/emit/spatch_reader.cpp`
- Create: `tests/spatch_roundtrip_test.cpp`

Write and read the `.spatch` binary format. We test via roundtrip: write patches, read them back, verify they match.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/spatch_roundtrip_test.cpp
#include <gtest/gtest.h>
#include "mora/emit/spatch_writer.h"
#include "mora/emit/spatch_reader.h"
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include <sstream>

class SpatchRoundtripTest : public ::testing::Test {
protected:
    mora::StringPool pool;
};

TEST_F(SpatchRoundtripTest, EmptyPatchSet) {
    mora::ResolvedPatchSet resolved;
    std::ostringstream out;
    mora::SpatchWriter writer(pool);
    writer.write(out, resolved, 0x12345678, 0xABCDEF01);

    std::string data = out.str();
    EXPECT_GE(data.size(), 4u); // at least magic bytes

    std::istringstream in(data);
    mora::SpatchReader reader(pool);
    auto result = reader.read(in);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->load_order_hash, 0x12345678u);
    EXPECT_EQ(result->source_hash, 0xABCDEF01u);
    EXPECT_EQ(result->patches.size(), 0u);
}

TEST_F(SpatchRoundtripTest, SinglePatch) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(25), pool.intern("my_mod"), 0);
    auto resolved = ps.resolve();

    std::ostringstream out;
    mora::SpatchWriter writer(pool);
    writer.write(out, resolved, 0, 0);

    std::istringstream in(out.str());
    mora::SpatchReader reader(pool);
    auto result = reader.read(in);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->patches.size(), 1u);
    EXPECT_EQ(result->patches[0].target_formid, 0x100u);
    ASSERT_EQ(result->patches[0].fields.size(), 1u);
    EXPECT_EQ(result->patches[0].fields[0].field, mora::FieldId::Damage);
    EXPECT_EQ(result->patches[0].fields[0].value.as_int(), 25);
}

TEST_F(SpatchRoundtripTest, MultiplePatches) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(25), pool.intern("mod"), 0);
    ps.add_patch(0x100, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(pool.intern("Test Sword")), pool.intern("mod"), 0);
    ps.add_patch(0x200, mora::FieldId::Keywords, mora::FieldOp::Add,
                 mora::Value::make_formid(0xAAA), pool.intern("mod"), 0);
    auto resolved = ps.resolve();

    std::ostringstream out;
    mora::SpatchWriter writer(pool);
    writer.write(out, resolved, 0, 0);

    std::istringstream in(out.str());
    mora::SpatchReader reader(pool);
    auto result = reader.read(in);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->patches.size(), 2u); // 2 FormIDs
}

TEST_F(SpatchRoundtripTest, MagicBytesValidation) {
    std::istringstream in("XXXX"); // wrong magic
    mora::SpatchReader reader(pool);
    auto result = reader.read(in);
    EXPECT_FALSE(result.has_value());
}

TEST_F(SpatchRoundtripTest, FloatValue) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Weight, mora::FieldOp::Set,
                 mora::Value::make_float(3.14), pool.intern("mod"), 0);
    auto resolved = ps.resolve();

    std::ostringstream out;
    mora::SpatchWriter writer(pool);
    writer.write(out, resolved, 0, 0);

    std::istringstream in(out.str());
    mora::SpatchReader reader(pool);
    auto result = reader.read(in);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->patches.size(), 1u);
    EXPECT_DOUBLE_EQ(result->patches[0].fields[0].value.as_float(), 3.14);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build spatch_roundtrip_test && xmake run spatch_roundtrip_test`
Expected: FAIL

- [ ] **Step 3: Implement SpatchWriter and SpatchReader**

```cpp
// include/mora/emit/spatch_writer.h
#pragma once

#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include <ostream>

namespace mora {

class SpatchWriter {
public:
    explicit SpatchWriter(StringPool& pool) : pool_(pool) {}

    void write(std::ostream& out, const ResolvedPatchSet& patches,
               uint64_t load_order_hash, uint64_t source_hash);

private:
    void write_u8(std::ostream& out, uint8_t v);
    void write_u16(std::ostream& out, uint16_t v);
    void write_u32(std::ostream& out, uint32_t v);
    void write_u64(std::ostream& out, uint64_t v);
    void write_f64(std::ostream& out, double v);
    void write_string(std::ostream& out, std::string_view s);
    void write_value(std::ostream& out, const Value& v);

    StringPool& pool_;
};

} // namespace mora
```

```cpp
// include/mora/emit/spatch_reader.h
#pragma once

#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include <istream>
#include <optional>
#include <vector>

namespace mora {

struct SpatchFile {
    uint16_t version;
    uint64_t load_order_hash;
    uint64_t source_hash;
    std::vector<ResolvedPatch> patches;
};

class SpatchReader {
public:
    explicit SpatchReader(StringPool& pool) : pool_(pool) {}

    std::optional<SpatchFile> read(std::istream& in);

private:
    bool read_u8(std::istream& in, uint8_t& v);
    bool read_u16(std::istream& in, uint16_t& v);
    bool read_u32(std::istream& in, uint32_t& v);
    bool read_u64(std::istream& in, uint64_t& v);
    bool read_f64(std::istream& in, double& v);
    bool read_string(std::istream& in, std::string& s);
    bool read_value(std::istream& in, Value& v);

    StringPool& pool_;
};

} // namespace mora
```

Binary format (matching the spec):
- Header: "MORA" (4 bytes), version (u16), load_order_hash (u64), source_hash (u64), patch_count (u32)
- For each patch entry (sorted by FormID): target_formid (u32), field_count (u16), then for each field: field_id (u16), op (u8), value_type (u8), value (variable)
- Value encoding: type byte (0=int, 1=float, 2=string, 3=formid, 4=bool), then the value

Note: the spec mentions a string table, but for Phase 1 we inline strings directly. The string table optimization can be added later.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build spatch_roundtrip_test && xmake run spatch_roundtrip_test`
Expected: All 5 tests PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: .spatch binary writer and reader with roundtrip tests"
```

---

### Task 6: .mora.rt Bytecode Writer

**Files:**
- Create: `include/mora/emit/rt_writer.h`
- Create: `src/emit/rt_writer.cpp`
- Create: `tests/rt_writer_test.cpp`

The `.mora.rt` file stores dynamic rules as bytecode. For Phase 1, we serialize enough metadata for the runtime to know what events to hook and what rules to evaluate. The bytecode VM will be implemented in Plan 5 (SKSE Runtime). For now, we serialize rule names, trigger types, and the raw AST clause/effect data.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/rt_writer_test.cpp
#include <gtest/gtest.h>
#include "mora/emit/rt_writer.h"
#include "mora/eval/phase_classifier.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include "mora/core/string_pool.h"
#include <sstream>

class RtWriterTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::Module parse(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        auto mod = parser.parse_module();
        mora::NameResolver resolver(pool, diags);
        resolver.resolve(mod);
        return mod;
    }
};

TEST_F(RtWriterTest, EmptyOutput) {
    std::ostringstream out;
    mora::RtWriter writer(pool);
    writer.write(out, {});

    auto data = out.str();
    EXPECT_GE(data.size(), 4u); // magic
    EXPECT_EQ(data.substr(0, 4), "MORT");
}

TEST_F(RtWriterTest, WriteDynamicRules) {
    auto mod = parse(
        "dynamic_rule(NPC):\n"
        "    npc(NPC)\n"
        "    current_location(NPC, Loc)\n"
        "    => add_item(NPC, :Goods)\n"
    );

    mora::PhaseClassifier classifier(pool);
    auto classifications = classifier.classify_module(mod);
    ASSERT_EQ(classifications[0].phase, mora::Phase::Dynamic);

    // Collect dynamic rules
    std::vector<const mora::Rule*> dynamic_rules;
    for (size_t i = 0; i < mod.rules.size(); i++) {
        if (classifications[i].phase == mora::Phase::Dynamic) {
            dynamic_rules.push_back(&mod.rules[i]);
        }
    }

    std::ostringstream out;
    mora::RtWriter writer(pool);
    writer.write(out, dynamic_rules);

    auto data = out.str();
    EXPECT_GT(data.size(), 8u); // magic + version + rule_count at minimum
}

TEST_F(RtWriterTest, MagicAndVersion) {
    std::ostringstream out;
    mora::RtWriter writer(pool);
    writer.write(out, {});

    auto data = out.str();
    EXPECT_EQ(data[0], 'M');
    EXPECT_EQ(data[1], 'O');
    EXPECT_EQ(data[2], 'R');
    EXPECT_EQ(data[3], 'T');
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build rt_writer_test && xmake run rt_writer_test`
Expected: FAIL

- [ ] **Step 3: Implement RtWriter**

```cpp
// include/mora/emit/rt_writer.h
#pragma once

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include <ostream>
#include <vector>

namespace mora {

enum class TriggerKind : uint8_t {
    OnDataLoaded = 0,
    OnNpcLoad = 1,
    OnCellChange = 2,
    OnEquip = 3,
    OnQuestUpdate = 4,
};

class RtWriter {
public:
    explicit RtWriter(StringPool& pool) : pool_(pool) {}

    void write(std::ostream& out, const std::vector<const Rule*>& rules);

private:
    TriggerKind infer_trigger(const Rule& rule) const;
    void write_u8(std::ostream& out, uint8_t v);
    void write_u16(std::ostream& out, uint16_t v);
    void write_u32(std::ostream& out, uint32_t v);
    void write_string(std::ostream& out, std::string_view s);

    StringPool& pool_;
};

} // namespace mora
```

Implementation: `infer_trigger` checks which instance facts the rule references:
- `current_location`, `current_cell` → OnNpcLoad
- `equipped` → OnEquip
- `quest_stage` → OnQuestUpdate
- `current_level`, `is_alive` → OnNpcLoad
- default → OnDataLoaded

`write()`: Magic "MORT", version u16 (1), rule_count u32, then for each rule: name (length-prefixed string), trigger (u8), clause_count (u16), action_count (u16). The actual bytecode for clauses/actions is a placeholder for Phase 1 — we write the clause and action counts but leave the bytecode empty (the runtime VM is Plan 5).

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build rt_writer_test && xmake run rt_writer_test`
Expected: All 3 tests PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: .mora.rt bytecode writer for dynamic rules"
```

---

### Task 7: Lock File — Staleness Detection

**Files:**
- Create: `include/mora/emit/lock_file.h`
- Create: `src/emit/lock_file.cpp`

Simple utility: compute hashes of source files and write/read a `.mora.lock` file.

- [ ] **Step 1: Implement lock file**

```cpp
// include/mora/emit/lock_file.h
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>

namespace mora {

struct LockFile {
    uint64_t source_hash = 0;
    uint64_t load_order_hash = 0;

    static uint64_t hash_files(const std::vector<std::filesystem::path>& files);
    static uint64_t hash_string(const std::string& s);

    void write(const std::filesystem::path& path) const;
    static LockFile read(const std::filesystem::path& path);

    bool matches(uint64_t src_hash, uint64_t lo_hash) const;
};

} // namespace mora
```

```cpp
// src/emit/lock_file.cpp
#include "mora/emit/lock_file.h"
#include <fstream>
#include <functional>

namespace mora {

uint64_t LockFile::hash_string(const std::string& s) {
    return std::hash<std::string>{}(s);
}

uint64_t LockFile::hash_files(const std::vector<std::filesystem::path>& files) {
    uint64_t h = 0;
    for (const auto& f : files) {
        std::ifstream in(f);
        if (!in.is_open()) continue;
        std::string content((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
        h ^= hash_string(content) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= hash_string(f.string()) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    return h;
}

void LockFile::write(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&source_hash), sizeof(source_hash));
    out.write(reinterpret_cast<const char*>(&load_order_hash), sizeof(load_order_hash));
}

LockFile LockFile::read(const std::filesystem::path& path) {
    LockFile lf;
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return lf;
    in.read(reinterpret_cast<char*>(&lf.source_hash), sizeof(lf.source_hash));
    in.read(reinterpret_cast<char*>(&lf.load_order_hash), sizeof(lf.load_order_hash));
    return lf;
}

bool LockFile::matches(uint64_t src_hash, uint64_t lo_hash) const {
    return source_hash == src_hash && load_order_hash == lo_hash;
}

} // namespace mora
```

- [ ] **Step 2: Build to verify it compiles**

Run: `xmake build mora_lib`
Expected: compiles

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: lock file for stale cache detection"
```

---

### Task 8: `mora compile` CLI Command

**Files:**
- Modify: `src/main.cpp`

Wire up the full compile pipeline: parse → resolve → type check → classify → evaluate → write .spatch + .mora.rt + .mora.lock. Add progress display for new phases.

- [ ] **Step 1: Implement the compile command**

Add a `run_compile` function in main.cpp that extends the existing `check` pipeline:

After type checking (which already works), add these phases:
1. **Phase classification**: Run PhaseClassifier on all modules, report static vs dynamic counts
2. **Evaluation**: Create a FactDB (empty for now — Plan 3 populates it from ESPs), run Evaluator on static rules
3. **Emit**: Write .spatch via SpatchWriter, .mora.rt via RtWriter, .mora.lock via LockFile
4. **Summary**: Show frozen count, dynamic count, conflicts, file sizes

The compile command should:
- Accept an `--output` flag for the output directory (default: `MoraCache/` in the target directory)
- Run all the check phases first (parse, resolve, type check)
- If check fails with errors, don't proceed to compile
- If check passes, run classify → evaluate → emit
- Display the full progress with timing per phase
- Show the summary from the spec

For now, since we have no ESP data, the FactDB will be empty and the evaluator will produce no patches. That's fine — it proves the pipeline works end-to-end. Plan 3 will populate the FactDB from real ESP data.

Add `#include` directives for the new headers and implement the compile path.

- [ ] **Step 2: Create test data and verify**

Run:
```bash
xmake build mora
xmake run mora compile test_data/
```

Expected output (something like):
```
  mora v0.1.0

  Parsing 2 files                    0ms
  Resolving 5 rules                  0ms
  Type checking 5 rules              0ms
  Classifying 5 rules                0ms
  Evaluating 3 static rules          0ms
  Emitting                           0ms

  ✓ Compiled successfully in 0ms

  Summary:
    3 rules frozen → mora.spatch (X bytes, 0 patches)
    2 rules dynamic → mora.rt
    0 conflicts resolved
    0 errors, 0 warnings
```

Verify that MoraCache/ directory was created with .spatch, .mora.rt, and .mora.lock files.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: mora compile command with full pipeline"
```

---

### Task 9: `mora inspect` CLI Command

**Files:**
- Modify: `src/main.cpp`

Implement the inspect command that reads a .spatch file and displays its contents in human-readable format.

- [ ] **Step 1: Implement inspect command**

The inspect command:
1. Find and read the .spatch file (default location: `MoraCache/mora.spatch` in current dir, or path argument)
2. Use SpatchReader to parse it
3. Display header info (version, hashes, patch count, file size)
4. For each patch: show FormID, field changes with values
5. With `--conflicts` flag: show only the conflict report (requires storing conflicts in the .spatch — for Phase 1, read from mora.log if available, or display "no conflict data available")

Output format matching the spec:
```
  mora.spatch v1 — 0 patches (X bytes)
  load order hash: 00000000
  source hash: 00000000

  (no patches — populate FactDB with ESP data in Plan 3)
```

- [ ] **Step 2: Test it**

Run:
```bash
xmake build mora
xmake run mora compile test_data/
xmake run mora inspect MoraCache/mora.spatch
```

Expected: Shows the spatch header and (empty) patch list.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: mora inspect command for human-readable spatch dump"
```

---

### Task 10: `mora info` CLI Command

**Files:**
- Modify: `src/main.cpp`

Implement the info command that shows project status at a glance.

- [ ] **Step 1: Implement info command**

The info command:
1. Find all .mora files in the target directory
2. Count rules by parsing (lightweight — just count)
3. Check if MoraCache exists and read .mora.lock
4. Compare lock hashes to current source files to determine if cache is stale
5. Display formatted output

Output format:
```
  mora v0.1.0

  Mora rules:    5 across 2 files
  Cache status:  ✓ up to date
    mora.spatch: X bytes
    mora.rt:     X bytes
```

Or if stale:
```
  Cache status:  ✗ stale (source files changed)
```

Or if no cache:
```
  Cache status:  no cache (run 'mora compile')
```

- [ ] **Step 2: Test it**

Run:
```bash
xmake build mora
xmake run mora info test_data/
```

Expected: Shows rule count and cache status.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: mora info command showing project status"
```

---

### Task 11: Backend Integration Test

**Files:**
- Create: `tests/backend_integration_test.cpp`

End-to-end test of the full backend pipeline: parse → resolve → type check → classify → evaluate → write .spatch → read .spatch → verify.

- [ ] **Step 1: Write the integration test**

```cpp
// tests/backend_integration_test.cpp
#include <gtest/gtest.h>
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
#include "mora/eval/phase_classifier.h"
#include "mora/eval/evaluator.h"
#include "mora/eval/fact_db.h"
#include "mora/emit/spatch_writer.h"
#include "mora/emit/spatch_reader.h"
#include <sstream>

class BackendIntegrationTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;
};

TEST_F(BackendIntegrationTest, FullPipeline) {
    // Parse
    std::string source =
        "tag_all_npcs(NPC):\n"
        "    npc(NPC)\n"
        "    => add_keyword(NPC, :Tagged)\n";

    mora::Lexer lexer(source, "test.mora", pool, diags);
    mora::Parser parser(lexer, pool, diags);
    auto mod = parser.parse_module();
    mod.source = source;
    ASSERT_FALSE(diags.has_errors());

    // Resolve + type check
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    ASSERT_FALSE(diags.has_errors());

    // Classify
    mora::PhaseClassifier classifier(pool);
    auto classifications = classifier.classify_module(mod);
    ASSERT_EQ(classifications.size(), 1u);
    EXPECT_EQ(classifications[0].phase, mora::Phase::Static);

    // Populate FactDB with test data
    mora::FactDB db(pool);
    auto npc = pool.intern("npc");
    db.add_fact(npc, {mora::Value::make_formid(0x100)});
    db.add_fact(npc, {mora::Value::make_formid(0x200)});

    // Evaluate
    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("Tagged"), 0xFFF);
    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();

    // Should have patches for both NPCs
    EXPECT_EQ(resolved.patch_count(), 2u);

    // Write .spatch
    std::ostringstream out;
    mora::SpatchWriter writer(pool);
    writer.write(out, resolved, 0, 0);

    // Read it back
    std::istringstream in(out.str());
    mora::SpatchReader reader(pool);
    auto spatch = reader.read(in);
    ASSERT_TRUE(spatch.has_value());
    EXPECT_EQ(spatch->patches.size(), 2u);
}

TEST_F(BackendIntegrationTest, DynamicRulesSkipped) {
    std::string source =
        "static_rule(NPC):\n"
        "    npc(NPC)\n"
        "    => add_keyword(NPC, :Static)\n"
        "\n"
        "dynamic_rule(NPC):\n"
        "    npc(NPC)\n"
        "    current_location(NPC, Loc)\n"
        "    => add_keyword(NPC, :Dynamic)\n";

    mora::Lexer lexer(source, "test.mora", pool, diags);
    mora::Parser parser(lexer, pool, diags);
    auto mod = parser.parse_module();
    mod.source = source;

    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    ASSERT_FALSE(diags.has_errors());

    mora::PhaseClassifier classifier(pool);
    auto classifications = classifier.classify_module(mod);
    EXPECT_EQ(classifications[0].phase, mora::Phase::Static);
    EXPECT_EQ(classifications[1].phase, mora::Phase::Dynamic);

    // Only static rules should produce patches
    mora::FactDB db(pool);
    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x100)});

    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("Static"), 0xAA);
    evaluator.set_symbol_formid(pool.intern("Dynamic"), 0xBB);

    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();

    // Only the static rule's effect should appear
    auto patches = resolved.get_patches_for(0x100);
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].value.as_formid(), 0xAAu);
}

TEST_F(BackendIntegrationTest, ConflictResolution) {
    std::string source =
        "mod_a(W):\n"
        "    weapon(W)\n"
        "    => set_damage(W, :Val18)\n"
        "\n"
        "mod_b(W):\n"
        "    weapon(W)\n"
        "    => set_damage(W, :Val15)\n";

    mora::Lexer lexer(source, "test.mora", pool, diags);
    mora::Parser parser(lexer, pool, diags);
    auto mod = parser.parse_module();
    mod.source = source;

    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);

    mora::FactDB db(pool);
    db.add_fact(pool.intern("weapon"), {mora::Value::make_formid(0x300)});

    mora::Evaluator evaluator(pool, diags, db);
    // For set_damage, the symbol resolves to an int value
    // In practice these would be literal int args, but with symbols
    // we use formids as a proxy for now
    evaluator.set_symbol_formid(pool.intern("Val18"), 18);
    evaluator.set_symbol_formid(pool.intern("Val15"), 15);

    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();

    // Last-write-wins: mod_b (later in file = higher priority) wins
    auto patches = resolved.get_patches_for(0x300);
    ASSERT_GE(patches.size(), 1u);

    auto conflicts = resolved.get_conflicts();
    EXPECT_GE(conflicts.size(), 1u);
}
```

- [ ] **Step 2: Run test**

Run: `xmake build backend_integration_test && xmake run backend_integration_test`
Expected: All 3 tests PASS

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: backend integration tests for full compile pipeline"
```

---

### Task 12: Update xmake.lua

**Files:**
- Modify: `xmake.lua`

Ensure the xmake.lua glob patterns include the new `eval/` and `emit/` directories.

- [ ] **Step 1: Update xmake.lua**

Add `src/eval/*.cpp` and `src/emit/*.cpp` to the mora_lib target's file list:

```lua
target("mora_lib")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
              "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
              "src/cli/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp")
target_end()
```

- [ ] **Step 2: Full build and test**

Run:
```bash
xmake build
xmake test
```
Expected: Everything builds and all tests pass.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "chore: add eval/ and emit/ to xmake build"
```

---

## Summary

This plan delivers:
- **FactDB**: In-memory fact database for rule evaluation (7 tests)
- **PhaseClassifier**: Static vs dynamic rule classification (4 tests)
- **PatchSet**: Field-level patches with conflict resolution (7 tests)
- **Evaluator**: Datalog-style static rule evaluation including derived rules, negation, guards, and conditional effects (5 tests)
- **SpatchWriter/Reader**: Binary .spatch format with roundtrip testing (5 tests)
- **RtWriter**: .mora.rt bytecode serialization (3 tests)
- **LockFile**: Stale cache detection
- **CLI**: `mora compile`, `mora inspect`, `mora info` commands
- **Integration**: Full pipeline test from source to binary (3 tests)

**Not covered (deferred):**
- ESP parser (Plan 3) — FactDB is populated from test data only
- INI importers (Plan 4)
- SKSE runtime plugin (Plan 5) — .mora.rt bytecode VM
- `--watch` mode for compile
- JSON output format

**Dependencies on Plan 1:**
- Lexer, Parser, AST, NameResolver, TypeChecker, DiagBag, StringPool, Progress — all used as-is
- Module.source field — used for diagnostic source lines in evaluator errors

**Note on Task ordering:** Task 12 (xmake.lua update) should actually be done FIRST or alongside Task 1, since the new directories won't be found by the build system otherwise. The implementer for Task 1 should add `src/eval/*.cpp` and `src/emit/*.cpp` to the glob patterns and create placeholder files, or the xmake.lua update should be done before any new files are compiled.
