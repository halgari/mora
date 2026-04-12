# Chunked Columnar FactDB Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the row-oriented FactDB with a chunked columnar store and pipeline evaluator that eliminates per-tuple heap allocation, targeting 7.2s → ~200ms for rule evaluation.

**Architecture:** Data stored as typed column chunks (2048 rows each). Evaluator compiles rules into pipelines of chunk operators (Scan, HashJoin, SemiJoin, Unnest, EmitPatches). Operators pass SelectionVectors (stack arrays) between stages — zero heap allocation in the hot loop. ChunkPool manages fixed-size chunk reuse.

**Tech Stack:** C++20, existing StringPool for interned strings, no external dependencies.

---

### Task 1: Chunk types and ChunkPool

**Files:**
- Create: `include/mora/data/chunk.h`
- Create: `include/mora/data/chunk_pool.h`
- Create: `src/data/chunk_pool.cpp`
- Test: `tests/chunk_pool_test.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// tests/chunk_pool_test.cpp
#include <gtest/gtest.h>
#include "mora/data/chunk_pool.h"

TEST(ChunkPoolTest, AcquireAndRelease) {
    mora::ChunkPool pool;
    auto* c = pool.acquire<mora::U32Chunk>();
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->count, 0u);
    c->data[0] = 42;
    c->count = 1;
    pool.release(c);

    // Reacquire should return the same chunk (recycled)
    auto* c2 = pool.acquire<mora::U32Chunk>();
    EXPECT_EQ(c2, c);
    EXPECT_EQ(c2->count, 0u); // count reset on acquire
    pool.release(c2);
}

TEST(ChunkPoolTest, MultipleChunks) {
    mora::ChunkPool pool;
    auto* a = pool.acquire<mora::U32Chunk>();
    auto* b = pool.acquire<mora::U32Chunk>();
    EXPECT_NE(a, b);
    pool.release(a);
    pool.release(b);
}

TEST(ChunkPoolTest, I64Chunks) {
    mora::ChunkPool pool;
    auto* c = pool.acquire<mora::I64Chunk>();
    ASSERT_NE(c, nullptr);
    c->data[0] = -42;
    c->count = 1;
    pool.release(c);
}

TEST(ChunkPoolTest, F64Chunks) {
    mora::ChunkPool pool;
    auto* c = pool.acquire<mora::F64Chunk>();
    ASSERT_NE(c, nullptr);
    c->data[0] = 3.14;
    c->count = 1;
    pool.release(c);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build && xmake test chunk_pool_test`
Expected: FAIL — `chunk.h` / `chunk_pool.h` not found.

- [ ] **Step 3: Implement chunk types**

```cpp
// include/mora/data/chunk.h
#pragma once
#include <cstdint>
#include <cstddef>

namespace mora {

static constexpr size_t CHUNK_SIZE = 2048;

// Base chunk with count field. All typed chunks inherit from this.
struct ChunkBase {
    size_t count = 0;  // actual rows (0..CHUNK_SIZE)
};

struct U32Chunk : ChunkBase {
    uint32_t data[CHUNK_SIZE];  // 8 KB — FormIDs, StringIds
};

struct I64Chunk : ChunkBase {
    int64_t data[CHUNK_SIZE];   // 16 KB — integers
};

struct F64Chunk : ChunkBase {
    double data[CHUNK_SIZE];    // 16 KB — floats
};

// Selection vector: indices into a chunk identifying which rows passed a filter.
// Stack-allocated. Passed between pipeline operators.
struct SelectionVector {
    uint16_t indices[CHUNK_SIZE];
    size_t count = 0;

    void select_all(size_t n) {
        for (size_t i = 0; i < n; i++) indices[i] = static_cast<uint16_t>(i);
        count = n;
    }
};

} // namespace mora
```

- [ ] **Step 4: Implement ChunkPool**

```cpp
// include/mora/data/chunk_pool.h
#pragma once
#include "mora/data/chunk.h"
#include <vector>
#include <memory>

namespace mora {

class ChunkPool {
public:
    template <typename T>
    T* acquire() {
        static_assert(std::is_base_of_v<ChunkBase, T>);
        auto& fl = freelist<T>();
        if (!fl.empty()) {
            T* c = fl.back();
            fl.pop_back();
            c->count = 0;
            return c;
        }
        auto ptr = std::make_unique<T>();
        T* raw = ptr.get();
        raw->count = 0;
        storage_.push_back(std::move(ptr));
        return raw;
    }

    template <typename T>
    void release(T* chunk) {
        static_assert(std::is_base_of_v<ChunkBase, T>);
        freelist<T>().push_back(chunk);
    }

private:
    template <typename T>
    std::vector<T*>& freelist() {
        // Use a static local per type — each type gets its own freelist.
        // This is safe because ChunkPool instances are not shared across threads.
        static thread_local std::vector<T*> fl;
        return fl;
    }

    // Backing storage keeps chunks alive for the pool's lifetime
    std::vector<std::unique_ptr<ChunkBase>> storage_;
};

} // namespace mora
```

