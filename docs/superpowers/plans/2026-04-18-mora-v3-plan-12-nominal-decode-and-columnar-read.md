# Plan 12 — Nominal-decode hints + columnar parquet read + `merge_from` type preservation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Address the three deferred concerns from the Plan 10 and Plan 11 reviews without touching the evaluator:
1. Replace the fragile `type_->name() == "FormID"` string-compare in `Column::at` with a `Type::kind_hint()` method.
2. Enforce nominal-tag checks at `Column::append` — values whose kind doesn't fit the column's declared type produce a diagnostic rather than silently coercing.
3. Migrate the parquet sink's per-relation read path from `FactDB::get_relation()` + `materialize()` (rebuilding every Tuple) to `FactDB::get_relation_columnar()` + `column.at(row)` per-cell accesses. Avoids the `vector<Tuple>` allocation and sets up a cleaner foundation for Plan 13.
4. `FactDB::merge_from` preserves the source ColumnarRelation's column types when auto-vivifying the destination, instead of degrading to `Any`.

**Architecture:** `Type` gains a `kind_hint()` method returning the `Value::Kind` that round-trips cleanly through the column. Physical types return their natural kind (`Int32` → `Int`, `String` → `String`, etc.). Nominal types override — `FormID` and its Skyrim peers return `Kind::FormID`. `Column::at` reads the hint and packs a `Value` with the right kind without string compares. `Column::append` compares the input `Value::Kind` against the hint and rejects mismatches unless the column is `Any` (polymorphic). `FactDB` grows a `get_relation_columnar` accessor; the parquet sink uses it.

**Tech Stack:** C++20, xmake, gtest.

**Branch:** `mora-v3-foundation`
**Base:** `9b5e57c` (HEAD after Plan 11 M2, 26 commits above master)

**Scope note.** Plan 12 is polish: it cleans up the Type decode path, plumbs per-column types into one more place (`merge_from`), and swaps one caller (parquet sink) from the tuple shim to the column API. No evaluator changes. Plan 13 is the vectorized evaluator rewrite proper — that's where operator infrastructure + seminaive fixpoint land.

**Not in scope:**
- Vectorized operator framework (Plan 13)
- Vectorized evaluator (Plan 13)
- Seminaive fixpoint driver (Plan 13)
- Arrow zero-copy column-to-array conversion (Plan 13 or later — the parquet sink still walks cells one at a time, but without the tuple-materialization detour)
- Dropping verb keywords from the grammar (Plan 14)

---

## Milestone 1 — `Type::kind_hint()` + Column enforcement

Add a per-type hint for the preferred `Value::Kind`, refactor Column to use it, and enforce it on append.

### Task 1.1: Extend the `Type` API

**Files:**
- Modify: `include/mora/core/type.h`
- Modify: `src/core/type.cpp`

- [ ] **Step 1: Add `Value::Kind kind_hint() const` to `Type`**

Update `include/mora/core/type.h`:

```cpp
#include "mora/data/value.h"   // Value::Kind
// ... existing includes ...

class Type {
public:
    virtual ~Type()                          = default;
    virtual std::string_view name() const    = 0;
    virtual const Type*      physical() const = 0;
    virtual size_t           element_bytes() const = 0;

    // The Value::Kind that naturally round-trips through a Column of
    // this Type. Physical types return the kind matching their payload
    // (Int32 → Int; String → String; Keyword → Keyword; Any → Var,
    // which means "no specific kind — use the cell's stored kind").
    // Nominal types may override to return a different kind (FormID
    // returns Kind::FormID even though its physical is Int32).
    virtual Value::Kind      kind_hint() const = 0;

    bool is_nominal() const { return physical() != this; }

protected:
    Type() = default;
};
```

Yes this introduces a cycle (`type.h` includes `value.h`, and value.h was previously independent). Verify the include order works. If `value.h` transitively includes something that includes `type.h`, we have a cycle — in that case, forward-declare `class Type` in `value.h` and ensure nothing in `value.h` uses `Type`'s definition (only pointer types).

