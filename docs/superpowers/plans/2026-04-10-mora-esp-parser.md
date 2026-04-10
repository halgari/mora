# Mora ESP Parser & Indexed FactDB Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a high-performance ESP parser that reads Bethesda plugin files via mmap and populates an indexed FactDB through a generic schema registry. The FactDB becomes the core data engine for Datalog evaluation — all game data is represented as indexed fact tables with configurable column indexes for fast joins.

**Architecture:** The ESP parser uses memory-mapped zero-copy reading (adapted from dovaquack patterns). A SchemaRegistry maps relation names to extraction descriptors — each descriptor knows how to pull data from ESP subrecords. The FactDB is upgraded from linear-scan vectors to hash-indexed tables. The same SchemaRegistry will later drive SKSE runtime extraction (Plan 5) using a different extractor backend.

**Tech Stack:** C++20, xmake, Google Test, zlib (for compressed records).

---

## Design: Generic Schema Registry

The core idea: facts are just indexed tuples. The schema registry is data, not code. Each relation has:

```cpp
struct RelationSchema {
    StringId name;                    // e.g., "has_keyword"
    std::vector<TypeKind> column_types; // e.g., {FormID, KeywordID}
    std::vector<size_t> indexes;      // which columns to build hash indexes on

    // ESP extraction config (Plan 3)
    struct EspSource {
        RecordType record_type;       // e.g., "NPC_", "WEAP"
        std::string subrecord_tag;    // e.g., "KWDA", "DATA"
        enum class Kind {
            Existence,     // record exists → single-column fact (FormID)
            Subrecord,     // read a subrecord → multi-column fact
            PackedField,   // read a field within a packed struct
            ArrayField,    // subrecord contains array of values
            ListField,     // repeating subrecords
        } kind;
        size_t offset = 0;            // byte offset within subrecord (for PackedField)
        size_t element_size = 0;      // for ArrayField
    };
    std::vector<EspSource> esp_sources; // can come from multiple record types

    // SKSE extraction config (Plan 5, placeholder for now)
    // struct SkseSouce { ... };
};
```

This means:
- `npc(FormID)` → Existence relation, extracted from any record with type "NPC_"
- `has_keyword(FormID, KeywordID)` → ArrayField relation, from "KWDA" subrecord across NPC_, WEAP, ARMO, etc.
- `damage(FormID, Int)` → PackedField relation, from "DATA" subrecord at offset 8 in WEAP records
- Users can add custom relations by registering new schemas

## Design: Indexed FactDB

The current FactDB uses `vector<Tuple>` with linear scan. For 50K+ records, this is too slow for Datalog joins. The new design:

```cpp
class IndexedRelation {
    std::vector<Tuple> tuples_;
    // Hash indexes: column_index → { value_hash → [tuple_indices] }
    std::vector<std::unordered_map<uint64_t, std::vector<uint32_t>>> indexes_;
};
```

When you query `has_keyword(0x100, ?)`, it hashes `0x100` and looks up the first-column index to get all tuples where column 0 = 0x100. O(1) instead of O(n).

---

## File Structure

```
include/mora/
├── data/
│   ├── indexed_relation.h    # hash-indexed tuple storage
│   ├── fact_db.h             # upgraded FactDB using IndexedRelation
│   ├── schema_registry.h     # relation schemas + extraction descriptors
│   └── value.h               # Value type (extracted from fact_db.h)
├── esp/
│   ├── mmap_file.h           # memory-mapped file access
│   ├── record_types.h        # RecordType, record/subrecord headers
│   ├── plugin_index.h        # index record locations in a plugin file
│   ├── subrecord_reader.h    # iterate subrecords within a record
│   ├── esp_reader.h          # high-level: read plugin → populate FactDB
│   └── load_order.h          # read plugins.txt, resolve load order
src/
├── data/
│   ├── indexed_relation.cpp
│   ├── fact_db.cpp           # replaces eval/fact_db.cpp
│   ├── schema_registry.cpp
│   └── value.cpp
├── esp/
│   ├── mmap_file.cpp
│   ├── record_types.cpp
│   ├── plugin_index.cpp
│   ├── subrecord_reader.cpp
│   ├── esp_reader.cpp
│   └── load_order.cpp
tests/
├── indexed_relation_test.cpp
├── schema_registry_test.cpp
├── mmap_file_test.cpp
├── record_types_test.cpp
├── plugin_index_test.cpp
├── esp_reader_test.cpp
```

---

### Task 1: Extract Value Type

**Files:**
- Create: `include/mora/data/value.h`
- Create: `src/data/value.cpp`
- Modify: `include/mora/eval/fact_db.h` — remove Value class, include data/value.h instead
- Modify: all files that use `mora::Value` — update includes