Wait — `thread_local` static in a template member is shared across all ChunkPool instances on the same thread, which is wrong. Use a simpler approach with type-erased freelists per pool:

```cpp
// include/mora/data/chunk_pool.h
#pragma once
#include "mora/data/chunk.h"
#include <vector>
#include <memory>

namespace mora {

class ChunkPool {
public:
    template <typename T>
    T* acquire() {
        static_assert(std::is_base_of_v<ChunkBase, T>);
        auto& fl = get_freelist<T>();
        if (!fl.empty()) {
            auto* c = static_cast<T*>(fl.back());
            fl.pop_back();
            c->count = 0;
            return c;
        }
        auto* raw = new T();
        raw->count = 0;
        owned_.push_back(std::unique_ptr<ChunkBase>(raw));
        return raw;
    }

    template <typename T>
    void release(T* chunk) {
        static_assert(std::is_base_of_v<ChunkBase, T>);
        get_freelist<T>().push_back(chunk);
    }

private:
    // Per-type freelists indexed by type tag
    enum TypeTag { kU32, kI64, kF64, kTypeCount };

    template <typename T> static constexpr TypeTag tag_for();

    template <typename T>
    std::vector<ChunkBase*>& get_freelist() {
        return freelists_[tag_for<T>()];
    }

    std::vector<ChunkBase*> freelists_[kTypeCount];
    std::vector<std::unique_ptr<ChunkBase>> owned_;
};

template <> constexpr ChunkPool::TypeTag ChunkPool::tag_for<U32Chunk>() { return kU32; }
template <> constexpr ChunkPool::TypeTag ChunkPool::tag_for<I64Chunk>() { return kI64; }
template <> constexpr ChunkPool::TypeTag ChunkPool::tag_for<F64Chunk>() { return kF64; }

} // namespace mora
```

Actually, even simpler — just use separate named vectors:

```cpp
// include/mora/data/chunk_pool.h
#pragma once
#include "mora/data/chunk.h"
#include <vector>
#include <memory>

namespace mora {

class ChunkPool {
public:
    U32Chunk* acquire_u32();
    I64Chunk* acquire_i64();
    F64Chunk* acquire_f64();

    void release(U32Chunk* c) { free_u32_.push_back(c); }
    void release(I64Chunk* c) { free_i64_.push_back(c); }
    void release(F64Chunk* c) { free_f64_.push_back(c); }

    // Convenience template
    template <typename T> T* acquire();
    template <typename T> void release(T* c);

private:
    std::vector<U32Chunk*> free_u32_;
    std::vector<I64Chunk*> free_i64_;
    std::vector<F64Chunk*> free_f64_;
    std::vector<std::unique_ptr<ChunkBase>> owned_;
};

// Template specializations in .cpp
template <> inline U32Chunk* ChunkPool::acquire<U32Chunk>() { return acquire_u32(); }
template <> inline I64Chunk* ChunkPool::acquire<I64Chunk>() { return acquire_i64(); }
template <> inline F64Chunk* ChunkPool::acquire<F64Chunk>() { return acquire_f64(); }

} // namespace mora
```

```cpp
// src/data/chunk_pool.cpp
#include "mora/data/chunk_pool.h"

namespace mora {

U32Chunk* ChunkPool::acquire_u32() {
    if (!free_u32_.empty()) {
        auto* c = free_u32_.back();
        free_u32_.pop_back();
        c->count = 0;
        return c;
    }
    auto* c = new U32Chunk();
    c->count = 0;
    owned_.push_back(std::unique_ptr<ChunkBase>(c));
    return c;
}

I64Chunk* ChunkPool::acquire_i64() {
    if (!free_i64_.empty()) {
        auto* c = free_i64_.back();
        free_i64_.pop_back();
        c->count = 0;
        return c;
    }
    auto* c = new I64Chunk();
    c->count = 0;
    owned_.push_back(std::unique_ptr<ChunkBase>(c));
    return c;
}

F64Chunk* ChunkPool::acquire_f64() {
    if (!free_f64_.empty()) {
        auto* c = free_f64_.back();
        free_f64_.pop_back();
        c->count = 0;
        return c;
    }
    auto* c = new F64Chunk();
    c->count = 0;
    owned_.push_back(std::unique_ptr<ChunkBase>(c));
    return c;
}

} // namespace mora
```