Grep for `#include.*core/type.h` in `value.h` to check. If `value.h` doesn't include `type.h`, adding the reverse include here is safe.

- [ ] **Step 2: Implement `kind_hint()` in `src/core/type.cpp`**

Each `PhysicalType<Name, Bytes>` template instantiation gets an override. Since the template doesn't carry the kind, either:

(a) pass a `Value::Kind` template param alongside Name/Bytes, or
(b) write 8 non-template classes, or
(c) specialize per-name inside the base template.

Pick (a) — minimal changes:

```cpp
template <const char* Name, size_t ElemBytes, Value::Kind K>
class PhysicalType : public Type {
public:
    std::string_view name() const override { return Name; }
    const Type*      physical() const override { return this; }
    size_t           element_bytes() const override { return ElemBytes; }
    Value::Kind      kind_hint() const override { return K; }
};

using Int32Type_   = PhysicalType<kInt32Name,   sizeof(int32_t), Value::Kind::Int>;
using Int64Type_   = PhysicalType<kInt64Name,   sizeof(int64_t), Value::Kind::Int>;
using Float64Type_ = PhysicalType<kFloat64Name, sizeof(double),  Value::Kind::Float>;
using BoolType_    = PhysicalType<kBoolName,    sizeof(bool),    Value::Kind::Bool>;
using StringType_  = PhysicalType<kStringName,  sizeof(uint32_t),Value::Kind::String>;
using KeywordType_ = PhysicalType<kKeywordName, sizeof(uint32_t),Value::Kind::Keyword>;
using BytesType_   = PhysicalType<kBytesName,   0,               Value::Kind::Var>;  // no single kind
using AnyType_     = PhysicalType<kAnyName,     0,               Value::Kind::Var>;  // polymorphic — read from cell
```

`Bytes` and `Any` both map to `Kind::Var` as a sentinel meaning "no specific kind" — Column::at on these types reads the cell's own stored kind rather than applying the hint.

- [ ] **Step 3: NominalType gains a per-instance hint**

Currently `NominalType` mirrors the physical's kind by default. To override (FormID → Kind::FormID), the constructor accepts the hint:

```cpp
class NominalType : public Type {
public:
    NominalType(std::string n, const Type* physical, Value::Kind hint)
        : name_(std::move(n)), physical_(physical), kind_hint_(hint) {}
    std::string_view name()          const override { return name_; }
    const Type*      physical()      const override { return physical_; }
    size_t           element_bytes() const override {
        return physical_->element_bytes();
    }
    Value::Kind      kind_hint()     const override { return kind_hint_; }
private:
    std::string name_;
    const Type* physical_;
    Value::Kind kind_hint_;
};
```

- [ ] **Step 4: Update `TypeRegistry::register_nominal` to take a kind hint**

Add an overload / default parameter:

```cpp
// Register with an explicit kind hint. Idempotent — same (name, physical)
// returns the existing pointer; the new kind_hint is ignored if an entry
// already exists.
const Type* register_nominal(std::string_view name,
                              const Type* physical,
                              Value::Kind kind_hint);

// Back-compat overload: defaults the kind hint to physical->kind_hint().
const Type* register_nominal(std::string_view name, const Type* physical) {
    return register_nominal(name, physical, physical->kind_hint());
}
```

Body of the 3-arg version: same as today, but when inserting a new `NominalType`, pass `kind_hint`.

- [ ] **Step 5: Build**

```
xmake build mora_lib 2>&1 | tail -15
```

Expected: succeeds. If the `<type.h> → <value.h>` include order surfaces a cycle, resolve with a forward declaration as described in Step 1.

### Task 1.2: Update Skyrim nominal registrations

**Files:**
- Modify: `extensions/skyrim_compile/src/types.cpp`

