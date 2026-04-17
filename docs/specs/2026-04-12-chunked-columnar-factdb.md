# Chunked Columnar FactDB

> **Status:** Historical design doc — shipped. See `src/data/chunk_pool.cpp`,
> `src/data/columnar_relation.cpp`, and `src/data/indexed_relation.cpp` for
> the current implementation.

## Problem

The evaluator spends 7.2s processing 89 rules × 31K NPCs because every tuple match allocates an `unordered_map` for bindings, copies `Value` objects with `shared_ptr` refcounts, and materializes all intermediate results as `vector<Tuple>`. For 4M output patches, this produces ~50M heap allocations.

## Design

Replace the row-oriented FactDB (vector of tuples with hash indexes) with a **chunked columnar store** inspired by DuckDB's vectorized execution model. Data is stored in fixed-size column chunks. The evaluator processes chunks through a pipeline of operators that pass selection vectors (not materialized tuples) between stages.

## Core Data Structures

### TypedColumn

A single column of values stored as a flat typed array. No `Value` wrapper — the type is known at the relation schema level.

```cpp
enum class ColumnType : uint8_t { FormID, Int, Float, StringId, List };

struct Chunk {
    static constexpr size_t CHUNK_SIZE = 2048;
    size_t count = 0;  // actual rows in this chunk (≤ CHUNK_SIZE)
};

struct FormIdChunk : Chunk {
    uint32_t data[CHUNK_SIZE];  // 8 KB — fits in L1
};

struct IntChunk : Chunk {
    int64_t data[CHUNK_SIZE];   // 16 KB
};

struct FloatChunk : Chunk {
    double data[CHUNK_SIZE];    // 16 KB
};

struct StringIdChunk : Chunk {
    uint32_t data[CHUNK_SIZE];  // 8 KB — StringPool index, not string data
};
```

Strings: already interned as `uint32_t` via `StringPool`. A "string column" is just a `uint32_t` array of StringId indices. Comparison = integer `==`. Hashing = integer hash. 4 bytes per string, better than DuckDB's 16-byte `string_t`.

### ChunkPool

Pre-allocates chunks and reuses them across pipeline stages. The pool is per-thread to avoid contention.

```cpp
class ChunkPool {
public:
    FormIdChunk* acquire_formid();
    IntChunk* acquire_int();
    void release(Chunk* chunk);  // returns to freelist by type

private:
    // Per-type freelists. Fixed-size so just a stack of pointers.
    std::vector<FormIdChunk*> free_formid_;
    std::vector<IntChunk*> free_int_;
    // ... one per type
    // Backing storage: large mmap'd region divided into CHUNK_SIZE slots
    std::vector<std::unique_ptr<uint8_t[]>> slabs_;
};
```

Steady state: a 5-operator pipeline needs ~10 live chunks. The pool allocates those once at startup and never calls malloc again during evaluation.

### ColumnarRelation

Replaces `IndexedRelation`. Stores N columns, each as a vector of typed chunks. Plus hash indexes built over chunk data.

```cpp
struct ColumnarRelation {
    size_t arity;
    std::vector<ColumnType> col_types;
    size_t total_rows = 0;

    // Column storage: vector of chunks per column
    // col_chunks[col_index] = vector of typed chunk pointers
    std::vector<std::vector<Chunk*>> col_chunks;

    // Hash indexes: col_index → hash(value) → vector<(chunk_idx, row_idx)>
    struct RowRef { uint32_t chunk; uint16_t row; };
    std::vector<std::unordered_map<uint64_t, std::vector<RowRef>>> indexes;

    void add_chunk(/* column data arrays */);
    void build_indexes(const std::vector<size_t>& indexed_columns);
};
```

### SelectionVector

A bitmask or index array identifying which rows in a chunk passed a filter. Passed between pipeline operators instead of materialized tuples.

```cpp
struct SelectionVector {
    uint16_t indices[Chunk::CHUNK_SIZE];
    size_t count = 0;

    // Start with all rows selected
    void select_all(size_t n) {
        for (size_t i = 0; i < n; i++) indices[i] = i;
        count = n;
    }
};
```

Stack-allocated. 4KB per selection vector. Zero heap allocation.

## Pipeline Evaluation

The evaluator compiles each rule into a pipeline of chunk operators. For the SPID keyword distribution rule:

```
spid_dist(RuleID, "keyword", Target)
spid_filter(RuleID, "keyword", KWList)
KW in KWList
has_keyword(NPC, KW)
npc(NPC)
=> add_keyword(NPC, Target)
```

The pipeline:

```
1. SCAN spid_dist WHERE col1 == "keyword"
   → chunks of (RuleID, Target) with selection vector
   
2. HASH JOIN spid_filter ON col0 == RuleID WHERE col1 == "keyword"
   → adds KWList column

3. UNNEST KWList → KW
   → expands list values into individual rows (new chunks)

4. HASH PROBE has_keyword ON col1 == KW
   → returns (NPC, KW) pairs from has_keyword that match

5. SEMI JOIN npc ON col0 == NPC
   → filters to valid NPCs

6. EMIT PATCHES (NPC, Target) → add_keyword
```

