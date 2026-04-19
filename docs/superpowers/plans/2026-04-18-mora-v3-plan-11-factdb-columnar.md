# Plan 11 — FactDB backed by columns

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace FactDB's per-relation `vector<Tuple>` (via `IndexedRelation`) with `ColumnarRelation` — N typed `Column`s plus hash indexes on selected columns. The Plan 10 `Vector` / `Column` abstractions become the primary storage model. Keep the current `add_fact(Tuple)` / `query(rel, pattern)` public API working via tuple↔column shims; the evaluator and parquet sink don't change in this plan. Also populate `ColumnSpec.type` for the four effect relations so their column types are known at creation time (not inferred from first-insert values).

**Architecture:** `ColumnarRelation` owns N `Column`s (one per arg), plus per-indexed-column hash maps (`value.hash() → row_indices`). `append(Tuple)` routes each `Value` to its column via `Column::append(Value)` and updates the index maps. `query(pattern)` mimics `IndexedRelation::query` but materializes candidate rows from column accessors. `FactDB::get_relation()` returns `std::vector<Tuple>` **by value** (was `const vector<Tuple>&`); callers using `const auto&` still work via temp lifetime extension. The evaluator gets a slight per-call cost for reading tuples; Plan 12's vectorized evaluator skips the tuple shim entirely.

**Tech Stack:** C++20, xmake, gtest.

**Branch:** `mora-v3-foundation`
**Base:** `b1917ca` (HEAD after Plan 10, 24 commits above master)

**Scope note.** Plan 11 is two milestones, two commits:
- **M1:** Introduce `ColumnarRelation` alongside (not replacing) `IndexedRelation`. Dedicated tests. FactDB still uses `IndexedRelation`.
- **M2:** Switch FactDB's internal map to `ColumnarRelation`. Delete `IndexedRelation`. Populate effect-relation column types.

**Not in scope:**
- Vectorized evaluator (Plan 12) — evaluator stays tuple-based, read through the shim.
- Parquet sink reading columns directly (Plan 12 or a polish pass) — still reads via `get_relation()` tuple shim.
- Nominal-tag enforcement on chunk boundaries (Plan 12).
- Richer nominal decode than `type_->name() == "FormID"` — Plan 10's string-compare stands until Plan 12 introduces a `Value::Kind` hint on nominal types if needed.

---

## Milestone 1 — `ColumnarRelation` alongside `IndexedRelation`

Build the new storage class + indexes + query engine. Nothing uses it yet.

### Task 1.1: Create `include/mora/data/columnar_relation.h`

**Files:**
- Create: `include/mora/data/columnar_relation.h`

- [ ] **Step 1: Write the header**

```cpp
#pragma once

#include "mora/core/type.h"
#include "mora/data/column.h"
#include "mora/data/value.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace mora {

// A relation stored as N Columns (one per arg) plus optional hash
// indexes on selected columns. Append-only — rows are added, never
// updated in place.
//
// Plan 11 introduces this class alongside IndexedRelation. Plan 11 M2
// switches FactDB to use it and deletes IndexedRelation.
class ColumnarRelation {
public:
    // `column_types[i]` drives the ith Column's Type (and thus its
    // typed chunk allocation). `indexed_columns` names columns that
    // get a hash-map index alongside the Column.
    ColumnarRelation(std::vector<const Type*> column_types,
                     std::vector<size_t>       indexed_columns);

    size_t arity()     const { return columns_.size(); }
    size_t row_count() const;

    // Append one tuple. The tuple's arity must match `arity()`.
    // Values are routed to columns via `Column::append(Value)`.
    void append(const Tuple& t);

    // Bulk-move append: move tuples from `incoming` and rebuild
    // indexes for the newly-appended range only.
    void absorb(std::vector<Tuple>&& incoming);

    // Read a column by index. Callers that know the physical type
    // may downcast into the typed vector chunks.
    const Column& column(size_t i) const { return *columns_[i]; }

    // Build a materialized Tuple for row `row`. O(arity) Column::at
    // calls. The slow path — Plan 12's vectorized evaluator avoids
    // this entirely.
    Tuple row_at(size_t row) const;

    // Pattern-matching query. Returns matching tuples by value.
    // Uses the best available index; falls back to full scan.
    std::vector<Tuple> query(const Tuple& pattern) const;

    bool contains(const Tuple& values) const;

    // Full materialization — rebuilds a Tuple per row. Used by the
    // FactDB tuple-based shim. Plan 12 callers should read columns
    // directly instead.
    std::vector<Tuple> materialize() const;

private:
    std::vector<std::unique_ptr<Column>> columns_;
    std::vector<size_t>                   indexed_columns_;
    // One hash map per indexed-column entry. Maps Value::hash() to
    // the row indices whose cell in that column hashes to this value.
    std::vector<std::unordered_map<uint64_t, std::vector<uint32_t>>> indexes_;

    int find_index_slot(size_t column) const;
    std::vector<uint32_t> lookup(size_t column, const Value& key) const;
    void index_row(uint32_t row);
};

} // namespace mora
```