Currently `register_all_nominal_types` calls `ctx.register_nominal_type("FormID", i32)` for 12 Skyrim nominals. All 12 should decode as `Kind::FormID` since they're form-ID-shaped at runtime (Mora has no NpcID/WeaponID/etc. kind — it's all FormID at the Value level).

- [ ] **Step 1: Pass the FormID hint for each registration**

Change the loop body so each call is:

```cpp
ctx.register_nominal_type("FormID", i32, mora::Value::Kind::FormID);
ctx.register_nominal_type("NpcID",  i32, mora::Value::Kind::FormID);
// ... etc
```

This requires `ExtensionContext::register_nominal_type` to accept the hint. Update `include/mora/ext/extension.h` + `src/ext/extension.cpp` to add the 3-arg overload; forward to `TypeRegistry::instance().register_nominal(name, physical, hint)`.

Keep the 2-arg back-compat overload (defaults to `physical->kind_hint()`) so callers that don't care still compile.

- [ ] **Step 2: Build and verify**

```
xmake build 2>&1 | tail -5
```

Expected: succeeds. Test that `TypeRegistry::instance().find("FormID")->kind_hint() == Value::Kind::FormID` in a follow-up test if time permits (can be covered by updating an existing test).

### Task 1.3: Refactor `Column::at` and `Column::append` to use `kind_hint()`

**Files:**
- Modify: `src/data/column.cpp`

- [ ] **Step 1: Simplify `Column::at`**

Replace the `type_->name() == "FormID"` branch with a kind-hint-driven decode. The current shape:

```cpp
if (phys == types::int32()) {
    auto const& c = static_cast<const Int32Vector&>(chunk);
    if (type_->is_nominal() && type_->name() == "FormID") {
        return Value::make_formid(static_cast<uint32_t>(c.get(row_idx)));
    }
    return Value::make_int(c.get(row_idx));
}
```

Becomes:

```cpp
auto const hint = type_->kind_hint();
if (phys == types::int32()) {
    auto const& c = static_cast<const Int32Vector&>(chunk);
    if (hint == Value::Kind::FormID) {
        return Value::make_formid(static_cast<uint32_t>(c.get(row_idx)));
    }
    return Value::make_int(c.get(row_idx));
}
```

Same behavior, no string compare. Every other branch in `Column::at` continues to use the physical's natural decode (Int64 → Int, Float64 → Float, Bool → Bool, String → String, Keyword → Keyword, Any → cell's own kind via `AnyVector::get`).

- [ ] **Step 2: Enforce kind check in `Column::append`**

Replace the permissive append path with a strict one (except for `Any`):

```cpp
void Column::append(const Value& v) {
    auto& chunk = ensure_writable_chunk();
    auto const* phys = type_->physical();
    auto const hint  = type_->kind_hint();

    // AnyVector columns accept any kind.
    if (phys == types::any()) {
        auto& c = static_cast<AnyVector&>(chunk);
        c.append(v);
        return;
    }

    // For typed columns, the value's kind must match the column's hint.
    // Exception: Int32 columns with kind_hint == Int accept FormID (they
    // share the same 32-bit payload). This preserves the Plan 10
    // permissive behavior for untagged Int32 columns used by tests.
    if (hint != Value::Kind::Var && v.kind() != hint) {
        bool const int32_formid_compat =
            (phys == types::int32()) &&
            (hint == Value::Kind::Int) &&
            (v.kind() == Value::Kind::FormID);
        if (!int32_formid_compat) {
            throw std::runtime_error(
                std::string("Column::append: kind mismatch — column '") +
                std::string(type_->name()) +
                "' expects " + std::to_string(static_cast<int>(hint)) +
                " got " + std::to_string(static_cast<int>(v.kind())));
        }
    }

    if (phys == types::int32()) {
        auto& c = static_cast<Int32Vector&>(chunk);
        if (v.kind() == Value::Kind::FormID) {
            c.append(static_cast<int32_t>(v.as_formid()));
        } else {
            c.append(static_cast<int32_t>(v.as_int()));
        }
    } else if (phys == types::int64()) {
        static_cast<Int64Vector&>(chunk).append(v.as_int());
    } else if (phys == types::float64()) {
        static_cast<Float64Vector&>(chunk).append(v.as_float());
    } else if (phys == types::boolean()) {
        static_cast<BoolVector&>(chunk).append(v.as_bool());
    } else if (phys == types::string()) {
        static_cast<StringVector&>(chunk).append(v.as_string());
    } else if (phys == types::keyword()) {
        static_cast<KeywordVector&>(chunk).append(v.as_keyword());
    } else {
        throw std::runtime_error("Column::append: unsupported physical type");
    }
}
```

