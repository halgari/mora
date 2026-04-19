# Plan 10 — Typed Vector + Column infrastructure

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce the typed `Vector` and `Column` abstractions — chunk-oriented, `Type*`-driven, with 8 built-in physical subclasses (`Int32Vector`, `Int64Vector`, `Float64Vector`, `BoolVector`, `StringVector`, `KeywordVector`, `BytesVector`, `AnyVector`). Delete the dead pre-v3 `columnar_relation` / `chunk_pool` / `operators` / `pipeline` modules; they were never wired into anything and don't match the spec. Nothing in FactDB / evaluator / parquet changes yet — that's Plan 11.

**Architecture:** Each `Vector` owns a single chunk of like-typed data (`kChunkSize = 2048` rows). Typed subclasses expose row-level read and append. `AnyVector` is the escape hatch for polymorphic columns: it stores `(kind, payload)` rows with a compact tagged-union layout that mirrors `Value::Kind`. `Column` = `const Type*` + `std::vector<std::unique_ptr<Vector>>` chunks. Vector exposes a minimal generic interface; typed access happens via `static_cast<Int32Vector&>(*v)` — there's no `std::visit`-style dispatch at the vector level; callers that know the column type downcast, callers that don't use `AnyVector`.

**Tech Stack:** C++20, xmake, gtest.

**Branch:** `mora-v3-foundation`
**Base:** `a64811c` (HEAD after Plan 9 + arity polish, 23 commits above master)

---

## Scope note

**Narrow.** Plan 10 is additive (Vector + Column) plus a cleanup pass (deleting dead modules). No FactDB or evaluator changes. Plan 11 does the FactDB migration that makes these new types the primary storage model.

**Not in scope:**
- FactDB storing columns (Plan 11)
- Vectorized operators / evaluator rewrite (Plan 12)
- Nominal-tag enforcement on chunk boundaries (Plan 12, part of the vectorized eval)
- Structural conversions (`Int32 → Int64 → Float64`) (Plan 12)
- RLE / offset / dictionary vector encodings (post-v3 optimization)

---

## Task 1: Delete dead pre-v3 columnar modules

**Files deleted:**
- `src/data/columnar_relation.cpp`
- `src/data/chunk_pool.cpp`
- `include/mora/data/columnar_relation.h`
- `include/mora/data/chunk_pool.h`
- `include/mora/data/chunk.h`
- `include/mora/eval/operators.h`
- `include/mora/eval/pipeline.h`
- `tests/columnar_relation_test.cpp`
- `tests/chunk_pool_test.cpp`
- `tests/pipeline_test.cpp`
- `tests/operators_test.cpp`

**File modified:**
- `xmake.lua` — has explicit non-glob references to `src/data/chunk_pool.cpp`, `src/data/columnar_relation.cpp` around line 177. Remove those two list items. The rest of `src/data/*.cpp` is covered by explicit file lines; after deletion they simply vanish.

- [ ] **Step 1: Verify these files are dead (no `src/` consumers)**

```
grep -rln 'columnar_relation\|chunk_pool\|include/mora/eval/operators\|include/mora/eval/pipeline\|include/mora/data/chunk\.h' src include extensions
```

Expected output: only the files being deleted themselves (the four header/impl pairs). No consumers in `src/` or `extensions/`.

If anything outside those paths shows up — STOP and report. Something's using this code.

- [ ] **Step 2: Delete the files**

```bash
rm src/data/columnar_relation.cpp src/data/chunk_pool.cpp \
   include/mora/data/columnar_relation.h include/mora/data/chunk_pool.h \
   include/mora/data/chunk.h \
   include/mora/eval/operators.h include/mora/eval/pipeline.h \
   tests/columnar_relation_test.cpp tests/chunk_pool_test.cpp \
   tests/pipeline_test.cpp tests/operators_test.cpp
```

- [ ] **Step 3: Update `xmake.lua`**

Find the `mora_lib` target's `add_files(...)` block (around line 173). Remove the two explicit entries:

```lua
"src/data/chunk_pool.cpp", "src/data/columnar_relation.cpp",
```

Keep the rest of the `src/data/*.cpp` list as-is.

- [ ] **Step 4: Build**

```
xmake build 2>&1 | tail -5
```