- [ ] **Step 2: Don't build yet**

Impl comes in Task 1.2. Header-only add is fine for now.

### Task 1.2: Implement `src/data/columnar_relation.cpp`

**Files:**
- Create: `src/data/columnar_relation.cpp`

- [ ] **Step 1: Write the implementation**

```cpp
#include "mora/data/columnar_relation.h"

#include <cassert>
#include <stdexcept>
#include <utility>

namespace mora {

ColumnarRelation::ColumnarRelation(std::vector<const Type*> column_types,
                                     std::vector<size_t>       indexed_columns)
    : indexed_columns_(std::move(indexed_columns))
    , indexes_(indexed_columns_.size())
{
    columns_.reserve(column_types.size());
    for (const Type* t : column_types) {
        assert(t != nullptr && "ColumnarRelation: column type must not be nullptr");
        columns_.push_back(std::make_unique<Column>(t));
    }
}

size_t ColumnarRelation::row_count() const {
    return columns_.empty() ? 0 : columns_.front()->row_count();
}

void ColumnarRelation::index_row(uint32_t row) {
    for (size_t slot = 0; slot < indexed_columns_.size(); ++slot) {
        size_t const col = indexed_columns_[slot];
        Value const v = columns_[col]->at(row);
        uint64_t const h = v.hash();
        indexes_[slot][h].push_back(row);
    }
}

void ColumnarRelation::append(const Tuple& t) {
    if (t.size() != columns_.size()) {
        throw std::runtime_error("ColumnarRelation::append: tuple arity mismatch");
    }
    auto const row = static_cast<uint32_t>(row_count());
    for (size_t i = 0; i < t.size(); ++i) {
        columns_[i]->append(t[i]);
    }
    index_row(row);
}

void ColumnarRelation::absorb(std::vector<Tuple>&& incoming) {
    auto local = std::move(incoming);
    for (auto& t : local) append(t);
}

Tuple ColumnarRelation::row_at(size_t row) const {
    Tuple t;
    t.reserve(columns_.size());
    for (auto const& c : columns_) t.push_back(c->at(row));
    return t;
}

int ColumnarRelation::find_index_slot(size_t column) const {
    for (size_t slot = 0; slot < indexed_columns_.size(); ++slot) {
        if (indexed_columns_[slot] == column) {
            return static_cast<int>(slot);
        }
    }
    return -1;
}

std::vector<uint32_t> ColumnarRelation::lookup(size_t column,
                                                 const Value& key) const {
    int const slot = find_index_slot(column);
    if (slot < 0) return {};
    uint64_t const h = key.hash();
    auto it = indexes_[static_cast<size_t>(slot)].find(h);
    if (it == indexes_[static_cast<size_t>(slot)].end()) return {};
    // Verify each candidate against the key to filter hash collisions.
    std::vector<uint32_t> result;
    result.reserve(it->second.size());
    for (uint32_t const row : it->second) {
        if (columns_[column]->at(row) == key) {
            result.push_back(row);
        }
    }
    return result;
}

std::vector<Tuple> ColumnarRelation::query(const Tuple& pattern) const {
    // Find the first concrete column with a hash index.
    int best_slot = -1;
    size_t best_col = 0;
    for (size_t col = 0; col < pattern.size(); ++col) {
        if (!pattern[col].is_var()) {
            int const slot = find_index_slot(col);
            if (slot >= 0) {
                best_slot = slot;
                best_col = col;
                break;
            }
        }
    }

    std::vector<uint32_t> candidates;
    if (best_slot >= 0) {
        candidates = lookup(best_col, pattern[best_col]);
    } else {
        candidates.reserve(row_count());
        for (uint32_t i = 0; i < row_count(); ++i) candidates.push_back(i);
    }

    std::vector<Tuple> result;
    result.reserve(candidates.size());
    for (uint32_t const row : candidates) {
        Tuple t = row_at(row);
        bool ok = true;
        for (size_t col = 0; col < pattern.size(); ++col) {
            if (!pattern[col].matches(t[col])) {
                ok = false;
                break;
            }
        }
        if (ok) result.push_back(std::move(t));
    }
    return result;
}

bool ColumnarRelation::contains(const Tuple& values) const {
    return !query(values).empty();
}

std::vector<Tuple> ColumnarRelation::materialize() const {
    std::vector<Tuple> out;
    out.reserve(row_count());
    for (uint32_t i = 0; i < row_count(); ++i) out.push_back(row_at(i));
    return out;
}

} // namespace mora
```