The `kind_hint() == Value::Kind::Var` sentinel path accepts any kind — this covers `Bytes` (no natural Value kind) and `Any` (already routed above).

- [ ] **Step 3: Build + run the existing Column tests**

```
xmake build 2>&1 | tail -5
xmake run test_column 2>&1 | tail -20
```

Expected: all 6 existing Column tests still pass. The `FormIDNominalDecodesBack` test now exercises the `kind_hint()` path.

### Task 1.4: Add tests for the new hint + enforcement behavior

**Files:**
- Modify: `tests/core/test_type.cpp`
- Modify: `tests/data/test_column.cpp`

- [ ] **Step 1: Add `kind_hint` assertions to `test_type.cpp`**

Add two cases:

```cpp
TEST(CoreType, PhysicalsHaveNaturalKindHints) {
    EXPECT_EQ(mora::types::int32()->kind_hint(),   mora::Value::Kind::Int);
    EXPECT_EQ(mora::types::int64()->kind_hint(),   mora::Value::Kind::Int);
    EXPECT_EQ(mora::types::float64()->kind_hint(), mora::Value::Kind::Float);
    EXPECT_EQ(mora::types::boolean()->kind_hint(), mora::Value::Kind::Bool);
    EXPECT_EQ(mora::types::string()->kind_hint(),  mora::Value::Kind::String);
    EXPECT_EQ(mora::types::keyword()->kind_hint(), mora::Value::Kind::Keyword);
    EXPECT_EQ(mora::types::any()->kind_hint(),     mora::Value::Kind::Var);
    EXPECT_EQ(mora::types::bytes()->kind_hint(),   mora::Value::Kind::Var);
}

TEST(CoreType, NominalCanOverrideKindHint) {
    auto const* tag = mora::TypeRegistry::instance().register_nominal(
        "Plan12NominalTag", mora::types::int32(), mora::Value::Kind::FormID);
    ASSERT_NE(tag, nullptr);
    EXPECT_TRUE(tag->is_nominal());
    EXPECT_EQ(tag->physical(), mora::types::int32());
    EXPECT_EQ(tag->kind_hint(), mora::Value::Kind::FormID);
}
```

- [ ] **Step 2: Add `ColumnAppendRejectsKindMismatch` to `test_column.cpp`**

```cpp
TEST(Column, AppendRejectsKindMismatchOnTypedColumn) {
    mora::Column c(mora::types::string());
    EXPECT_THROW(c.append(mora::Value::make_int(42)),
                 std::runtime_error);
    // Append of the right kind still works
    mora::StringPool pool;
    c.append(mora::Value::make_string(pool.intern("ok")));
    EXPECT_EQ(c.row_count(), 1u);
}

TEST(Column, AnyColumnAcceptsAnyKind) {
    mora::Column c(mora::types::any());
    EXPECT_NO_THROW(c.append(mora::Value::make_int(1)));
    EXPECT_NO_THROW(c.append(mora::Value::make_bool(true)));
    EXPECT_NO_THROW(c.append(mora::Value::make_float(3.14)));
    EXPECT_EQ(c.row_count(), 3u);
}
```