Each operator:
- Receives an input chunk + selection vector
- Produces an output chunk + selection vector (acquired from pool)
- Releases input chunk back to pool when done
- Zero heap allocation in the hot loop

### Operator Interface

```cpp
class PipelineOperator {
public:
    virtual ~PipelineOperator() = default;
    // Process one input chunk, produce zero or more output chunks
    // Calls sink.push() for each output chunk
    virtual void process(const DataChunk& input, PipelineSink& sink) = 0;
};

class PipelineSink {
public:
    virtual void push(DataChunk&& chunk) = 0;
};

struct DataChunk {
    Chunk* columns[8];     // up to 8 columns per chunk (enough for any relation)
    SelectionVector sel;
    size_t col_count = 0;
    ChunkPool* pool;       // for releasing columns when done
};
```

### Hash Table for Joins

DuckDB-style build/probe hash join. Build side constructs a hash table from one relation's chunks. Probe side streams chunks through the hash table.

```cpp
class ChunkHashTable {
public:
    // Build from a ColumnarRelation on the specified key column
    void build(const ColumnarRelation& rel, size_t key_col);

    // Probe: for each value in the key array, find matching rows
    // Returns matched row refs via selection vector
    void probe(const uint32_t* keys, size_t count,
               SelectionVector& matches, std::vector<RowRef>& refs);

private:
    // key_hash → vector of RowRef pointing into the build relation
    std::unordered_map<uint64_t, std::vector<RowRef>> table_;
};
```

## String Handling

**No change needed.** Our `StringPool` already interns all strings as `uint32_t` indices:
- String columns store `uint32_t[2048]` (8KB per chunk)
- String comparison = `uint32_t ==` (single instruction)
- String hashing = `std::hash<uint32_t>` (single instruction)
- Zero per-string allocation during evaluation

This is better than DuckDB's 16-byte `string_t` with inline/prefix optimization. DuckDB needs that because it handles arbitrary runtime strings. We intern everything at load time.

## List Handling

The current `Value::Kind::List` uses `shared_ptr<vector<Value>>` — expensive to copy. In the columnar model, lists are stored as:

```cpp
struct ListChunk : Chunk {
    // Offsets into a shared value array
    uint32_t offsets[CHUNK_SIZE];  // start index into values[]
    uint16_t lengths[CHUNK_SIZE];  // element count
    uint32_t* values;              // flat array of FormID/StringId values
    size_t values_size;
};
```

The UNNEST operator expands lists into flat chunks by reading offsets/lengths and emitting one row per list element.

## Migration Path

### Phase 1: ColumnarRelation + ChunkPool (foundation)
- New `include/mora/data/columnar_relation.h`
- New `src/data/columnar_relation.cpp`
- New `include/mora/data/chunk_pool.h`
- ChunkPool with per-type freelists
- ColumnarRelation stores typed column chunks
- Bulk loading from ESP reader (add_chunk instead of add_fact one by one)

### Phase 2: Pipeline operators
- New `include/mora/eval/pipeline.h`
- Scan, HashJoin, SemiJoin, Unnest, EmitPatches operators
- DataChunk + SelectionVector types
- ChunkHashTable for build/probe joins

### Phase 3: Pipeline compiler
- New `include/mora/eval/pipeline_compiler.h`
- Takes a Rule AST, produces a pipeline of operators
- Replaces the recursive `match_clauses` evaluator for the compile path
- The old evaluator stays for `mora check` (no FactDB needed)

### Phase 4: Wire into compile pipeline
- Modify `src/main.cpp` to use pipeline evaluation
- FactDB populated as ColumnarRelations during ESP loading
- INI distribution facts loaded as ColumnarRelation chunks
- Pipeline compiler builds and executes pipelines for generic rules

## Expected Performance

Current: 7.2s for 89 rules × 31K NPCs (50M+ allocations)

After:
- Chunk-based: ~244 chunks of NPCs, each processed in tight loops
- Zero per-row allocation — only chunk acquire/release from pool
- Hash joins on contiguous arrays (cache-friendly)
- Selection vectors on stack (no heap)
- **Expected: 7.2s → ~200ms** (conservative)
- With parallelism across chunks: **~50ms**

## What Stays The Same

- `StringPool` — already optimal (4-byte interned IDs)
- `Value` type — still used for .mora file evaluation via the old evaluator
- The old `FactDB` / `IndexedRelation` / `Evaluator` — kept for `mora check` path
- The patch table serializer — takes ResolvedPatchSet regardless of how it was produced
- All RT functions — runtime is data-driven now, doesn't care how patches were produced