- [ ] **Step 2: Add to `xmake.lua`**

The `mora_lib` target's `src/data` entries are explicit (not a glob). Add `src/data/columnar_relation.cpp` alongside `vector.cpp`, `column.cpp`, etc.

- [ ] **Step 3: Build**

```
xmake build 2>&1 | tail -5
```

Expected: succeeds.

### Task 1.3: Write `tests/data/test_columnar_relation.cpp`

**Files:**
- Create: `tests/data/test_columnar_relation.cpp`

- [ ] **Step 1: Write the tests**

```cpp
#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/columnar_relation.h"
#include "mora/data/value.h"

#include <gtest/gtest.h>

namespace {

TEST(ColumnarRelation, AppendsAndRetrievesRows) {
    auto const* formid  = mora::TypeRegistry::instance().register_nominal(
        "FormID", mora::types::int32());
    mora::ColumnarRelation rel({formid, mora::types::int64()}, /*indexed*/ {0});

    rel.append(mora::Tuple{mora::Value::make_formid(0x100), mora::Value::make_int(42)});
    rel.append(mora::Tuple{mora::Value::make_formid(0x200), mora::Value::make_int(7)});

    EXPECT_EQ(rel.row_count(), 2u);
    EXPECT_EQ(rel.arity(), 2u);

    auto t0 = rel.row_at(0);
    ASSERT_EQ(t0.size(), 2u);
    EXPECT_EQ(t0[0].as_formid(), 0x100u);
    EXPECT_EQ(t0[1].as_int(), 42);

    auto t1 = rel.row_at(1);
    EXPECT_EQ(t1[0].as_formid(), 0x200u);
    EXPECT_EQ(t1[1].as_int(), 7);
}

TEST(ColumnarRelation, QueryByIndexedColumnUsesHashIndex) {
    auto const* formid = mora::TypeRegistry::instance().register_nominal(
        "FormID", mora::types::int32());
    mora::ColumnarRelation rel({formid, mora::types::int64()}, /*indexed*/ {0});

    for (uint32_t f = 0; f < 100; ++f) {
        rel.append(mora::Tuple{mora::Value::make_formid(0x1000 + f),
                                mora::Value::make_int(static_cast<int64_t>(f))});
    }

    mora::Tuple pat{mora::Value::make_formid(0x1000 + 42), mora::Value::make_var()};
    auto result = rel.query(pat);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0][0].as_formid(), 0x1000u + 42);
    EXPECT_EQ(result[0][1].as_int(), 42);
}

TEST(ColumnarRelation, QueryFullScanWhenNoIndexMatches) {
    mora::ColumnarRelation rel({mora::types::int64(), mora::types::int64()},
                                /*indexed*/ {});  // no indexes

    rel.append(mora::Tuple{mora::Value::make_int(1), mora::Value::make_int(10)});
    rel.append(mora::Tuple{mora::Value::make_int(2), mora::Value::make_int(20)});
    rel.append(mora::Tuple{mora::Value::make_int(2), mora::Value::make_int(30)});

    mora::Tuple pat{mora::Value::make_int(2), mora::Value::make_var()};
    auto result = rel.query(pat);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0][1].as_int(), 20);
    EXPECT_EQ(result[1][1].as_int(), 30);
}

TEST(ColumnarRelation, ContainsExactMatch) {
    auto const* formid = mora::TypeRegistry::instance().register_nominal(
        "FormID", mora::types::int32());
    mora::ColumnarRelation rel({formid, mora::types::int64()}, /*indexed*/ {0});

    rel.append(mora::Tuple{mora::Value::make_formid(0x100), mora::Value::make_int(42)});
    EXPECT_TRUE(rel.contains(
        mora::Tuple{mora::Value::make_formid(0x100), mora::Value::make_int(42)}));
    EXPECT_FALSE(rel.contains(
        mora::Tuple{mora::Value::make_formid(0x100), mora::Value::make_int(99)}));
}

TEST(ColumnarRelation, MaterializeReturnsAllRows) {
    mora::ColumnarRelation rel({mora::types::int64()}, /*indexed*/ {});
    for (int64_t i = 0; i < 5; ++i) {
        rel.append(mora::Tuple{mora::Value::make_int(i)});
    }
    auto rows = rel.materialize();
    ASSERT_EQ(rows.size(), 5u);
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(rows[i][0].as_int(), static_cast<int64_t>(i));
    }
}

TEST(ColumnarRelation, KeywordAndAnyColumnsRoundTrip) {
    mora::StringPool pool;
    auto const* formid = mora::TypeRegistry::instance().register_nominal(
        "FormID", mora::types::int32());
    mora::ColumnarRelation rel(
        {formid, mora::types::keyword(), mora::types::any()},
        /*indexed*/ {0});

    rel.append(mora::Tuple{
        mora::Value::make_formid(0xAA),
        mora::Value::make_keyword(pool.intern("GoldValue")),
        mora::Value::make_int(100)});
    rel.append(mora::Tuple{
        mora::Value::make_formid(0xBB),
        mora::Value::make_keyword(pool.intern("Name")),
        mora::Value::make_string(pool.intern("Skeever"))});

    ASSERT_EQ(rel.row_count(), 2u);
    auto r0 = rel.row_at(0);
    EXPECT_EQ(r0[0].as_formid(), 0xAAu);
    EXPECT_EQ(pool.get(r0[1].as_keyword()), "GoldValue");
    EXPECT_EQ(r0[2].as_int(), 100);

    auto r1 = rel.row_at(1);
    EXPECT_EQ(r1[0].as_formid(), 0xBBu);
    EXPECT_EQ(pool.get(r1[1].as_keyword()), "Name");
    EXPECT_EQ(pool.get(r1[2].as_string()), "Skeever");
}

TEST(ColumnarRelation, AbsorbBulkMove) {
    mora::ColumnarRelation rel({mora::types::int64()}, /*indexed*/ {0});
    std::vector<mora::Tuple> batch;
    for (int64_t i = 0; i < 10; ++i) batch.push_back({mora::Value::make_int(i)});
    rel.absorb(std::move(batch));
    EXPECT_EQ(rel.row_count(), 10u);

    auto const hit = rel.query({mora::Value::make_int(7)});
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0][0].as_int(), 7);
}

} // namespace
```