- [ ] **Step 3: Build + run the tests**

```
xmake build 2>&1 | tail -5
xmake run test_type 2>&1 | tail -15
xmake run test_column 2>&1 | tail -15
```

Expected: all type + column tests pass. Total test binary count unchanged at 76; test-case count increases.

### Task 1.5: M1 commit

- [ ] **Step 1: Full test suite**

```
xmake test 2>&1 | tail -5
```

Expected: 76/76 pass.

- [ ] **Step 2: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: Type::kind_hint() — nominal-aware Column encode/decode

Replaces the fragile `type_->name() == "FormID"` string compare in
Column::at with a first-class Type::kind_hint() method. Physical
types return the Value::Kind that naturally round-trips (Int32→Int,
String→String, Keyword→Keyword, etc.); Any/Bytes return Kind::Var as
a "no specific kind" sentinel. Nominal types can override — Skyrim's
12 nominals (FormID, NpcID, WeaponID, ...) now decode as Kind::FormID
via the registration hint.

Column::append enforces the hint: a value whose kind doesn't match
the column's declared type raises runtime_error. AnyVector columns
still accept anything. One compat carve-out: Int32 columns with
Int kind_hint accept FormID values (shared 32-bit payload), matching
Plan 10 behavior.

Addresses Plan 10 M2 review's deferred "string-compare FormID decode"
and "permissive Int/FormID acceptance" concerns.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Milestone 2 — Parquet sink reads columns + `merge_from` preserves types

Expose `FactDB::get_relation_columnar` for column-level access, migrate the parquet sink off the tuple shim, and teach `merge_from` to carry source column types into the destination.

### Task 2.1: Add `FactDB::get_relation_columnar`

**Files:**
- Modify: `include/mora/eval/fact_db.h`
- Modify: `src/eval/fact_db.cpp`

- [ ] **Step 1: Add the accessor**

Header:

```cpp
// Read-only access to the underlying columnar storage. Returns nullptr
// if the relation has no rows and hasn't been configured. Plan 12
// added this as a cheaper path than get_relation() (which materializes
// a tuple vector per call); prefer this when the caller can read
// columns directly.
const ColumnarRelation* get_relation_columnar(StringId relation) const;
```

Add `#include "mora/data/columnar_relation.h"` at the top of `fact_db.h` (not just `value.h`).

Impl:

```cpp
const ColumnarRelation* FactDB::get_relation_columnar(StringId relation) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return nullptr;
    return &it->second;
}
```

- [ ] **Step 2: Build**

```
xmake build mora_lib 2>&1 | tail -5
```

Expected: succeeds.

### Task 2.2: Parquet sink uses the columnar accessor

**Files:**
- Modify: `extensions/parquet/src/snapshot_sink.cpp`

The current call site (line 388): `const auto& tuples = db.get_relation(rel_id);`. Replace with columnar access + per-cell reads.

- [ ] **Step 1: Rewrite the per-relation emit block**

Around line 388, change the flow from "materialize to vector<Tuple>, iterate rows" to "grab ColumnarRelation*, iterate rows via column.at(row_idx, col_idx)". The shape:

```cpp
const auto* rel = db.get_relation_columnar(rel_id);
if (rel == nullptr || rel->row_count() == 0) {
    if (!output_names.has_value()) continue;
    // Output-only mode: emit empty parquet for declared output relations.
    const auto* schema = ctx.extension->find_schema(std::string(rel_name));
    if (schema == nullptr) {
        ctx.diags.warning("parquet-skip-empty-no-schema", /* ... */);
        continue;
    }
    emit_empty_parquet(rel_name, schema->columns.size(), root, ctx.diags);
    continue;
}

const size_t arity    = rel->arity();
const size_t num_rows = rel->row_count();
```

Everywhere the old code read `tuples[row][col]`, change to `rel->column(col).at(row)`. The "arity consistency" check goes away — every row in a ColumnarRelation has exactly `arity()` columns by construction.