Expected: succeeds. 78 tests become 74 (four test binaries removed: columnar_relation, chunk_pool, pipeline, operators).

---

## Task 2: Create `include/mora/data/vector.h`

**Files:**
- Create: `include/mora/data/vector.h`

The Vector base + 8 typed subclasses. Keep the interface thin — callers that know the type downcast; callers that don't use `AnyVector`.

- [ ] **Step 1: Write the header**

```cpp
#pragma once

#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/value.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mora {

inline constexpr size_t kChunkSize = 2048;

// A Vector owns a single chunk of like-typed rows. It knows its Type
// (identity, not content) and its logical size. Typed access is via
// downcast; the base exposes just type(), size(), capacity(), and a
// reserve hook.
class Vector {
public:
    virtual ~Vector() = default;
    virtual const Type* type() const = 0;
    virtual size_t      size() const = 0;
    virtual size_t      capacity() const { return kChunkSize; }
    virtual void        reserve(size_t n) = 0;
    // No Value-level read/write on the base — callers downcast.
};

// Concrete typed vectors ----------------------------------------------
// Each holds a std::vector<T> underneath, pre-reserved to kChunkSize.
// `append(T)` pushes; `get(size_t)` returns the cell by value. No mutation
// of existing rows — vectors are write-once-per-row for now.

class Int32Vector : public Vector {
public:
    Int32Vector();
    const Type* type() const override;
    size_t      size() const override { return data_.size(); }
    void        reserve(size_t n) override;
    void        append(int32_t v);
    int32_t     get(size_t i) const { return data_[i]; }
    // Raw span for vectorized consumers (Plan 12+).
    const int32_t* data() const { return data_.data(); }
private:
    std::vector<int32_t> data_;
};

class Int64Vector : public Vector {
public:
    Int64Vector();
    const Type* type() const override;
    size_t      size() const override { return data_.size(); }
    void        reserve(size_t n) override;
    void        append(int64_t v);
    int64_t     get(size_t i) const { return data_[i]; }
    const int64_t* data() const { return data_.data(); }
private:
    std::vector<int64_t> data_;
};

class Float64Vector : public Vector {
public:
    Float64Vector();
    const Type* type() const override;
    size_t      size() const override { return data_.size(); }
    void        reserve(size_t n) override;
    void        append(double v);
    double      get(size_t i) const { return data_[i]; }
    const double* data() const { return data_.data(); }
private:
    std::vector<double> data_;
};

class BoolVector : public Vector {
public:
    BoolVector();
    const Type* type() const override;
    size_t      size() const override { return size_; }
    void        reserve(size_t n) override;
    void        append(bool v);
    bool        get(size_t i) const;
private:
    // Plain std::vector<uint8_t> keeps each row addressable without
    // the std::vector<bool> proxy surprises. Upgrade to a bit-packed
    // layout later if profiling demands it.
    std::vector<uint8_t> data_;
    size_t size_ = 0;
};

class StringVector : public Vector {
public:
    StringVector();
    const Type* type() const override;
    size_t      size() const override { return data_.size(); }
    void        reserve(size_t n) override;
    void        append(StringId id);
    StringId    get(size_t i) const { return data_[i]; }
private:
    std::vector<StringId> data_;
};

class KeywordVector : public Vector {
public:
    KeywordVector();
    const Type* type() const override;
    size_t      size() const override { return data_.size(); }
    void        reserve(size_t n) override;
    void        append(StringId id);
    StringId    get(size_t i) const { return data_[i]; }
private:
    std::vector<StringId> data_;
};

class BytesVector : public Vector {
public:
    BytesVector();
    const Type* type() const override;
    size_t      size() const override { return offsets_.size() - 1; }
    void        reserve(size_t n) override;
    void        append(const uint8_t* bytes, size_t n);
    // Returns a pointer to row `i`'s bytes plus its length. Pointer
    // is invalidated on the next append.
    const uint8_t* data(size_t i, size_t* out_len) const;
private:
    std::vector<uint8_t> bytes_;
    std::vector<size_t>  offsets_{0};  // [0, off1, off2, ...]; size = N+1
};

// AnyVector — polymorphic. Stores a kind tag + one payload column per
// physical type. Only the payload matching each row's kind is valid;
// the others are don't-care. Built for the Plan 6 tagged-column
// encoding idea, realized at the Vector level.
//
// `append(Value)` routes by `v.kind()`. `get(i)` returns a Value.
//
// This is the slow path. Prefer typed vectors when the column type
// is known statically.
class AnyVector : public Vector {
public:
    AnyVector();
    const Type* type() const override;
    size_t      size() const override { return kinds_.size(); }
    void        reserve(size_t n) override;

    void   append(const Value& v);
    Value  get(size_t i) const;
    Value::Kind kind_at(size_t i) const { return kinds_[i]; }

private:
    std::vector<Value::Kind> kinds_;
    // Per-kind payload columns. For each row i, exactly one entry
    // matches kinds_[i]; positions in other columns are placeholders.
    // Tracking positions per-kind is cheaper than a universal payload
    // union; typed consumers just index into the matching column.
    std::vector<int64_t>  int_payloads_;      // indexed by seq of Int/FormID rows
    std::vector<double>   float_payloads_;
    std::vector<uint32_t> string_payloads_;   // indexed by seq of String/Keyword rows
    std::vector<uint8_t>  bool_payloads_;
    // Dense position-in-kind map: for row i, which index into the
    // matching payload column to read.
    std::vector<uint32_t> payload_idx_;
};

} // namespace mora
```