- [ ] **Step 2: Build and run**

```
xmake build 2>&1 | tail -5
xmake run test_columnar_relation 2>&1 | tail -15
```

Expected: 7 test cases pass.

### Task 1.4: M1 commit

- [ ] **Step 1: Full test suite**

```
xmake test 2>&1 | tail -5
```

Expected: 77/77 pass (76 before + 1 new test binary with 7 cases).

- [ ] **Step 2: Commit**

```bash
git add include/mora/data/columnar_relation.h src/data/columnar_relation.cpp \
        tests/data/test_columnar_relation.cpp xmake.lua
git commit -m "$(cat <<'EOF'
mora v3: ColumnarRelation — N Columns + per-column hash indexes

Adds ColumnarRelation alongside IndexedRelation: append-only storage
with one Column per arg (Plan 10 types), optional hash indexes on
selected columns, pattern-based query with index-or-scan fallback.

Nothing uses it yet — Plan 11 M2 switches FactDB to it and deletes
IndexedRelation.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Milestone 2 — FactDB uses `ColumnarRelation`; delete `IndexedRelation`

Switch FactDB's internal storage, add a new `configure_relation` overload that takes column types, populate effect-relation types, delete `IndexedRelation`.

### Task 2.1: Add type-aware `configure_relation` overload

**Files:**
- Modify: `include/mora/eval/fact_db.h`

- [ ] **Step 1: Add new method signature**

Add alongside the existing `configure_relation(StringId, size_t arity, const vector<size_t>& indexes)`:

```cpp
// Configure a relation with explicit column types. Drives ColumnarRelation
// allocation. Preferred over the arity-only overload.
void configure_relation(StringId name,
                         std::vector<const Type*> column_types,
                         const std::vector<size_t>& indexes);