The Value type currently lives in `eval/fact_db.h`. It needs to move to `data/value.h` since it's now used by both the FactDB and the ESP parser. This is a pure refactor — no behavior change.

- [ ] **Step 1: Create data/value.h with the Value class moved from eval/fact_db.h**

Read the current `include/mora/eval/fact_db.h` to get the exact Value class definition. Move it to `include/mora/data/value.h`. Add a `uint64_t hash() const` method for index building. Keep the original `eval/fact_db.h` but have it `#include "mora/data/value.h"` and remove the Value definition.

- [ ] **Step 2: Create src/data/value.cpp with hash() implementation**

```cpp
uint64_t Value::hash() const {
    switch (kind_) {
        case Kind::FormID: return std::hash<uint32_t>{}(formid_);
        case Kind::Int:    return std::hash<int64_t>{}(int_);
        case Kind::Float:  return std::hash<double>{}(float_);
        case Kind::String: return std::hash<uint32_t>{}(string_.index);
        case Kind::Bool:   return std::hash<bool>{}(bool_);
        default:           return 0;
    }
}
```

- [ ] **Step 3: Update all includes and verify build**

Run: `xmake build && xmake test`
All existing tests must still pass.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "refactor: extract Value type to data/value.h for shared use"
```

---

### Task 2: IndexedRelation — Hash-Indexed Tuple Storage

**Files:**
- Create: `include/mora/data/indexed_relation.h`
- Create: `src/data/indexed_relation.cpp`
- Create: `tests/indexed_relation_test.cpp`

This is the core data structure for fast Datalog joins. Each relation is a set of tuples with configurable hash indexes on specified columns.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/indexed_relation_test.cpp
#include <gtest/gtest.h>
#include "mora/data/indexed_relation.h"

TEST(IndexedRelationTest, AddAndScan) {
    // A relation with 2 columns, index on column 0
    mora::IndexedRelation rel(2, {0});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x300)});
    rel.add({mora::Value::make_formid(0x999), mora::Value::make_formid(0x200)});

    EXPECT_EQ(rel.size(), 3u);
}

TEST(IndexedRelationTest, IndexedLookup) {
    mora::IndexedRelation rel(2, {0});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x300)});
    rel.add({mora::Value::make_formid(0x999), mora::Value::make_formid(0x200)});

    // Lookup by column 0 = 0x100
    auto results = rel.lookup(0, mora::Value::make_formid(0x100));
    ASSERT_EQ(results.size(), 2u);
}

TEST(IndexedRelationTest, IndexedLookupMiss) {
    mora::IndexedRelation rel(2, {0});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});

    auto results = rel.lookup(0, mora::Value::make_formid(0x999));
    EXPECT_TRUE(results.empty());
}

TEST(IndexedRelationTest, MultiColumnIndex) {
    // Index on both columns
    mora::IndexedRelation rel(2, {0, 1});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x300)});

    // Lookup by column 1 = 0x200
    auto results = rel.lookup(1, mora::Value::make_formid(0x200));
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0][0].as_formid(), 0x100u);
}

TEST(IndexedRelationTest, PatternQuery) {
    mora::IndexedRelation rel(2, {0});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x300)});
    rel.add({mora::Value::make_formid(0x999), mora::Value::make_formid(0x200)});

    // Query with pattern: (0x100, ?) — uses index on col 0
    mora::Tuple pattern = {mora::Value::make_formid(0x100), mora::Value::make_var()};
    auto results = rel.query(pattern);
    ASSERT_EQ(results.size(), 2u);
}

TEST(IndexedRelationTest, FullScanWhenNoIndex) {
    // No index on any column
    mora::IndexedRelation rel(2, {});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    rel.add({mora::Value::make_formid(0x999), mora::Value::make_formid(0x300)});

    // Still works, just linear scan
    mora::Tuple pattern = {mora::Value::make_formid(0x100), mora::Value::make_var()};
    auto results = rel.query(pattern);
    ASSERT_EQ(results.size(), 1u);
}

TEST(IndexedRelationTest, ExistenceCheck) {
    mora::IndexedRelation rel(2, {0, 1});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});

    EXPECT_TRUE(rel.contains({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)}));
    EXPECT_FALSE(rel.contains({mora::Value::make_formid(0x100), mora::Value::make_formid(0x999)}));
}

TEST(IndexedRelationTest, SingleColumnRelation) {
    mora::IndexedRelation rel(1, {0});
    rel.add({mora::Value::make_formid(0x100)});
    rel.add({mora::Value::make_formid(0x200)});

    EXPECT_EQ(rel.size(), 2u);
    auto results = rel.query({mora::Value::make_var()});
    EXPECT_EQ(results.size(), 2u);
}

TEST(IndexedRelationTest, IntValues) {
    mora::IndexedRelation rel(2, {0});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_int(25)});
    rel.add({mora::Value::make_formid(0x200), mora::Value::make_int(10)});

    auto results = rel.lookup(0, mora::Value::make_formid(0x100));
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0][1].as_int(), 25);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build indexed_relation_test && xmake run indexed_relation_test`
Expected: FAIL