The heterogeneity detection loop (per-column kind set) becomes:

```cpp
std::vector<std::unordered_set<mora::Value::Kind>> col_kinds(arity);
for (size_t c = 0; c < arity; ++c) {
    for (size_t r = 0; r < num_rows; ++r) {
        col_kinds[c].insert(rel->column(c).at(r).kind());
    }
}
```

The column-building loops that wrote Arrow arrays used `tuples[row][col].as_X()` — same rewrite: `rel->column(c).at(r).as_X()`.

- [ ] **Step 2: Build**

```
xmake build 2>&1 | tail -10
```

Expected: succeeds.

- [ ] **Step 3: Run parquet tests**

```
xmake run parquet_tests 2>&1 | tail -10
```

Expected: all parquet tests pass with identical output (no behavior change — just an access-path change).

### Task 2.3: `FactDB::merge_from` preserves column types

**Files:**
- Modify: `src/eval/fact_db.cpp`

Currently `merge_from` auto-vivifies the destination with `types::any()` columns regardless of source column types. Fix.

- [ ] **Step 1: Read source schemas from the source ColumnarRelation**

Replace the auto-vivify branch:

```cpp
if (mine == relations_.end()) {
    // No local relation — auto-vivify with Any columns and absorb rows.
    auto rows = rel.materialize();
    std::vector<const Type*> auto_types(rel.arity(), types::any());
    relations_.try_emplace(idx, std::move(auto_types), std::vector<size_t>{});
    auto& dest = relations_.at(idx);
    for (auto& t : rows) dest.append(t);
}
```

With:

```cpp
if (mine == relations_.end()) {
    // No local relation — vivify using the source's column types.
    std::vector<const Type*> src_types;
    src_types.reserve(rel.arity());
    for (size_t i = 0; i < rel.arity(); ++i) {
        src_types.push_back(rel.column(i).type());
    }
    auto [emplaced, _] = relations_.try_emplace(
        idx, std::move(src_types), std::vector<size_t>{});
    emplaced->second.absorb(rel.materialize());
}
```