- [ ] **Step 5: Run tests, commit**

Run: `xmake build && xmake test chunk_pool_test`

```bash
git add include/mora/data/chunk.h include/mora/data/chunk_pool.h src/data/chunk_pool.cpp tests/chunk_pool_test.cpp
git commit -m "feat: chunk types and ChunkPool for columnar storage"
```

---

### Task 2: ColumnarRelation

**Files:**
- Create: `include/mora/data/columnar_relation.h`
- Create: `src/data/columnar_relation.cpp`
- Test: `tests/columnar_relation_test.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/columnar_relation_test.cpp
#include <gtest/gtest.h>
#include "mora/data/columnar_relation.h"
#include "mora/data/chunk_pool.h"

TEST(ColumnarRelationTest, CreateAndAddRows) {
    mora::ChunkPool pool;
    // has_keyword(FormID, FormID) — 2 columns, both U32
    mora::ColumnarRelation rel(2, {mora::ColType::U32, mora::ColType::U32}, pool);

    // Add some rows
    rel.append_row({100, 0xA01});
    rel.append_row({100, 0xA02});
    rel.append_row({200, 0xB01});

    EXPECT_EQ(rel.row_count(), 3u);
}

TEST(ColumnarRelationTest, ScanColumn) {
    mora::ChunkPool pool;
    mora::ColumnarRelation rel(2, {mora::ColType::U32, mora::ColType::U32}, pool);

    for (uint32_t i = 0; i < 5000; i++) {
        rel.append_row({i, i * 10});
    }

    EXPECT_EQ(rel.row_count(), 5000u);
    EXPECT_EQ(rel.chunk_count(), 3u); // ceil(5000/2048)

    // Read back from first chunk
    auto* chunk0 = rel.u32_chunk(/*col=*/0, /*chunk_idx=*/0);
    EXPECT_EQ(chunk0->count, 2048u);
    EXPECT_EQ(chunk0->data[0], 0u);
    EXPECT_EQ(chunk0->data[2047], 2047u);

    // Last chunk is partial
    auto* chunk2 = rel.u32_chunk(0, 2);
    EXPECT_EQ(chunk2->count, 5000u - 2 * 2048);
}

TEST(ColumnarRelationTest, BuildHashIndex) {
    mora::ChunkPool pool;
    mora::ColumnarRelation rel(2, {mora::ColType::U32, mora::ColType::U32}, pool);

    rel.append_row({100, 0xA01});
    rel.append_row({200, 0xB01});
    rel.append_row({100, 0xA02});

    rel.build_index(0); // index on column 0

    auto refs = rel.lookup(0, 100);
    EXPECT_EQ(refs.size(), 2u); // two rows with formid 100

    auto refs2 = rel.lookup(0, 200);
    EXPECT_EQ(refs2.size(), 1u);

    auto refs3 = rel.lookup(0, 999);
    EXPECT_EQ(refs3.size(), 0u);
}
```

- [ ] **Step 2: Implement ColumnarRelation**

```cpp
// include/mora/data/columnar_relation.h
#pragma once
#include "mora/data/chunk.h"
#include "mora/data/chunk_pool.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <initializer_list>

namespace mora {

enum class ColType : uint8_t { U32, I64, F64 };

struct RowRef {
    uint32_t chunk_idx;
    uint16_t row_idx;
};

class ColumnarRelation {
public:
    ColumnarRelation(size_t arity, std::initializer_list<ColType> types, ChunkPool& pool);

    // Append a single row (convenience for loading).
    // Values are passed as uint64_t and cast according to column type.
    void append_row(std::initializer_list<uint64_t> values);

    // Build a hash index on the given column
    void build_index(size_t col);

    // Lookup rows where column col == value (must have index built)
    const std::vector<RowRef>& lookup(size_t col, uint64_t value) const;

    size_t row_count() const { return total_rows_; }
    size_t chunk_count() const;
    size_t arity() const { return arity_; }

    // Direct chunk access
    U32Chunk* u32_chunk(size_t col, size_t chunk_idx);
    I64Chunk* i64_chunk(size_t col, size_t chunk_idx);
    F64Chunk* f64_chunk(size_t col, size_t chunk_idx);

    const U32Chunk* u32_chunk(size_t col, size_t chunk_idx) const;

private:
    void ensure_current_chunk();

    size_t arity_;
    std::vector<ColType> col_types_;
    ChunkPool& pool_;
    size_t total_rows_ = 0;

    // Column storage: col_chunks_[col_index] = vector of chunk pointers
    std::vector<std::vector<ChunkBase*>> col_chunks_;
    size_t current_chunk_row_ = 0; // row index within current chunk

    // Hash indexes: col_index → { value_hash → vector<RowRef> }
    std::vector<std::unordered_map<uint64_t, std::vector<RowRef>>> indexes_;
    static const std::vector<RowRef> empty_refs_;
};

} // namespace mora
```