- [ ] **Step 3: Implement IndexedRelation**

```cpp
// include/mora/data/indexed_relation.h
#pragma once

#include "mora/data/value.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mora {

class IndexedRelation {
public:
    // arity = number of columns, indexed_columns = which to build hash indexes on
    IndexedRelation(size_t arity, std::vector<size_t> indexed_columns);

    void add(Tuple tuple);

    // Lookup by a specific indexed column value. Returns spans into tuples_.
    std::vector<const Tuple*> lookup(size_t column, const Value& key) const;

    // Pattern query: concrete values must match, Var matches anything.
    // Uses the best available index, falls back to scan.
    std::vector<const Tuple*> query(const Tuple& pattern) const;

    // Check if an exact tuple exists
    bool contains(const Tuple& values) const;

    size_t size() const { return tuples_.size(); }
    size_t arity() const { return arity_; }

    // Direct access for iteration
    const std::vector<Tuple>& all() const { return tuples_; }

private:
    size_t arity_;
    std::vector<size_t> indexed_columns_;
    std::vector<Tuple> tuples_;

    // indexes_[i] maps value_hash → list of tuple indices, for indexed_columns_[i]
    std::vector<std::unordered_map<uint64_t, std::vector<uint32_t>>> indexes_;

    // Find which index slot (if any) covers this column
    int find_index(size_t column) const;
};

} // namespace mora
```

Implementation:
- `add()`: Append tuple to `tuples_`, then for each indexed column, hash the value at that column and add the tuple index to the index map.
- `lookup()`: Find the index for the column, hash the key, return pointers to matching tuples.
- `query()`: Find the first concrete (non-Var) value that has an index, use `lookup()` to get candidates, then filter by remaining pattern values. If no indexed column matches, fall back to linear scan.
- `contains()`: Use `query()` with all-concrete pattern, check if result is non-empty.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build indexed_relation_test && xmake run indexed_relation_test`
Expected: All 9 tests PASS

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: hash-indexed relation storage for fast Datalog joins"
```

---

### Task 3: Upgrade FactDB to Use IndexedRelation

**Files:**
- Rewrite: `include/mora/eval/fact_db.h` — use IndexedRelation internally
- Rewrite: `src/eval/fact_db.cpp`

The public API stays the same (`add_fact`, `query`, `has_fact`, `fact_count`), but the implementation switches from `vector<Tuple>` to `IndexedRelation`. The relation's indexed columns are determined by a default heuristic: index column 0 (the "subject" in most facts).

- [ ] **Step 1: Rewrite FactDB to use IndexedRelation**

Update the internal storage from `unordered_map<uint32_t, vector<Tuple>>` to `unordered_map<uint32_t, IndexedRelation>`. When a relation is first accessed, create an IndexedRelation with the right arity and default indexes.

Add a new method: `void configure_relation(StringId name, size_t arity, std::vector<size_t> indexes)` — allows pre-configuring index strategy before facts are added.

- [ ] **Step 2: Run all existing tests**