Use `absorb(materialize())` to rebuild indexes on the appended range. (Plan 11 M2's `absorb` loops through tuples, re-appending via the normal path — matches the Column::append nominal check now enforced.)

- [ ] **Step 2: Build**

```
xmake build mora_lib 2>&1 | tail -5
```

- [ ] **Step 3: Add a targeted test**

**File:** `tests/fact_db_test.cpp` — append a new `TEST` case.

```cpp
TEST(FactDBMergeFromPreservesColumnTypes, MergesSchemaOnNewRelation) {
    mora::StringPool pool;
    auto const* formid = mora::TypeRegistry::instance().register_nominal(
        "FormID", mora::types::int32(), mora::Value::Kind::FormID);

    // Source DB has a typed relation.
    mora::FactDB src(pool);
    auto rel_id = pool.intern("typed_rel");
    src.configure_relation(rel_id, {formid, mora::types::int64()}, /*indexed*/ {0});
    src.add_fact(rel_id, {mora::Value::make_formid(0x100),
                           mora::Value::make_int(42)});

    // Destination is empty; merge should vivify with the source's types.
    mora::FactDB dst(pool);
    dst.merge_from(src);

    auto const* merged = dst.get_relation_columnar(rel_id);
    ASSERT_NE(merged, nullptr);
    ASSERT_EQ(merged->arity(), 2u);
    EXPECT_EQ(merged->column(0).type(), formid);
    EXPECT_EQ(merged->column(1).type(), mora::types::int64());
    // Round-trip: the FormID decode should yield Kind::FormID.
    auto const row = merged->row_at(0);
    EXPECT_EQ(row[0].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(row[0].as_formid(), 0x100u);
    EXPECT_EQ(row[1].as_int(), 42);
}
```

- [ ] **Step 4: Run the test**

```
xmake run fact_db_test 2>&1 | tail -10
```

Expected: all fact_db tests pass.

### Task 2.4: CLI smoke

- [ ] **Step 1: Run on `test_data/minimal`**

```
xmake run mora -- compile test_data/minimal --output-dir /tmp/mora-p12-smoke --sink parquet.snapshot=/tmp/mora-p12-smoke/out 2>&1 | tail -20
echo "exit: $?"
ls /tmp/mora-p12-smoke/
```

Expected: exit 0. Parquet file count matches Plan 11's smoke (9 files). Content should be byte-identical — this plan changes internals, not output semantics.

### Task 2.5: M2 commit

- [ ] **Step 1: Full test suite**

```
xmake test 2>&1 | tail -5
```

Expected: 76/76 pass.

- [ ] **Step 2: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: parquet sink reads columns; merge_from preserves column types

FactDB gains `get_relation_columnar(rel_id)` — a direct accessor to
the underlying ColumnarRelation. The parquet snapshot sink switches
from `get_relation()` + `materialize()` (rebuilt tuple vector per
relation) to per-cell `column(c).at(r)` reads, dropping the
temporary vector allocation.

FactDB::merge_from now vivifies destination relations with the
source's column types rather than degrading to Any. Plan 11's
reviewer flagged this as a gap; the fix is trivial once
ColumnarRelation exposes per-column types.

Added one FactDB test confirming type preservation across merge.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Verification (end of Plan 12)

1. `grep -rn 'type_->name() == "FormID"' src include extensions` is empty. The string-compare decode is gone.
2. `xmake test` passes 76/76.
3. CLI smoke on `test_data/minimal` produces byte-identical parquet output to Plan 11.
4. Branch is 28 commits above master (26 + 2 Plan 12 commits).
5. `FactDB::get_relation_columnar(rel_id)` is public and used by the parquet sink.
6. `FactDB::merge_from` test passes — destination relation has the source's column types.

## Forward-looking notes for Plan 13

- **Evaluator still uses `get_relation()` + `materialize()`** via `merged_query`. Plan 13's vectorized rewrite replaces this with operator-based chunk iteration.
- **Parquet sink still walks cells one at a time** (`column(c).at(r)` per cell). True Arrow zero-copy would read whole chunks (`Int32Vector::data()`) into Arrow arrays. Left for Plan 13 or a later polish.
- **`Column::append`'s strict kind check may surface latent bugs** in tests that fed mismatched kinds. If any Plan 12 test breakage is resolved by adding the `int32_formid_compat` carve-out elsewhere (e.g. a test schema using Int32 for data that should have been FormID-nominal), note it for Plan 13 to clean up.

## Critical files

- `/home/tbaldrid/oss/mora/include/mora/core/type.h` — **modified**, `kind_hint()` added.
- `/home/tbaldrid/oss/mora/src/core/type.cpp` — **modified**, template + NominalType updated.
- `/home/tbaldrid/oss/mora/include/mora/ext/extension.h` — **modified**, 3-arg `register_nominal_type` overload.
- `/home/tbaldrid/oss/mora/extensions/skyrim_compile/src/types.cpp` — **modified**, pass `Kind::FormID` hint.
- `/home/tbaldrid/oss/mora/src/data/column.cpp` — **modified**, `kind_hint`-driven encode/decode, strict append check.
- `/home/tbaldrid/oss/mora/include/mora/eval/fact_db.h` — **modified**, `get_relation_columnar` accessor.
- `/home/tbaldrid/oss/mora/src/eval/fact_db.cpp` — **modified**, accessor + `merge_from` type preservation.
- `/home/tbaldrid/oss/mora/extensions/parquet/src/snapshot_sink.cpp` — **modified**, columnar read path.
- `/home/tbaldrid/oss/mora/tests/core/test_type.cpp` + `tests/data/test_column.cpp` + `tests/fact_db_test.cpp` — **tests added**.