- [ ] **Step 2: Don't build yet**

The impl (`src/data/vector.cpp`) comes in Task 3. Header alone is fine.

---

## Task 3: Implement `src/data/vector.cpp`

**Files:**
- Create: `src/data/vector.cpp`

- [ ] **Step 1: Write the implementation**

```cpp
#include "mora/data/vector.h"

namespace mora {

// -- Int32Vector ------------------------------------------------------
Int32Vector::Int32Vector() { data_.reserve(kChunkSize); }
const Type* Int32Vector::type() const { return types::int32(); }
void Int32Vector::reserve(size_t n) { data_.reserve(n); }
void Int32Vector::append(int32_t v) { data_.push_back(v); }

// -- Int64Vector ------------------------------------------------------
Int64Vector::Int64Vector() { data_.reserve(kChunkSize); }
const Type* Int64Vector::type() const { return types::int64(); }
void Int64Vector::reserve(size_t n) { data_.reserve(n); }
void Int64Vector::append(int64_t v) { data_.push_back(v); }

// -- Float64Vector ----------------------------------------------------
Float64Vector::Float64Vector() { data_.reserve(kChunkSize); }
const Type* Float64Vector::type() const { return types::float64(); }
void Float64Vector::reserve(size_t n) { data_.reserve(n); }
void Float64Vector::append(double v) { data_.push_back(v); }

// -- BoolVector -------------------------------------------------------
BoolVector::BoolVector() { data_.reserve(kChunkSize); }
const Type* BoolVector::type() const { return types::boolean(); }
void BoolVector::reserve(size_t n) { data_.reserve(n); }
void BoolVector::append(bool v) {
    data_.push_back(v ? 1 : 0);
    ++size_;
}
bool BoolVector::get(size_t i) const { return data_[i] != 0; }

// -- StringVector -----------------------------------------------------
StringVector::StringVector() { data_.reserve(kChunkSize); }
const Type* StringVector::type() const { return types::string(); }
void StringVector::reserve(size_t n) { data_.reserve(n); }
void StringVector::append(StringId id) { data_.push_back(id); }

// -- KeywordVector ----------------------------------------------------
KeywordVector::KeywordVector() { data_.reserve(kChunkSize); }
const Type* KeywordVector::type() const { return types::keyword(); }
void KeywordVector::reserve(size_t n) { data_.reserve(n); }
void KeywordVector::append(StringId id) { data_.push_back(id); }

// -- BytesVector ------------------------------------------------------
BytesVector::BytesVector() {
    bytes_.reserve(kChunkSize * 16);  // rough — average 16 bytes/row
    offsets_.reserve(kChunkSize + 1);
}
const Type* BytesVector::type() const { return types::bytes(); }
void BytesVector::reserve(size_t n) {
    bytes_.reserve(n * 16);
    offsets_.reserve(n + 1);
}
void BytesVector::append(const uint8_t* bytes, size_t n) {
    bytes_.insert(bytes_.end(), bytes, bytes + n);
    offsets_.push_back(bytes_.size());
}
const uint8_t* BytesVector::data(size_t i, size_t* out_len) const {
    size_t const start = offsets_[i];
    size_t const end   = offsets_[i + 1];
    *out_len = end - start;
    return bytes_.data() + start;
}

// -- AnyVector --------------------------------------------------------
AnyVector::AnyVector() {
    kinds_.reserve(kChunkSize);
    payload_idx_.reserve(kChunkSize);
}
const Type* AnyVector::type() const { return types::any(); }
void AnyVector::reserve(size_t n) {
    kinds_.reserve(n);
    payload_idx_.reserve(n);
}

void AnyVector::append(const Value& v) {
    auto const kind = v.kind();
    kinds_.push_back(kind);
    switch (kind) {
        case Value::Kind::Int:
            payload_idx_.push_back(static_cast<uint32_t>(int_payloads_.size()));
            int_payloads_.push_back(v.as_int());
            break;
        case Value::Kind::FormID:
            payload_idx_.push_back(static_cast<uint32_t>(int_payloads_.size()));
            int_payloads_.push_back(static_cast<int64_t>(v.as_formid()));
            break;
        case Value::Kind::Float:
            payload_idx_.push_back(static_cast<uint32_t>(float_payloads_.size()));
            float_payloads_.push_back(v.as_float());
            break;
        case Value::Kind::String:
            payload_idx_.push_back(static_cast<uint32_t>(string_payloads_.size()));
            string_payloads_.push_back(v.as_string().index);
            break;
        case Value::Kind::Keyword:
            payload_idx_.push_back(static_cast<uint32_t>(string_payloads_.size()));
            string_payloads_.push_back(v.as_keyword().index);
            break;
        case Value::Kind::Bool:
            payload_idx_.push_back(static_cast<uint32_t>(bool_payloads_.size()));
            bool_payloads_.push_back(v.as_bool() ? 1 : 0);
            break;
        case Value::Kind::Var:
        case Value::Kind::List:
            // Not expected in FactDB columns; drop to placeholder.
            payload_idx_.push_back(0);
            break;
    }
}

Value AnyVector::get(size_t i) const {
    auto const kind = kinds_[i];
    auto const idx  = payload_idx_[i];
    switch (kind) {
        case Value::Kind::Int:     return Value::make_int(int_payloads_[idx]);
        case Value::Kind::FormID:  return Value::make_formid(
                                       static_cast<uint32_t>(int_payloads_[idx]));
        case Value::Kind::Float:   return Value::make_float(float_payloads_[idx]);
        case Value::Kind::String:  return Value::make_string(StringId{string_payloads_[idx]});
        case Value::Kind::Keyword: return Value::make_keyword(StringId{string_payloads_[idx]});
        case Value::Kind::Bool:    return Value::make_bool(bool_payloads_[idx] != 0);
        case Value::Kind::Var:     return Value::make_var();
        case Value::Kind::List:    return Value::make_var();  // not supported
    }
    return Value::make_var();
}

} // namespace mora
```