```

Add `#include "mora/core/type.h"` at the top. Keep the arity-only overload for backwards compat; it'll fill column types with `types::any()` placeholders internally.

### Task 2.2: Swap FactDB's storage map to `ColumnarRelation`

**Files:**
- Modify: `include/mora/eval/fact_db.h`
- Modify: `src/eval/fact_db.cpp`

- [ ] **Step 1: Change the storage member**

In `fact_db.h`:

```cpp
#include "mora/data/columnar_relation.h"   // replaces indexed_relation.h
// Delete: #include "mora/data/indexed_relation.h"

// In the class body:
std::unordered_map<uint32_t, ColumnarRelation> relations_;
```

- [ ] **Step 2: Rewrite `configure_relation`**

The arity-only overload becomes:

```cpp
void FactDB::configure_relation(StringId name, size_t arity,
                                  const std::vector<size_t>& indexes) {
    // Backwards compat: unknown column types → Any placeholders.
    std::vector<const Type*> types(arity, types::any());
    configure_relation(name, std::move(types), indexes);
}
```

The new type-aware overload emplaces a `ColumnarRelation`:

```cpp
void FactDB::configure_relation(StringId name,
                                  std::vector<const Type*> column_types,
                                  const std::vector<size_t>& indexes) {
    relations_.try_emplace(name.index,
                            std::move(column_types),
                            std::vector<size_t>(indexes));
}
```

If a relation is already configured and a caller reconfigures, the `try_emplace` silently drops the new schema — same behavior as today. Fine for Plan 11.

- [ ] **Step 3: Rewrite `add_fact`**

```cpp
void FactDB::add_fact(StringId relation, Tuple values) {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) {
        // Auto-vivify with AnyVector columns matching the tuple's arity.
        std::vector<const Type*> types(values.size(), types::any());
        it = relations_.try_emplace(relation.index,
                                     std::move(types),
                                     std::vector<size_t>{}).first;
    }
    it->second.append(values);
}
```

Auto-vivification is a compat quirk today — tests/evaluator call `add_fact` without prior `configure_relation`. Use `types::any()` for unknown shape.

- [ ] **Step 4: Rewrite `query`, `has_fact`, `fact_count`, `get_relation`, `merge_from`**

```cpp
std::vector<Tuple> FactDB::query(StringId relation, const Tuple& pattern) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return {};
    return it->second.query(pattern);
}

bool FactDB::has_fact(StringId relation, const Tuple& values) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return false;
    return it->second.contains(values);
}

size_t FactDB::fact_count(StringId relation) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return 0;
    return it->second.row_count();
}

size_t FactDB::fact_count() const {
    size_t n = 0;
    for (auto const& [_, rel] : relations_) n += rel.row_count();
    return n;
}

std::vector<StringId> FactDB::all_relation_names() const {
    std::vector<StringId> out;
    out.reserve(relations_.size());
    for (auto const& [idx, _] : relations_) out.push_back(StringId{idx});
    return out;
}

// Changed return type: was const vector<Tuple>&, now vector<Tuple> by value.
std::vector<Tuple> FactDB::get_relation(StringId relation) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return {};
    return it->second.materialize();
}

void FactDB::merge_from(FactDB& other) {
    for (auto& [idx, rel] : other.relations_) {
        auto mine = relations_.find(idx);
        if (mine == relations_.end()) {
            // No local relation — absorb via materialize/append using the
            // other side's column types if we need to preserve them. For
            // simplicity in Plan 11, re-create via materialize+append using
            // our auto-vivify path.
            auto rows = rel.materialize();
            auto auto_types = std::vector<const Type*>(rel.arity(), types::any());
            relations_.try_emplace(idx, std::move(auto_types),
                                    std::vector<size_t>{});
            auto& dest = relations_.at(idx);
            for (auto& t : rows) dest.append(t);
        } else {
            mine->second.absorb(rel.materialize());
        }
    }
    other.relations_.clear();
}
```

