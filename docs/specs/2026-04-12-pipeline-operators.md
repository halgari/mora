# Pipeline Operator Framework

## Problem

The current columnar evaluator is a monolithic function with hardcoded nested loops. It works but has three issues:

1. **Not composable** — adding new rule patterns means copy-pasting 50-line functions
2. **Row-at-a-time output** — calls `PatchSet::add_patch` 2.86M times (2.9s just for resolution/dedup)
3. **Dual-write** — ESPs load into old FactDB first, then get copied to ColumnarFactStore
4. **No chunk-level pipelining** — operators iterate row-by-row within chunks instead of passing selection vectors between stages

## Design

### Operator Model

Each pipeline operator takes an input DataChunk and produces zero or more output DataChunks. Operators are concrete types (not virtual — enables inlining) connected via a `std::function` sink.

```cpp
// An operator processes one input chunk, calls sink with each output chunk.
// Operators are chained: op1's sink feeds op2, which feeds op3, etc.
using ChunkSink = std::function<void(DataChunk&)>;
```

### Core Operators

**1. TableScan** — reads all chunks from a ColumnarRelation
```
TableScan(relation) → chunks
```
Produces one DataChunk per chunk in the relation, selection = all rows.

**2. Filter** — applies an equality predicate on a U32 column
```
Filter(col, value) : chunk → chunk (with narrowed selection)
```
Rewrites the selection vector in-place. Zero allocation.

**3. HashProbe** — probes a hash index, appends matched columns
```
HashProbe(build_relation, probe_col, build_key_col) : chunk → chunks
```
For each selected row in the input, probes the build relation's index on `build_key_col` using the input's `probe_col` value. Produces output chunks with the input columns PLUS the build relation's columns appended. Uses ChunkPool for output chunks.

**4. SemiJoin** — filters rows to those present in an index
```
SemiJoin(relation, key_col, input_col) : chunk → chunk
```
Narrows selection to rows where `input[input_col]` exists in `relation`'s index on `key_col`. Zero allocation — just rewrites selection.

**5. Project** — selects/reorders columns
```
Project(col_mapping) : chunk → chunk
```
Rearranges column pointers. Zero allocation.

**6. EmitPatches** — terminal operator that writes to a flat patch output buffer
```
EmitPatches(formid_col, field_id, op, value_col) : chunk → patches
```
Reads FormID and value from specified columns, writes directly to a pre-allocated patch output buffer. NO `PatchSet::add_patch` calls.

### Patch Output Buffer (replaces PatchSet for distributions)

The current PatchSet uses a `vector<RawPatch>` with per-patch hash map insertion for conflict resolution. For 2.86M patches this takes 2.9s.

Replace with a flat array of `PatchEntry` (same format as the serialized patch table):
```cpp
struct PatchBuffer {
    std::vector<PatchEntry> entries;  // pre-reserved
    void emit(uint32_t formid, uint8_t field_id, uint8_t op, uint32_t value_formid);
};
```

After all operators finish, sort by formid and deduplicate. Sort is O(N log N) on a contiguous array — much faster than N hash map insertions.

### Pipeline Construction

A pipeline is a chain of operators connected by sinks:

```cpp
// Example: SPID keyword distribution via keyword filter
auto pipeline = 
    table_scan(spid_kw_filter)              // scan all (RuleID, KeywordFormID) pairs
    | hash_probe(has_keyword, /*probe*/1, /*build_key*/1)  // join on keyword → adds NPC col
    | semi_join(npc, /*key*/0, /*input_col*/2)             // verify NPC exists
    | hash_lookup(rule_to_target, /*key*/0)                // resolve RuleID → Target
    | emit_patches(/*formid*/2, Keywords, Add, /*value*/3); // emit
```

For V1, we don't need the pipe syntax — just nested lambdas:

```cpp
table_scan(spid_kw_filter, [&](DataChunk& chunk) {
    hash_probe(has_keyword, 1, 1, chunk, pool, [&](DataChunk& joined) {
        semi_join(npc, 0, 2, joined, [&](DataChunk& filtered) {
            // emit patches from filtered chunk
        });
    });
});
```

### ESP Direct-to-Columnar Loading

Eliminate the dual-write by having the ESP reader write directly to ColumnarRelation. The EspReader currently calls `db.add_fact(relation, tuple)` — change to `col_rel.append_row(values)`.

This requires the ESP reader to know about ColumnarFactStore, or to produce a stream of (relation, values) that the caller routes to either FactDB or ColumnarFactStore.

### Dedup Strategy

Instead of `PatchSet::resolve()` (which builds a `map<formid, vector<FieldPatch>>`), sort the flat `PatchBuffer` by (formid, field_id) and deduplicate in a linear pass. For 2.86M entries at 16 bytes each = 45.7MB, `std::sort` takes ~300ms. Dedup is a single linear scan.

## Files

### New/Modified
- `include/mora/eval/operators.h` — operator functions (header-only for inlining)
- `include/mora/eval/patch_buffer.h` — flat patch output buffer
- `src/eval/patch_buffer.cpp` — sort + dedup
- Modify `src/eval/pipeline_evaluator.cpp` — rewrite using operators
- Modify `src/main.cpp` — use PatchBuffer, eliminate dual-write
- Modify `src/esp/esp_reader.cpp` — direct-to-columnar loading option

### Kept
- Old FactDB/Evaluator — for `mora check` and .mora file evaluation
- PatchSet — still used for .mora file patches (small volume)

## Expected Performance

| Phase | Current | After |
|-------|---------|-------|
| ESP loading | 2.2s (FactDB + copy to columnar) | 1.6s (direct to columnar) |
| Columnar eval | 1.5s | ~300ms (chunk-level pipelining) |
| Patch resolve | 2.9s (PatchSet hash map) | ~400ms (sort + dedup) |
| Serialize + link | ~430ms | ~430ms (unchanged) |
| **Total** | **6.3s** | **~2.7s** |