- [ ] **Step 2: Build**

```
xmake build 2>&1 | tail -5
```

Expected: succeeds. `src/data/vector.cpp` is already in the explicit file list if `xmake.lua` needs it; otherwise it's picked up by a glob. Check the investigation note: `src/data/*.cpp` at line 177 now expands to `value.cpp`, `schema_registry.cpp`, `indexed_relation.cpp`, `vector.cpp`. Verify by `xmake build -v 2>&1 | grep vector` that the new file is being compiled.

**IF xmake uses explicit file lists** (no glob) for `src/data/`, add an entry for `src/data/vector.cpp`. Inspect lines 173–181 of `xmake.lua`.

---

## Task 4: Create `include/mora/data/column.h` + `src/data/column.cpp`

**Files:**
- Create: `include/mora/data/column.h`
- Create: `src/data/column.cpp`

A Column is `Type*` + an append-only vector of `unique_ptr<Vector>` chunks. When the current chunk fills (`size() == kChunkSize`), the next append allocates a new chunk of the matching type.

- [ ] **Step 1: Write the header**

```cpp
#pragma once

#include "mora/core/type.h"
#include "mora/data/value.h"
#include "mora/data/vector.h"

#include <memory>
#include <vector>

namespace mora {

// A Column is Type + chunks. Each chunk is a typed Vector holding up
// to kChunkSize rows. Appends allocate a new chunk lazily when the
// current one fills. The Column owns its chunks.
//
// No random-access `set` — columns are append-only by design. Updates
// happen at the relation level by rewriting affected rows.
class Column {
public:
    explicit Column(const Type* type);

    const Type* type()        const { return type_; }
    size_t      row_count()   const;
    size_t      chunk_count() const { return chunks_.size(); }

    // Access a chunk by index. Callers that know the type downcast
    // (e.g. `static_cast<const Int32Vector&>(col.chunk(0))`).
    Vector&       chunk(size_t i)       { return *chunks_[i]; }
    const Vector& chunk(size_t i) const { return *chunks_[i]; }

    // Append a Value, routed by this column's Type. For physical
    // types, the Value's kind must match (Int32 column accepts Int
    // values, etc.); mismatches throw via assertion in debug. For
    // nominal types, the physical match determines routing (FormID
    // nominal over Int32 accepts FormID or Int values).
    //
    // AnyVector columns accept any kind.
    void append(const Value& v);

    // Row-level read. Slow path — rebuilds a Value from the column's
    // typed storage. Plan 12's vectorized evaluator avoids this.
    Value at(size_t row) const;

private:
    const Type*                        type_;
    std::vector<std::unique_ptr<Vector>> chunks_;

    Vector& ensure_writable_chunk();
    std::unique_ptr<Vector> make_chunk() const;
};

} // namespace mora
```