- [ ] **Step 5: Delete the `empty_` static**

The old `static const std::vector<Tuple> empty_;` is no longer needed — `get_relation` returns a value now. Remove the declaration in the header and the definition in the .cpp.

- [ ] **Step 6: Update `get_relation`'s declaration in the header**

```cpp
// was:  const std::vector<Tuple>& get_relation(StringId relation) const;
//  now:
std::vector<Tuple> get_relation(StringId relation) const;
```

- [ ] **Step 7: Build**

```
xmake build mora_lib 2>&1 | tail -10
```

Expected: `mora_lib` builds. Tests may still fail if they captured `const auto&` vs `const vector<Tuple>&` in ways that don't extend the lifetime (unlikely — `const auto&` extends; `const vector<Tuple>&` on a value return also extends via C++ rules).

Ensure the full build works before moving on:

```
xmake build 2>&1 | tail -10
```

If anything downstream (parquet sink, evaluator) breaks because of the return-type change, fix it at the call site. The parquet sink uses `const auto& tuples = db.get_relation(rel_id)` — that binds to a temporary and extends its lifetime; should work unchanged.

### Task 2.3: Populate effect-relation column types in the evaluator

**Files:**
- Modify: `src/eval/evaluator.cpp`

- [ ] **Step 1: Update `ensure_effect_relations_configured`**

Currently this calls:

```cpp
db.configure_relation(rel, /*arity*/ 3, /*indexed*/ {0});
```

Change to the typed overload. Each effect relation has the same shape: `(FormID, Keyword, Any)`.

```cpp
void Evaluator::ensure_effect_relations_configured(FactDB& db) {
    if (effect_rels_configured_) return;
    effect_rel_set_      = pool_.intern("skyrim/set");
    effect_rel_add_      = pool_.intern("skyrim/add");
    effect_rel_remove_   = pool_.intern("skyrim/remove");
    effect_rel_multiply_ = pool_.intern("skyrim/multiply");

    auto const* formid_t = TypeRegistry::instance().find("FormID");
    // FormID must have been registered by Skyrim at this point. If not,
    // fall back to Int32 — the column still works, just lacks the nominal.
    if (formid_t == nullptr) formid_t = types::int32();

    std::vector<const Type*> effect_cols = {
        formid_t,         // col 0: target FormID
        types::keyword(), // col 1: field keyword
        types::any(),     // col 2: polymorphic value
    };

    for (StringId rel : {effect_rel_set_, effect_rel_add_,
                         effect_rel_remove_, effect_rel_multiply_}) {
        db.configure_relation(rel, effect_cols, /*indexed*/ {0});
    }
    effect_rels_configured_ = true;
}
```

Add `#include "mora/core/type.h"` at the top if not present.

- [ ] **Step 2: Build and run evaluator/fact_db tests**

```
xmake build 2>&1 | tail -5
xmake run fact_db_test && xmake run evaluator_test 2>&1 | tail -20
```

Expected: both pass.

### Task 2.4: Delete `IndexedRelation`

**Files deleted:**
- `include/mora/data/indexed_relation.h`
- `src/data/indexed_relation.cpp`

**File modified:**
- `xmake.lua` — remove `src/data/indexed_relation.cpp` from the `mora_lib` explicit file list.

- [ ] **Step 1: Confirm no remaining consumers**

```
grep -rn 'indexed_relation\|IndexedRelation' src include extensions tests
```

Expected: empty. If anything survived the FactDB swap, fix it first.

- [ ] **Step 2: Delete**

```bash
rm include/mora/data/indexed_relation.h src/data/indexed_relation.cpp
```

- [ ] **Step 3: Update `xmake.lua`**

Remove `src/data/indexed_relation.cpp` from the `add_files` list.