Run: `xmake build && xmake test`
Expected: All 16 existing tests still pass (the FactDB API hasn't changed).

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "feat: upgrade FactDB to use hash-indexed relations"
```

---

### Task 4: Schema Registry

**Files:**
- Create: `include/mora/data/schema_registry.h`
- Create: `src/data/schema_registry.cpp`
- Create: `tests/schema_registry_test.cpp`

The SchemaRegistry defines what fact relations exist and how to populate them. For Plan 3, it includes ESP extraction descriptors. The SKSE descriptors will be added in Plan 5.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/schema_registry_test.cpp
#include <gtest/gtest.h>
#include "mora/data/schema_registry.h"
#include "mora/core/string_pool.h"

class SchemaRegistryTest : public ::testing::Test {
protected:
    mora::StringPool pool;
};

TEST_F(SchemaRegistryTest, RegisterAndLookup) {
    mora::SchemaRegistry reg(pool);
    reg.register_defaults();

    auto* schema = reg.lookup(pool.intern("npc"));
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->column_types.size(), 1u);
    EXPECT_EQ(schema->column_types[0].kind, mora::TypeKind::NpcID);
}

TEST_F(SchemaRegistryTest, HasKeywordSchema) {
    mora::SchemaRegistry reg(pool);
    reg.register_defaults();

    auto* schema = reg.lookup(pool.intern("has_keyword"));
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->column_types.size(), 2u);
    EXPECT_EQ(schema->column_types[0].kind, mora::TypeKind::FormID);
    EXPECT_EQ(schema->column_types[1].kind, mora::TypeKind::KeywordID);
    EXPECT_FALSE(schema->esp_sources.empty());
}

TEST_F(SchemaRegistryTest, WeaponDamageSchema) {
    mora::SchemaRegistry reg(pool);
    reg.register_defaults();

    auto* schema = reg.lookup(pool.intern("damage"));
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->column_types.size(), 2u);
    // Should have ESP source info for WEAP DATA subrecord
    ASSERT_FALSE(schema->esp_sources.empty());
    EXPECT_EQ(schema->esp_sources[0].record_type, "WEAP");
    EXPECT_EQ(schema->esp_sources[0].subrecord_tag, "DATA");
}

TEST_F(SchemaRegistryTest, CustomRelation) {
    mora::SchemaRegistry reg(pool);

    mora::RelationSchema custom;
    custom.name = pool.intern("my_custom_fact");
    custom.column_types = {mora::MoraType::make(mora::TypeKind::FormID),
                           mora::MoraType::make(mora::TypeKind::String)};
    custom.indexed_columns = {0};
    reg.register_schema(std::move(custom));

    auto* schema = reg.lookup(pool.intern("my_custom_fact"));
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->column_types.size(), 2u);
}

TEST_F(SchemaRegistryTest, AllRelations) {
    mora::SchemaRegistry reg(pool);
    reg.register_defaults();

    auto all = reg.all_schemas();
    // Should have at least the core Skyrim relations
    EXPECT_GE(all.size(), 20u);
}

TEST_F(SchemaRegistryTest, ConfigureFactDB) {
    mora::SchemaRegistry reg(pool);
    reg.register_defaults();

    mora::FactDB db(pool);
    reg.configure_fact_db(db);

    // FactDB should now have pre-configured relations with proper indexes
    // Adding a fact should work without error
    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x100)});
    EXPECT_EQ(db.fact_count(pool.intern("npc")), 1u);
}
```

- [ ] **Step 2: Implement SchemaRegistry**

```cpp
// include/mora/data/schema_registry.h
#pragma once

#include "mora/data/value.h"
#include "mora/ast/types.h"
#include "mora/core/string_pool.h"
#include "mora/eval/fact_db.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace mora {

struct EspSource {
    std::string record_type;      // e.g., "NPC_", "WEAP"
    std::string subrecord_tag;    // e.g., "KWDA", "DATA", "" for existence
    enum class Kind {
        Existence,     // record exists → single FormID fact
        Subrecord,     // single value from a subrecord
        PackedField,   // field at offset within a packed subrecord
        ArrayField,    // subrecord is an array of fixed-size elements
        ListField,     // repeating subrecords (each occurrence = one fact)
    } kind = Kind::Existence;
    size_t offset = 0;            // byte offset within subrecord
    size_t element_size = 0;      // element size for ArrayField
    // What type to read at the offset
    enum class ReadType { FormID, Int16, Int32, UInt16, UInt32, Float32, ZString, LString } read_type = ReadType::FormID;
};

struct RelationSchema {
    StringId name;
    std::vector<MoraType> column_types;
    std::vector<size_t> indexed_columns;   // which columns get hash indexes
    std::vector<EspSource> esp_sources;    // how to populate from ESP files
};

class SchemaRegistry {
public:
    explicit SchemaRegistry(StringPool& pool);

    void register_defaults();  // register all built-in Skyrim relations
    void register_schema(RelationSchema schema);

    const RelationSchema* lookup(StringId name) const;
    std::vector<const RelationSchema*> all_schemas() const;

    // Pre-configure a FactDB with proper relation arities and indexes
    void configure_fact_db(FactDB& db) const;

    // Get all schemas that extract from a given record type
    std::vector<const RelationSchema*> schemas_for_record(const std::string& record_type) const;

private:
    StringPool& pool_;
    std::unordered_map<uint32_t, RelationSchema> schemas_; // key: StringId.index
};

} // namespace mora
```

`register_defaults()` registers all the built-in Skyrim relations with ESP extraction info:

**Existence relations:**
- `npc` → record_type "NPC_", Existence
- `weapon` → record_type "WEAP", Existence
- `armor` → record_type "ARMO", Existence
- `spell` → record_type "SPEL", Existence
- `perk` → record_type "PERK", Existence
- `keyword` → record_type "KYWD", Existence
- `faction` → record_type "FACT", Existence
- `race` → record_type "RACE", Existence

**Property relations (from subrecords):**
- `has_keyword(FormID, KeywordID)` → ArrayField from "KWDA" in NPC_, WEAP, ARMO, ALCH, BOOK, AMMO
- `has_faction(FormID, FactionID)` → ListField from "SNAM" in NPC_ (first 4 bytes of 8-byte struct)
- `has_spell(FormID, SpellID)` → ListField from "SPLO" in NPC_
- `has_perk(FormID, PerkID)` → ListField from "PRKR" in NPC_ (first 4 bytes)
- `name(FormID, String)` → Subrecord "FULL" as LString in NPC_, WEAP, ARMO, etc.
- `editor_id(FormID, String)` → Subrecord "EDID" as ZString in all record types
- `damage(FormID, Int)` → PackedField from "DATA" in WEAP, offset=8, Int16
- `gold_value(FormID, Int)` → PackedField from "DATA" in WEAP offset=0, Int32
- `weight(FormID, Float)` → PackedField from "DATA" in WEAP offset=4, Float32
- `armor_rating(FormID, Int)` → Subrecord "DNAM" in ARMO, offset=0, Int32 (divided by 100)
- `base_level(FormID, Int)` → PackedField from "ACBS" in NPC_, offset=8, Int16
- `race_of(FormID, RaceID)` → Subrecord "RNAM" in NPC_, FormID

`configure_fact_db()` iterates all schemas and calls `db.configure_relation()` with the proper arity and index columns.

- [ ] **Step 3: Run tests**

Run: `xmake build schema_registry_test && xmake run schema_registry_test`
Expected: All 6 tests PASS

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "feat: schema registry mapping relations to ESP extraction rules"
```

---

### Task 5: MmapFile — Memory-Mapped File Access

**Files:**
- Create: `include/mora/esp/mmap_file.h`
- Create: `src/esp/mmap_file.cpp`
- Create: `tests/mmap_file_test.cpp`

Adapted from dovaquack's pattern: mmap the file, close fd immediately, expose via `std::span<const uint8_t>`.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/mmap_file_test.cpp
#include <gtest/gtest.h>
#include "mora/esp/mmap_file.h"
#include <fstream>
#include <filesystem>

class MmapFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temp test file
        std::ofstream out("test_mmap.bin", std::ios::binary);
        uint32_t magic = 0x34534554; // "TES4" little-endian
        out.write(reinterpret_cast<const char*>(&magic), 4);
        std::string data = "hello world";
        out.write(data.c_str(), data.size());
        out.close();
    }
    void TearDown() override {
        std::filesystem::remove("test_mmap.bin");
    }
};

TEST_F(MmapFileTest, OpenAndRead) {
    mora::MmapFile file("test_mmap.bin");
    EXPECT_GT(file.size(), 0u);
    auto data = file.span();
    EXPECT_EQ(data[0], 'T');
    EXPECT_EQ(data[1], 'E');
    EXPECT_EQ(data[2], 'S');
    EXPECT_EQ(data[3], '4');
}

TEST_F(MmapFileTest, SubSpan) {
    mora::MmapFile file("test_mmap.bin");
    auto sub = file.span(4, 5); // "hello"
    EXPECT_EQ(sub.size(), 5u);
    EXPECT_EQ(sub[0], 'h');
}

TEST_F(MmapFileTest, NonExistentFile) {
    EXPECT_THROW(mora::MmapFile("nonexistent.bin"), std::runtime_error);
}
```

- [ ] **Step 2: Implement MmapFile**

```cpp
// include/mora/esp/mmap_file.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace mora {

class MmapFile {
public:
    explicit MmapFile(const std::string& path);
    ~MmapFile();

    MmapFile(const MmapFile&) = delete;
    MmapFile& operator=(const MmapFile&) = delete;
    MmapFile(MmapFile&& other) noexcept;
    MmapFile& operator=(MmapFile&& other) noexcept;

    std::span<const uint8_t> span() const;
    std::span<const uint8_t> span(size_t offset, size_t length) const;
    size_t size() const { return size_; }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
};

} // namespace mora
```

Implementation uses POSIX `mmap()` with `MAP_PRIVATE | PROT_READ`. Close fd after mapping. Destructor calls `munmap()`.

- [ ] **Step 3: Build, test, commit**

```bash
xmake build mmap_file_test && xmake run mmap_file_test
git add -A && git commit -m "feat: memory-mapped file access for zero-copy ESP reading"
```

---

### Task 6: Record Types and Headers

**Files:**
- Create: `include/mora/esp/record_types.h`
- Create: `src/esp/record_types.cpp`
- Create: `tests/record_types_test.cpp`

Define the binary structures for TES4, GRUP, record, and subrecord headers. These are `reinterpret_cast`'d directly from mmap'd memory — no deserialization.

- [ ] **Step 1: Define header structs**

```cpp
// include/mora/esp/record_types.h
#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

namespace mora {

// 4-character record type tag (e.g., "NPC_", "WEAP", "GRUP")
struct RecordTag {
    char bytes[4];

    bool operator==(const RecordTag& other) const { return std::memcmp(bytes, other.bytes, 4) == 0; }
    bool operator==(const char* str) const { return std::memcmp(bytes, str, 4) == 0; }
    std::string_view as_sv() const { return {bytes, 4}; }

    static RecordTag from(const char* s) { RecordTag t; std::memcpy(t.bytes, s, 4); return t; }
};

// Record header: 24 bytes, cast directly from file data
struct RawRecordHeader {
    RecordTag type;          // "NPC_", "WEAP", etc.
    uint32_t data_size;      // size of subrecords data
    uint32_t flags;          // 0x00040000 = COMPRESSED
    uint32_t form_id;        // local FormID (needs master resolution)
    uint16_t timestamp;
    uint16_t vcs_info;
    uint16_t internal_version;
    uint16_t unknown;
};
static_assert(sizeof(RawRecordHeader) == 24);

// GRUP header: 24 bytes
struct RawGrupHeader {
    RecordTag type;          // always "GRUP"
    uint32_t group_size;     // includes this header
    uint32_t label;          // FormID or record type depending on group_type
    uint32_t group_type;     // 0=TopLevel, 1=WorldChildren, etc.
    uint16_t timestamp;
    uint16_t vcs_info;
    uint32_t unknown;
};
static_assert(sizeof(RawGrupHeader) == 24);

// Subrecord header: 6 bytes
struct RawSubrecordHeader {
    RecordTag type;          // "EDID", "DATA", "KWDA", etc.
    uint16_t data_size;      // size of following data (or 0 if preceded by XXXX)
};
static_assert(sizeof(RawSubrecordHeader) == 6);

// Record flags
namespace RecordFlags {
    constexpr uint32_t COMPRESSED = 0x00040000;
    constexpr uint32_t ESM       = 0x00000001;
    constexpr uint32_t LOCALIZED = 0x00000080;
    constexpr uint32_t ESL       = 0x00000200;
}

// Cast helpers (zero-copy from mmap'd data)
inline const RawRecordHeader* read_record_header(const uint8_t* data) {
    return reinterpret_cast<const RawRecordHeader*>(data);
}
inline const RawGrupHeader* read_grup_header(const uint8_t* data) {
    return reinterpret_cast<const RawGrupHeader*>(data);
}
inline const RawSubrecordHeader* read_subrecord_header(const uint8_t* data) {
    return reinterpret_cast<const RawSubrecordHeader*>(data);
}

} // namespace mora
```

- [ ] **Step 2: Write tests verifying struct sizes and cast helpers**

Test that headers can be constructed from raw bytes and read correctly.

- [ ] **Step 3: Build, test, commit**

```bash
xmake build record_types_test && xmake run record_types_test
git add -A && git commit -m "feat: ESP record/subrecord header types for zero-copy parsing"
```

---

### Task 7: Plugin Index — Record Location Index

**Files:**
- Create: `include/mora/esp/plugin_index.h`
- Create: `src/esp/plugin_index.cpp`
- Create: `tests/plugin_index_test.cpp`

The plugin index scans an ESP file once, recording the location (byte offset) and FormID of every record. This is the first pass — subrecords are read later on demand.

- [ ] **Step 1: Define PluginIndex**

```cpp
// include/mora/esp/plugin_index.h
#pragma once

#include "mora/esp/mmap_file.h"
#include "mora/esp/record_types.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mora {

struct RecordLocation {
    uint32_t form_id;       // local FormID (needs master resolution)
    uint32_t offset;        // byte offset of record header in file
    uint32_t data_size;     // subrecord data size
    uint32_t flags;         // record flags
};

struct PluginInfo {
    float version;
    uint32_t num_records;
    uint32_t flags;                        // TES4 flags (LOCALIZED, ESM, ESL)
    std::vector<std::string> masters;      // master plugin filenames
    std::string filename;                  // this plugin's filename

    // Records grouped by type tag string (e.g., "NPC_" → [loc1, loc2, ...])
    std::unordered_map<std::string, std::vector<RecordLocation>> by_type;
};

// Build the index by scanning all GRUPs recursively
PluginInfo build_plugin_index(const MmapFile& file, const std::string& filename);

// Resolve a local FormID to (master_filename, object_id)
struct ResolvedFormID {
    std::string_view master;   // plugin filename
    uint32_t object_id;        // lower 24 bits
};
ResolvedFormID resolve_form_id(uint32_t raw_id, const PluginInfo& info);

} // namespace mora
```

Implementation:
1. Parse TES4 header: read HEDR subrecord (version, num_records), MAST subrecords (master filenames)
2. Scan top-level GRUPs recursively: for each record, store its type, FormID, offset, flags
3. Handle GRUP types: TopLevel (type 0) contains records of a single type

- [ ] **Step 2: Write tests against a real or synthetic ESP**

For testing, create a minimal synthetic ESP in the test (write the binary header bytes directly). Or if there's a real ESP available, use that.

- [ ] **Step 3: Build, test, commit**

```bash
xmake build plugin_index_test && xmake run plugin_index_test
git add -A && git commit -m "feat: plugin index scans ESP files for record locations"
```

---

### Task 8: Subrecord Reader — Zero-Copy Subrecord Iteration

**Files:**
- Create: `include/mora/esp/subrecord_reader.h`
- Create: `src/esp/subrecord_reader.cpp`

An iterator that walks subrecords within a record's data span. Handles XXXX extended subrecords and compressed records (via zlib decompression).

- [ ] **Step 1: Implement SubrecordReader**

```cpp
// include/mora/esp/subrecord_reader.h
#pragma once

#include "mora/esp/record_types.h"
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace mora {

struct Subrecord {
    RecordTag type;
    std::span<const uint8_t> data;
};

class SubrecordReader {
public:
    // Read subrecords from record data. If compressed, decompresses first.
    SubrecordReader(std::span<const uint8_t> record_data, uint32_t flags);

    // Iterate: returns next subrecord, or empty optional when done
    bool next(Subrecord& out);

    // Convenience: find first subrecord with given tag
    std::span<const uint8_t> find(const char* tag) const;

    // Convenience: find all subrecords with given tag (for repeating)
    std::vector<std::span<const uint8_t>> find_all(const char* tag) const;

private:
    std::span<const uint8_t> data_;
    std::vector<uint8_t> decompressed_; // buffer for compressed records
    size_t offset_ = 0;
    uint32_t xxxx_size_ = 0;
};

} // namespace mora
```

Key implementation details:
- If `flags & COMPRESSED`, read first 4 bytes as decompressed size, zlib decompress the rest into `decompressed_`, then iterate over that buffer.
- Handle XXXX subrecords: if tag is "XXXX" and size is 4, read the uint32 as the next subrecord's actual size.
- `find()` and `find_all()` iterate all subrecords (resetting offset), collecting matches.

- [ ] **Step 2: Build and commit**

```bash
xmake build mora_lib
git add -A && git commit -m "feat: zero-copy subrecord reader with XXXX and compression support"
```

Note: add zlib to xmake.lua:
```lua
add_requires("zlib")
-- and in mora_lib target:
add_packages("zlib")
```

---

### Task 9: ESP Reader — Populate FactDB from Plugin Files

**Files:**
- Create: `include/mora/esp/esp_reader.h`
- Create: `src/esp/esp_reader.cpp`
- Create: `tests/esp_reader_test.cpp`

The high-level ESP reader ties everything together: opens a plugin file, builds the index, then iterates records to populate the FactDB according to the SchemaRegistry.

- [ ] **Step 1: Define EspReader**

```cpp
// include/mora/esp/esp_reader.h
#pragma once

#include "mora/data/schema_registry.h"
#include "mora/eval/fact_db.h"
#include "mora/esp/mmap_file.h"
#include "mora/esp/plugin_index.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <filesystem>
#include <string>
#include <unordered_map>

namespace mora {

class EspReader {
public:
    EspReader(StringPool& pool, DiagBag& diags, const SchemaRegistry& schema);

    // Read a single plugin, populate facts into the FactDB
    void read_plugin(const std::filesystem::path& path, FactDB& db);

    // Read all plugins in load order
    void read_load_order(const std::vector<std::filesystem::path>& plugins, FactDB& db);

    // Symbol table: EditorID → FormID (built during reading)
    const std::unordered_map<std::string, uint32_t>& editor_id_map() const { return editor_ids_; }

    // Resolve a symbolic reference from .mora files
    uint32_t resolve_symbol(const std::string& editor_id) const;

private:
    void extract_facts(const MmapFile& file, const PluginInfo& info,
                       const std::string& record_type,
                       const std::vector<RecordLocation>& records,
                       FactDB& db);

    // Read a specific value from subrecord data based on EspSource descriptor
    Value read_value(std::span<const uint8_t> data, const EspSource& source,
                     const PluginInfo& info);

    StringPool& pool_;
    DiagBag& diags_;
    const SchemaRegistry& schema_;
    std::unordered_map<std::string, uint32_t> editor_ids_; // EditorID → resolved FormID
    uint32_t current_load_index_ = 0;
};

} // namespace mora
```

Implementation:
1. `read_plugin()`: Open MmapFile, build_plugin_index(), then for each record type in the index, look up schemas_for_record() and call extract_facts().
2. `extract_facts()`: For each record location, open a SubrecordReader. Based on each schema's EspSource kind:
   - Existence: add fact with just the resolved FormID
   - Subrecord: find the tagged subrecord, read the value
   - PackedField: find the tagged subrecord, read at offset
   - ArrayField: find the tagged subrecord, iterate elements of element_size
   - ListField: find_all the tagged subrecord, one fact per occurrence
3. Always extract EDID subrecord for the symbol table.
4. `resolve_symbol()`: Look up editor_ids_ map, return FormID or 0 if not found.

- [ ] **Step 2: Write tests using a synthetic minimal ESP**

Create a helper that writes a minimal valid ESP in memory (TES4 header + one GRUP + a few records with subrecords) and test that facts are correctly extracted.

- [ ] **Step 3: Build, test, commit**

```bash
xmake build esp_reader_test && xmake run esp_reader_test
git add -A && git commit -m "feat: ESP reader populates FactDB from plugin files via schema registry"
```

---

### Task 10: Load Order

**Files:**
- Create: `include/mora/esp/load_order.h`
- Create: `src/esp/load_order.cpp`

Read Skyrim's `plugins.txt` and `loadorder.txt` to determine the load order of installed plugins.

- [ ] **Step 1: Implement load order reader**

```cpp
// include/mora/esp/load_order.h
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mora {

struct LoadOrder {
    std::vector<std::filesystem::path> plugins; // in load order
    std::filesystem::path data_dir;             // Skyrim Data/ directory

    // Read from a plugins.txt file (MO2 style or vanilla)
    static LoadOrder from_plugins_txt(const std::filesystem::path& plugins_txt,
                                       const std::filesystem::path& data_dir);

    // Read from explicit list of paths (for testing)
    static LoadOrder from_paths(const std::vector<std::filesystem::path>& paths);
};

} // namespace mora
```

- [ ] **Step 2: Build and commit**

```bash
xmake build mora_lib
git add -A && git commit -m "feat: load order reader for plugins.txt"
```

---

### Task 11: Wire ESP Reader into `mora compile`

**Files:**
- Modify: `src/main.cpp`

Update the compile command to optionally read ESP files and populate the FactDB before evaluation. Add `--data-dir` flag pointing to Skyrim's Data/ folder, and `--plugins-txt` for the load order.

When ESP data is provided, the evaluator will actually produce patches. Without it (current behavior), the FactDB is empty and no patches are generated.

Also wire up the EditorID symbol table so that `:SymbolName` references in .mora files are resolved to real FormIDs.

- [ ] **Step 1: Add flags and ESP loading phase**

Add between type checking and evaluation:
```
  Loading ESPs ··················· 512 plugins  1.8s
```

- [ ] **Step 2: Test with real data (manual)**

If a Skyrim data directory is available, run:
```bash
mora compile --data-dir /path/to/skyrim/Data test_data/example.mora
```

Without real data, the existing behavior (empty FactDB, 0 patches) is preserved.

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "feat: wire ESP reader into mora compile pipeline"
```

---

### Task 12: Update xmake.lua + Final Verification

**Files:**
- Modify: `xmake.lua`

Add `src/data/*.cpp` and `src/esp/*.cpp` to mora_lib. Add zlib dependency.

- [ ] **Step 1: Update xmake.lua**

```lua
add_requires("zlib")

target("mora_lib")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
              "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
              "src/cli/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp",
              "src/data/*.cpp", "src/esp/*.cpp")
    add_packages("zlib")
target_end()
```

- [ ] **Step 2: Full build and test**

```bash
xmake build && xmake test
```
Expected: All tests pass.

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "chore: add data/ and esp/ directories to build, add zlib dependency"
```

---

## Summary

This plan delivers:
- **IndexedRelation**: Hash-indexed tuple storage for O(1) Datalog joins (9 tests)
- **SchemaRegistry**: Generic mapping from relation names to ESP extraction rules (6 tests)
- **Value refactor**: Shared type between FactDB and ESP parser
- **MmapFile**: Zero-copy file access (3 tests)
- **Record types**: Binary struct definitions for ESP headers
- **PluginIndex**: First-pass record indexing
- **SubrecordReader**: Zero-copy subrecord iteration with XXXX and compression support
- **EspReader**: Full ESP→FactDB pipeline using SchemaRegistry
- **LoadOrder**: plugins.txt reader
- **CLI integration**: `mora compile --data-dir` reads real game data

**Key architectural wins:**
- Facts are generic indexed tuples — no hardcoded special cases
- Schema registry is data, not code — extensible by users
- Same FactDB abstraction works for both ESP (offline) and SKSE (runtime)
- Hash indexes make Datalog joins O(1) instead of O(n) per fact lookup
- Zero-copy ESP parsing via mmap — minimal allocation overhead