- [ ] **Step 2: Write the implementation**

```cpp
// src/data/column.cpp
#include "mora/data/column.h"

#include <cassert>
#include <stdexcept>

namespace mora {

Column::Column(const Type* type) : type_(type) {
    assert(type_ != nullptr);
}

size_t Column::row_count() const {
    size_t n = 0;
    for (auto const& c : chunks_) n += c->size();
    return n;
}

std::unique_ptr<Vector> Column::make_chunk() const {
    auto const* phys = type_->physical();
    if (phys == types::int32())   return std::make_unique<Int32Vector>();
    if (phys == types::int64())   return std::make_unique<Int64Vector>();
    if (phys == types::float64()) return std::make_unique<Float64Vector>();
    if (phys == types::boolean()) return std::make_unique<BoolVector>();
    if (phys == types::string())  return std::make_unique<StringVector>();
    if (phys == types::keyword()) return std::make_unique<KeywordVector>();
    if (phys == types::bytes())   return std::make_unique<BytesVector>();
    if (phys == types::any())     return std::make_unique<AnyVector>();
    throw std::runtime_error("Column::make_chunk: unknown physical type");
}

Vector& Column::ensure_writable_chunk() {
    if (chunks_.empty() || chunks_.back()->size() >= kChunkSize) {
        chunks_.push_back(make_chunk());
    }
    return *chunks_.back();
}

void Column::append(const Value& v) {
    auto& chunk = ensure_writable_chunk();
    auto const* phys = type_->physical();

    if (phys == types::int32()) {
        // Int32 column accepts Int (narrowed) or FormID (already 32-bit nominal).
        auto& c = static_cast<Int32Vector&>(chunk);
        if (v.kind() == Value::Kind::FormID) c.append(static_cast<int32_t>(v.as_formid()));
        else                                  c.append(static_cast<int32_t>(v.as_int()));
    } else if (phys == types::int64()) {
        auto& c = static_cast<Int64Vector&>(chunk);
        c.append(v.as_int());
    } else if (phys == types::float64()) {
        auto& c = static_cast<Float64Vector&>(chunk);
        c.append(v.as_float());
    } else if (phys == types::boolean()) {
        auto& c = static_cast<BoolVector&>(chunk);
        c.append(v.as_bool());
    } else if (phys == types::string()) {
        auto& c = static_cast<StringVector&>(chunk);
        c.append(v.as_string());
    } else if (phys == types::keyword()) {
        auto& c = static_cast<KeywordVector&>(chunk);
        c.append(v.as_keyword());
    } else if (phys == types::any()) {
        auto& c = static_cast<AnyVector&>(chunk);
        c.append(v);
    } else {
        throw std::runtime_error("Column::append: unsupported physical type");
    }
}

Value Column::at(size_t row) const {
    size_t const chunk_idx = row / kChunkSize;
    size_t const row_idx   = row % kChunkSize;
    auto const& chunk = *chunks_[chunk_idx];
    auto const* phys  = type_->physical();

    if (phys == types::int32()) {
        auto const& c = static_cast<const Int32Vector&>(chunk);
        // Nominal FormID-over-Int32 decodes to FormID; raw Int32 decodes to Int.
        // For v1 we decode according to the nominal name — "FormID" → FormID kind.
        // (Refine via a nominal_to_kind hint in a later plan if needed.)
        if (type_->is_nominal() && type_->name() == "FormID") {
            return Value::make_formid(static_cast<uint32_t>(c.get(row_idx)));
        }
        return Value::make_int(c.get(row_idx));
    }
    if (phys == types::int64()) {
        auto const& c = static_cast<const Int64Vector&>(chunk);
        return Value::make_int(c.get(row_idx));
    }
    if (phys == types::float64()) {
        auto const& c = static_cast<const Float64Vector&>(chunk);
        return Value::make_float(c.get(row_idx));
    }
    if (phys == types::boolean()) {
        auto const& c = static_cast<const BoolVector&>(chunk);
        return Value::make_bool(c.get(row_idx));
    }
    if (phys == types::string()) {
        auto const& c = static_cast<const StringVector&>(chunk);
        return Value::make_string(c.get(row_idx));
    }
    if (phys == types::keyword()) {
        auto const& c = static_cast<const KeywordVector&>(chunk);
        return Value::make_keyword(c.get(row_idx));
    }
    if (phys == types::any()) {
        auto const& c = static_cast<const AnyVector&>(chunk);
        return c.get(row_idx);
    }
    throw std::runtime_error("Column::at: unsupported physical type");
}

} // namespace mora
```