Implementation in `src/data/columnar_relation.cpp`:
- `append_row` fills current chunks, allocates new ones from pool when full
- `build_index` iterates all chunks for a column, hashes each value, builds the RowRef map
- `lookup` returns the matching RowRef vector from the index

- [ ] **Step 3: Run tests, commit**

Run: `xmake build && xmake test columnar_relation_test`

```bash
git add include/mora/data/columnar_relation.h src/data/columnar_relation.cpp tests/columnar_relation_test.cpp
git commit -m "feat: ColumnarRelation with chunked column storage and hash indexes"
```

---

### Task 3: DataChunk and pipeline operator interface

**Files:**
- Create: `include/mora/eval/pipeline.h`
- Test: `tests/pipeline_test.cpp`

- [ ] **Step 1: Define DataChunk and operator interfaces**

```cpp
// include/mora/eval/pipeline.h
#pragma once
#include "mora/data/chunk.h"
#include "mora/data/chunk_pool.h"
#include "mora/data/columnar_relation.h"
#include <functional>

namespace mora {

// A DataChunk is a set of column chunks representing up to CHUNK_SIZE rows.
// Passed between pipeline operators.
struct DataChunk {
    static constexpr size_t MAX_COLS = 8;
    ChunkBase* columns[MAX_COLS] = {};
    size_t col_count = 0;
    size_t row_count = 0;
    SelectionVector sel; // which rows are active (after filtering)

    U32Chunk* u32(size_t col) { return static_cast<U32Chunk*>(columns[col]); }
    I64Chunk* i64(size_t col) { return static_cast<I64Chunk*>(columns[col]); }
    F64Chunk* f64(size_t col) { return static_cast<F64Chunk*>(columns[col]); }
};

// Callback for pipeline output
using PatchEmitter = std::function<void(uint32_t formid, uint8_t field_id,
                                         uint8_t op, uint64_t value)>;

// Pipeline operators — each processes input chunks and produces output chunks.
// Implemented as concrete classes, not virtual dispatch (for inlining).

// ScanOp: iterate all chunks in a ColumnarRelation, optionally filter on a column.
struct ScanOp {
    const ColumnarRelation& rel;
    size_t filter_col = SIZE_MAX;  // SIZE_MAX = no filter
    uint64_t filter_val = 0;
};

// HashProbeOp: for each value in a key column of the input chunk,
// probe a pre-built hash index and emit matching rows.
struct HashProbeOp {
    const ColumnarRelation& build_rel;
    size_t build_key_col;
    // The hash index is the ColumnarRelation's built index
};

// SemiJoinOp: filter input rows to those present in a hash index.
// Like HashProbeOp but only keeps/discards rows — doesn't add columns.
struct SemiJoinOp {
    const ColumnarRelation& rel;
    size_t key_col;
};

} // namespace mora
```

- [ ] **Step 2: Write test for scan + filter**