- [ ] **Step 4: Full build + test**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -10
```

Expected: 76/76 pass (77 after M1 - 1 deleted test if indexed_relation has a test; check with `ls tests | grep indexed` before deleting). Actually there's no `tests/indexed_relation_test.cpp` per the investigation — the test count stays 77.

Re-count: after M1 = 77 (76 + test_columnar_relation). After M2 deletes no tests, stays 77.

### Task 2.5: CLI smoke

- [ ] **Step 1: Run on `test_data/minimal`**

```
xmake run mora -- compile test_data/minimal --output-dir /tmp/mora-p11-smoke --sink parquet.snapshot=/tmp/mora-p11-smoke/out 2>&1 | tail -20
echo "exit: $?"
ls /tmp/mora-p11-smoke/
```

Expected: exit 0; parquet files written. Contents should match Plan 10's output — no behavior change, only internal storage changed.

### Task 2.6: M2 commit

- [ ] **Step 1: Final build + test**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -10
```

Expected: 77/77 pass.

- [ ] **Step 2: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: FactDB stores relations columnar; delete IndexedRelation

FactDB's per-relation storage switches from IndexedRelation (vector<Tuple>
+ per-column hash maps) to ColumnarRelation (N Columns + per-column hash
indexes). Plan 10's typed Vector/Column become the primary storage model.

Key API shifts:
- configure_relation() gains a types-aware overload. The arity-only
  overload stays as a backwards-compat path that uses Any columns.
- get_relation() returns vector<Tuple> by value (was const-ref). The
  only caller (parquet sink) uses `const auto&` which extends the
  temporary's lifetime — no call-site changes needed.

Effect relations (skyrim/{set,add,remove,multiply}) now configure with
explicit column types: (FormID, Keyword, Any). Evaluator::ensure_effect_
relations_configured resolves FormID via TypeRegistry.

IndexedRelation (include + impl) deleted. xmake.lua's data-file list
updated.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Verification (end of Plan 11)

1. `grep -rn 'indexed_relation\|IndexedRelation' src include extensions tests` → empty.
2. `xmake test` → 77/77 pass.
3. CLI smoke on `test_data/minimal` exits 0 with parquet output matching Plan 10.
4. Branch is 26 commits above master (24 + 2 Plan 11 commits).
5. `FactDB::get_relation(rel_id)` returns `std::vector<Tuple>` by value.

## Behavior notes

- **Tuple equality semantics unchanged.** `Value::matches` + `Value::operator==` work as before; pattern queries produce the same row set.
- **Order stability.** FactDB row order for a given relation is the insertion order (same as before). Iteration through `materialize()` or `query(all-var pattern)` returns rows in that order.
- **Index rebuild cost.** On `absorb`, the new M2 code calls `append` per tuple (rebuilding indexes incrementally) rather than bulk-appending + single index pass. Slightly slower; acceptable for Plan 11.
- **Auto-vivify of unknown relations.** `add_fact` on an unconfigured relation creates an Any-columned ColumnarRelation. Same semantic as today.

## Forward-looking notes for Plan 12

- Parquet sink still reads through the tuple shim — Plan 12 (or a polish) should switch it to `rel.column(i)` for Arrow zero-copy conversions.
- Evaluator's `merged_query` + `match_fact_pattern` still read tuples — Plan 12's vectorized path reads columns.
- `Column::append` still permissively accepts Int for Int32-nominal-FormID columns — see Plan 10's deferred concern.

## Critical files

- `/home/tbaldrid/oss/mora/include/mora/data/columnar_relation.h` — **new**
- `/home/tbaldrid/oss/mora/src/data/columnar_relation.cpp` — **new**
- `/home/tbaldrid/oss/mora/include/mora/eval/fact_db.h` — **modified** (storage + API)
- `/home/tbaldrid/oss/mora/src/eval/fact_db.cpp` — **rewritten**
- `/home/tbaldrid/oss/mora/src/eval/evaluator.cpp` — **modified** (`ensure_effect_relations_configured` gains typed columns)
- `/home/tbaldrid/oss/mora/include/mora/data/indexed_relation.h` — **deleted** in M2
- `/home/tbaldrid/oss/mora/src/data/indexed_relation.cpp` — **deleted** in M2
- `/home/tbaldrid/oss/mora/tests/data/test_columnar_relation.cpp` — **new** in M1