- [ ] **Step 3: Add `src/data/column.cpp` to xmake if the file list is explicit**

Check `xmake.lua` around line 177. If `src/data/*.cpp` is listed file-by-file, add `src/data/column.cpp`. If it uses a glob, nothing to do.

- [ ] **Step 4: Build**

```
xmake build 2>&1 | tail -5
```

Expected: succeeds.

---

## Task 5: Tests

**Files:**
- Create: `tests/data/test_vector.cpp`
- Create: `tests/data/test_column.cpp`

- [ ] **Step 1: Write `tests/data/test_vector.cpp`**

```cpp
#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/data/vector.h"

#include <gtest/gtest.h>

namespace {

TEST(Vector, TypedVectorsCarryTheirType) {
    mora::Int32Vector   i32;
    mora::Int64Vector   i64;
    mora::StringVector  s;
    mora::KeywordVector kw;
    mora::AnyVector     any;

    EXPECT_EQ(i32.type(),  mora::types::int32());
    EXPECT_EQ(i64.type(),  mora::types::int64());
    EXPECT_EQ(s.type(),    mora::types::string());
    EXPECT_EQ(kw.type(),   mora::types::keyword());
    EXPECT_EQ(any.type(),  mora::types::any());
}

TEST(Vector, Int32AppendAndRead) {
    mora::Int32Vector v;
    EXPECT_EQ(v.size(), 0u);
    v.append(42);
    v.append(-7);
    v.append(2'000'000'000);
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v.get(0),  42);
    EXPECT_EQ(v.get(1),  -7);
    EXPECT_EQ(v.get(2),  2'000'000'000);
}

TEST(Vector, BoolVectorRoundTrip) {
    mora::BoolVector v;
    v.append(true);
    v.append(false);
    v.append(true);
    ASSERT_EQ(v.size(), 3u);
    EXPECT_TRUE (v.get(0));
    EXPECT_FALSE(v.get(1));
    EXPECT_TRUE (v.get(2));
}

TEST(Vector, StringAndKeywordCarryStringIds) {
    mora::StringPool pool;
    auto const s_id  = pool.intern("Skeever");
    auto const kw_id = pool.intern("Name");

    mora::StringVector  s;
    mora::KeywordVector kw;
    s.append(s_id);
    kw.append(kw_id);

    EXPECT_EQ(s.get(0).index,  s_id.index);
    EXPECT_EQ(kw.get(0).index, kw_id.index);
}

TEST(Vector, BytesVectorVariableWidth) {
    mora::BytesVector b;
    uint8_t const a[] = {1, 2, 3};
    uint8_t const c[] = {9, 8, 7, 6, 5};
    b.append(a, sizeof(a));
    b.append(c, sizeof(c));
    ASSERT_EQ(b.size(), 2u);
    size_t len = 0;
    auto const* p0 = b.data(0, &len);
    EXPECT_EQ(len, 3u);
    EXPECT_EQ(p0[0], 1); EXPECT_EQ(p0[2], 3);
    auto const* p1 = b.data(1, &len);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(p1[0], 9); EXPECT_EQ(p1[4], 5);
}

TEST(Vector, AnyVectorMixedKinds) {
    mora::StringPool pool;
    mora::AnyVector v;
    v.append(mora::Value::make_int(100));
    v.append(mora::Value::make_float(2.5));
    v.append(mora::Value::make_string(pool.intern("hello")));
    v.append(mora::Value::make_formid(0xDEADBEEF));

    ASSERT_EQ(v.size(), 4u);
    EXPECT_EQ(v.kind_at(0), mora::Value::Kind::Int);
    EXPECT_EQ(v.kind_at(1), mora::Value::Kind::Float);
    EXPECT_EQ(v.kind_at(2), mora::Value::Kind::String);
    EXPECT_EQ(v.kind_at(3), mora::Value::Kind::FormID);

    EXPECT_EQ(v.get(0).as_int(),     100);
    EXPECT_EQ(v.get(1).as_float(),   2.5);
    EXPECT_EQ(pool.get(v.get(2).as_string()), "hello");
    EXPECT_EQ(v.get(3).as_formid(),  0xDEADBEEFu);
}

} // namespace
```