```cpp
// tests/pipeline_test.cpp
#include <gtest/gtest.h>
#include "mora/eval/pipeline.h"

TEST(PipelineTest, ScanAllRows) {
    mora::ChunkPool pool;
    mora::ColumnarRelation rel(2, {mora::ColType::U32, mora::ColType::U32}, pool);

    for (uint32_t i = 0; i < 5000; i++) {
        rel.append_row({i, i * 10});
    }

    // Scan all rows
    size_t total = 0;
    mora::scan(rel, [&](const mora::DataChunk& chunk) {
        total += chunk.sel.count;
    });
    EXPECT_EQ(total, 5000u);
}

TEST(PipelineTest, ScanWithFilter) {
    mora::ChunkPool pool;
    mora::ColumnarRelation rel(2, {mora::ColType::U32, mora::ColType::U32}, pool);

    // 3 rows with col0=100, 2 rows with col0=200
    rel.append_row({100, 1});
    rel.append_row({100, 2});
    rel.append_row({200, 3});
    rel.append_row({100, 4});
    rel.append_row({200, 5});

    size_t matched = 0;
    mora::scan_filtered(rel, 0, 100, [&](const mora::DataChunk& chunk) {
        matched += chunk.sel.count;
    });
    EXPECT_EQ(matched, 3u);
}

TEST(PipelineTest, HashProbe) {
    mora::ChunkPool pool;

    // Build side: has_keyword(FormID, KeywordFormID)
    mora::ColumnarRelation has_kw(2, {mora::ColType::U32, mora::ColType::U32}, pool);
    has_kw.append_row({100, 0xA01});
    has_kw.append_row({200, 0xA01});
    has_kw.append_row({300, 0xA02});
    has_kw.build_index(1); // index on keyword column

    // Probe with a single keyword
    auto refs = has_kw.lookup(1, 0xA01);
    EXPECT_EQ(refs.size(), 2u); // NPCs 100 and 200 have keyword 0xA01
}
```

- [ ] **Step 3: Implement scan functions**

Free functions in `pipeline.h` (header-only for inlining):

```cpp
// Scan all chunks, call callback per chunk
template <typename F>
void scan(const ColumnarRelation& rel, F&& callback) {
    for (size_t ci = 0; ci < rel.chunk_count(); ci++) {
        DataChunk dc;
        dc.col_count = rel.arity();
        auto* first_col = rel.u32_chunk(0, ci);
        dc.row_count = first_col->count;
        for (size_t c = 0; c < rel.arity(); c++) {
            dc.columns[c] = /* get chunk for col c, chunk ci */;
        }
        dc.sel.select_all(dc.row_count);
        callback(dc);
    }
}

// Scan with equality filter on a U32 column
template <typename F>
void scan_filtered(const ColumnarRelation& rel, size_t col, uint64_t val, F&& callback) {
    for (size_t ci = 0; ci < rel.chunk_count(); ci++) {
        DataChunk dc;
        // ... set up columns
        auto* filter_chunk = rel.u32_chunk(col, ci);
        dc.sel.count = 0;
        for (size_t i = 0; i < filter_chunk->count; i++) {
            if (filter_chunk->data[i] == val) {
                dc.sel.indices[dc.sel.count++] = i;
            }
        }
        if (dc.sel.count > 0) callback(dc);
    }
}
```

- [ ] **Step 4: Run tests, commit**

Run: `xmake build && xmake test pipeline_test`

```bash
git add include/mora/eval/pipeline.h tests/pipeline_test.cpp
git commit -m "feat: DataChunk, SelectionVector, scan operators for pipeline evaluation"
```

---

### Task 4: Pipeline evaluator for SPID distribution rules

**Files:**
- Create: `include/mora/eval/pipeline_evaluator.h`
- Create: `src/eval/pipeline_evaluator.cpp`
- Test: `tests/pipeline_evaluator_test.cpp`

This is the big integration task: build a specialized evaluator for the SPID/KID generic distribution rules that uses the columnar pipeline instead of the recursive Datalog evaluator.

- [ ] **Step 1: Define interface**

```cpp
// include/mora/eval/pipeline_evaluator.h
#pragma once
#include "mora/data/columnar_relation.h"
#include "mora/data/chunk_pool.h"
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"

namespace mora {

// Columnar fact store: relations stored as ColumnarRelations.
// Populated during ESP and INI loading, queried by the pipeline evaluator.
class ColumnarFactStore {
public:
    explicit ColumnarFactStore(ChunkPool& pool);

    // Add or get a relation by name
    ColumnarRelation& get_or_create(StringId name, size_t arity,
                                     std::initializer_list<ColType> types);
    ColumnarRelation* get(StringId name);
    const ColumnarRelation* get(StringId name) const;

    // Build all indexes after bulk loading
    void build_all_indexes();

private:
    ChunkPool& pool_;
    std::unordered_map<uint32_t, ColumnarRelation> relations_;
};

// Evaluate SPID/KID distribution rules using columnar pipeline.
// Produces patches directly into a PatchSet — no intermediate Bindings.
void evaluate_distributions_columnar(
    const ColumnarFactStore& store,
    StringPool& pool,
    PatchSet& patches);

} // namespace mora
```