- [ ] **Step 2: Write `tests/data/test_column.cpp`**

```cpp
#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/column.h"
#include "mora/data/value.h"
#include "mora/data/vector.h"

#include <gtest/gtest.h>

namespace {

TEST(Column, TypeIsCarried) {
    mora::Column c(mora::types::int32());
    EXPECT_EQ(c.type(), mora::types::int32());
    EXPECT_EQ(c.row_count(), 0u);
    EXPECT_EQ(c.chunk_count(), 0u);
}

TEST(Column, AppendsAllocateChunksLazily) {
    mora::Column c(mora::types::int32());
    c.append(mora::Value::make_int(1));
    EXPECT_EQ(c.chunk_count(), 1u);
    EXPECT_EQ(c.row_count(), 1u);
    EXPECT_EQ(c.at(0).as_int(), 1);
}

TEST(Column, RollsToNextChunkAtBoundary) {
    mora::Column c(mora::types::int32());
    for (size_t i = 0; i < mora::kChunkSize + 3; ++i) {
        c.append(mora::Value::make_int(static_cast<int64_t>(i)));
    }
    EXPECT_EQ(c.chunk_count(), 2u);
    EXPECT_EQ(c.row_count(), mora::kChunkSize + 3);
    EXPECT_EQ(c.at(0).as_int(), 0);
    EXPECT_EQ(c.at(mora::kChunkSize - 1).as_int(),
              static_cast<int64_t>(mora::kChunkSize - 1));
    EXPECT_EQ(c.at(mora::kChunkSize).as_int(),
              static_cast<int64_t>(mora::kChunkSize));
    EXPECT_EQ(c.at(mora::kChunkSize + 2).as_int(),
              static_cast<int64_t>(mora::kChunkSize + 2));
}

TEST(Column, FormIDNominalDecodesBack) {
    auto const* formid = mora::TypeRegistry::instance().register_nominal(
        "FormID", mora::types::int32());
    mora::Column c(formid);
    c.append(mora::Value::make_formid(0x000A0B0C));
    EXPECT_EQ(c.row_count(), 1u);
    auto const v = c.at(0);
    EXPECT_EQ(v.kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(v.as_formid(), 0x000A0B0Cu);
}

TEST(Column, AnyColumnAcceptsMixedKinds) {
    mora::StringPool pool;
    mora::Column c(mora::types::any());
    c.append(mora::Value::make_int(42));
    c.append(mora::Value::make_string(pool.intern("foo")));
    c.append(mora::Value::make_float(1.5));

    EXPECT_EQ(c.row_count(), 3u);
    EXPECT_EQ(c.at(0).kind(), mora::Value::Kind::Int);
    EXPECT_EQ(c.at(0).as_int(), 42);
    EXPECT_EQ(c.at(1).kind(), mora::Value::Kind::String);
    EXPECT_EQ(pool.get(c.at(1).as_string()), "foo");
    EXPECT_EQ(c.at(2).kind(), mora::Value::Kind::Float);
    EXPECT_EQ(c.at(2).as_float(), 1.5);
}

TEST(Column, KeywordColumnTypedAccess) {
    mora::StringPool pool;
    mora::Column c(mora::types::keyword());
    c.append(mora::Value::make_keyword(pool.intern("GoldValue")));
    c.append(mora::Value::make_keyword(pool.intern("Name")));

    EXPECT_EQ(c.row_count(), 2u);
    EXPECT_EQ(pool.get(c.at(0).as_keyword()), "GoldValue");
    EXPECT_EQ(pool.get(c.at(1).as_keyword()), "Name");

    // Downcast to typed chunk for direct access
    auto const& kw = static_cast<const mora::KeywordVector&>(c.chunk(0));
    EXPECT_EQ(kw.size(), 2u);
}

} // namespace
```