- [ ] **Step 2: Write integration test**

The test sets up a small ColumnarFactStore with NPC/keyword/distribution facts and verifies the pipeline produces correct patches.

```cpp
// tests/pipeline_evaluator_test.cpp
#include <gtest/gtest.h>
#include "mora/eval/pipeline_evaluator.h"
#include "mora/core/string_pool.h"

TEST(PipelineEvaluatorTest, SpidKeywordDistribution) {
    mora::ChunkPool pool;
    mora::StringPool str_pool;
    mora::ColumnarFactStore store(pool);

    auto npc_id = str_pool.intern("npc");
    auto has_kw_id = str_pool.intern("has_keyword");
    auto spid_dist_id = str_pool.intern("spid_dist");
    auto spid_filter_id = str_pool.intern("spid_filter");

    // NPCs
    auto& npcs = store.get_or_create(npc_id, 1, {mora::ColType::U32});
    npcs.append_row({0x100});
    npcs.append_row({0x200});
    npcs.append_row({0x300});

    // has_keyword: NPC 0x100 has KW 0xA01, NPC 0x200 has KW 0xA01
    auto& has_kw = store.get_or_create(has_kw_id, 2,
        {mora::ColType::U32, mora::ColType::U32});
    has_kw.append_row({0x100, 0xA01});
    has_kw.append_row({0x200, 0xA01});

    // SPID: distribute keyword 0xBEEF to NPCs with keyword 0xA01
    auto& dists = store.get_or_create(spid_dist_id, 3,
        {mora::ColType::U32, mora::ColType::U32, mora::ColType::U32});
    dists.append_row({1, str_pool.intern("keyword").index, 0xBEEF});

    auto& filters = store.get_or_create(spid_filter_id, 3,
        {mora::ColType::U32, mora::ColType::U32, mora::ColType::U32});
    // Filter list stored as... this needs thought for the list column type

    store.build_all_indexes();

    mora::PatchSet patches;
    mora::evaluate_distributions_columnar(store, str_pool, patches);

    auto resolved = patches.resolve();
    EXPECT_EQ(resolved.patch_count(), 2u); // NPCs 0x100 and 0x200 get 0xBEEF
}
```

Note: The list column type for spid_filter needs design. In the columnar model, lists can be stored as a separate "list chunk" with offsets/lengths into a flat value array. For the first iteration, we can store the list data as a side-table and probe it during the pipeline. This is an implementation detail to work out during the task.

- [ ] **Step 3: Implement the pipeline evaluator**

The `evaluate_distributions_columnar` function hardcodes the SPID/KID pipeline structure (since we have exactly 37 generic rules with known structure). For each distribution type × filter kind:

1. Scan `spid_dist` filtered on dist_type
2. For each dist rule, look up its filter in `spid_filter`
3. Expand the filter list
4. Hash-probe `has_keyword` on the expanded keywords
5. Semi-join with `npc` to validate
6. Emit patches

For no-filter rules: directly cross-product with all NPCs (tight loop, no joins).

- [ ] **Step 4: Run tests, commit**

Run: `xmake build && xmake test pipeline_evaluator_test`

```bash
git add include/mora/eval/pipeline_evaluator.h src/eval/pipeline_evaluator.cpp tests/pipeline_evaluator_test.cpp
git commit -m "feat: pipeline evaluator for columnar SPID/KID distribution"
```

---

### Task 5: Wire into compile pipeline

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/import/ini_facts.cpp` (add columnar loading path)

- [ ] **Step 1: Add ColumnarFactStore population**

After ESP loading populates the old FactDB, also populate a ColumnarFactStore from the same data. (In a future refactor, ESP loading writes directly to columnar — for now, dual-write.)

- [ ] **Step 2: Replace evaluator call**

In `cmd_compile`, after INI facts are loaded:
```cpp
// OLD: evaluator.evaluate_static(mod, eval_progress)
// NEW: evaluate_distributions_columnar(col_store, pool, patches)
```

Keep the old evaluator for .mora file rules. Use the columnar pipeline only for INI distribution rules.

- [ ] **Step 3: Benchmark**

```bash
time ./build/linux/x86_64/release/mora compile ~/joj_sync --data-dir ~/joj_sync --no-color
```

Expected: evaluation phase drops from 7.2s to ~200ms.

- [ ] **Step 4: Run full test suite, commit**

```bash
xmake build && xmake test
git commit -m "feat: wire columnar pipeline evaluator into compile pipeline"
git push
```