- [ ] **Step 3: Build and run**

```
xmake build 2>&1 | tail -5
xmake run test_vector 2>&1 | tail -15
xmake run test_column 2>&1 | tail -15
```

Expected: 6 `test_vector` cases pass, 6 `test_column` cases pass.

The files live at `tests/data/test_*.cpp` matching the `tests/**/test_*.cpp` glob at `xmake.lua:267`.

---

## Task 6: Full build + commit

- [ ] **Step 1: Run full test suite**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -10
```

Expected:
- Build: succeeds.
- Tests: **76** total binaries pass (78 before Plan 10 - 4 deleted dead-code tests + 2 new = 76).

If anything else broke, it's a missed reverse-dependency on the deleted headers — grep for `columnar_relation`, `chunk_pool`, `mora/data/chunk.h`, `eval/operators.h`, `eval/pipeline.h` and fix.

- [ ] **Step 2: Confirm the diff scope**

```
git status
git diff --stat a64811c..HEAD
```

Expected:
- Deleted: 11 files (~800 LOC: 4 header/impl pairs + 4 test files + 3 header-only deletions)
- Added: 4 new files (~500 LOC: 2 header/impl pairs + 2 tests)
- Modified: 1 file (`xmake.lua`, 2 lines removed)

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: typed Vector + Column infrastructure; retire dead pre-v3 columnar code

Introduces the typed Vector abstraction (Int32Vector, Int64Vector,
Float64Vector, BoolVector, StringVector, KeywordVector, BytesVector,
AnyVector) and the Column class (Type* + chunks of Vectors, kChunkSize
= 2048). Nothing in FactDB or the evaluator uses these yet — Plan 11
promotes them to the primary storage model.

Deletes dead pre-v3 columnar modules that were never wired into any
code path: src/data/{columnar_relation,chunk_pool}.cpp +
include/mora/data/{columnar_relation,chunk_pool,chunk}.h +
include/mora/eval/{operators,pipeline}.h, plus their 4 test files
(~553 LOC of tests covering abandoned code).

Net: ~800 LOC deleted, ~500 LOC added. 76 tests pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Expected: commit succeeds, branch is 24 commits above master.

---

## Verification

1. `grep -rn 'columnar_relation\|chunk_pool\|include/mora/data/chunk\.h\|eval/operators\.h\|eval/pipeline\.h' src include extensions tests` is empty.
2. `xmake test` passes 76/76.
3. `include/mora/data/vector.h` + `src/data/vector.cpp` + `include/mora/data/column.h` + `src/data/column.cpp` all exist.
4. `Column(types::int32())` + append + `.at(row)` round-trips for every physical type in the test suite.

## Critical files

- `/home/tbaldrid/oss/mora/include/mora/data/vector.h` — **new**, the typed-chunk abstraction
- `/home/tbaldrid/oss/mora/include/mora/data/column.h` — **new**, chunks-of-one-type
- `/home/tbaldrid/oss/mora/src/data/vector.cpp` + `column.cpp` — **new**
- `/home/tbaldrid/oss/mora/tests/data/test_vector.cpp` + `test_column.cpp` — **new**
- `/home/tbaldrid/oss/mora/xmake.lua` — 2-line change removing deleted files from `src/data` list
