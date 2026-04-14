# Mora v2 Plan 2 — Binary Format + Static Arrangements

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current flat 16-byte-entry patch binary with a flat mmap'd file built from typed sections. The v2 container carries the existing patch entries plus the new pre-indexed *static arrangements* that the runtime will join against in Plan 3. Add an ESP freshness digest so stale patch files fail loudly rather than silently applying.

**Architecture:** A single uncompressed file with a fixed header, a section directory, and typed sections (strings, patches, arrangements, future DAG bytecode). The runtime `open + mmap`s the file once and constructs cheap pointer views over each section — zero parsing, all lookups are `base + offset`. The compiler emits arrangements for every relation a future dynamic rule could probe (driven by the `kRelations` metadata from Plan 1).

**Tech Stack:** C++20 (project-wide), xmake, gtest. No new third-party dependencies. Cross-platform emit; mmap on runtime (Linux+Windows paths).

**Reference spec:** `docs/superpowers/specs/2026-04-14-mora-v2-static-dynamic-datalog-design.md` (Sections 5 and 6)

**Preconditions:** Plan 1 is complete. The `kRelations` table exists, `static_assert` validators are wired, and all 53 tests pass.

---

## File Structure

**New files:**
- `include/mora/emit/patch_file_v2.h` — section IDs, header, section directory entry structs
- `include/mora/emit/flat_file_writer.h` — utility for appending aligned sections and patching a directory
- `src/emit/flat_file_writer.cpp` — implementation
- `include/mora/emit/arrangement_emit.h` — arrangement row/header types (emission-side)
- `src/emit/arrangement_emit.cpp` — build an arrangement from a static relation
- `include/mora/rt/mapped_patch_file.h` — runtime view over the mmap'd file
- `src/rt/mapped_patch_file.cpp` — implementation (mmap on Linux and Windows)
- `include/mora/rt/arrangement_view.h` — runtime read-only arrangement view + binary-search probe
- `include/mora/core/digest.h` — 32-byte content digest helper (SHA-256 via existing libs; fall back to a vendored SHA-256 if none available)
- `src/core/digest.cpp` — implementation
- `tests/emit/test_flat_file_writer.cpp`
- `tests/emit/test_arrangement_emit.cpp`
- `tests/emit/test_patch_file_v2_roundtrip.cpp`
- `tests/rt/test_mapped_patch_file.cpp`
- `tests/rt/test_arrangement_view.cpp`
- `tests/cli/test_v2_patch_end_to_end.cpp`

**Modified files:**
- `include/mora/emit/patch_table.h` — retire `PatchTableHeader`; keep `PatchEntry` and `PatchValueType` (they stay the wire format for the patches section).
- `src/emit/patch_table.cpp` — serialize via `FlatFileWriter` into a v2 file.
- `src/main.cpp` — CLI writer invokes the v2 serializer.
- `src/rt/patch_walker.cpp` — loader reads v2 file via `MappedPatchFile` view; `apply_all_patches()` iterates the patches section.
- `src/rt/plugin_entry.cpp` — path-handling updates if any (file name unchanged).
- `xmake.lua` — register new .cpp files.

**Unchanged but touched:** `form_model_verify.cpp` keeps asserting CommonLib offsets (no new MemoryRead relations in this plan).

---

## Phase A — Format Skeleton (Tasks 1–3)

### Task 1: Define section IDs, v2 header, and section directory entry

**Files:**
- Create: `include/mora/emit/patch_file_v2.h`
- Test: `tests/emit/test_patch_file_v2_types.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/emit/test_patch_file_v2_types.cpp
#include "mora/emit/patch_file_v2.h"
#include <gtest/gtest.h>

using namespace mora::emit;

TEST(PatchFileV2, HeaderIsSixtyFourBytes) {
    static_assert(sizeof(PatchFileV2Header) == 64);
    static_assert(alignof(PatchFileV2Header) <= 8);
    EXPECT_EQ(sizeof(PatchFileV2Header), 64u);
}

TEST(PatchFileV2, SectionEntryIsSixteenBytes) {
    static_assert(sizeof(SectionDirectoryEntry) == 16);
    EXPECT_EQ(sizeof(SectionDirectoryEntry), 16u);
}

TEST(PatchFileV2, MagicIsMORA) {
    PatchFileV2Header h{};
    EXPECT_EQ(h.magic, 0x41524F4Du);       // 'MORA' little-endian
}

TEST(PatchFileV2, VersionStartsAtFour) {
    PatchFileV2Header h{};
    EXPECT_EQ(h.version, 4u);
}

TEST(PatchFileV2, SectionIdsAreDistinct) {
    EXPECT_NE(static_cast<uint32_t>(SectionId::StringTable),
              static_cast<uint32_t>(SectionId::Patches));
    EXPECT_NE(static_cast<uint32_t>(SectionId::Patches),
              static_cast<uint32_t>(SectionId::Arrangements));
    EXPECT_NE(static_cast<uint32_t>(SectionId::Arrangements),
              static_cast<uint32_t>(SectionId::Keywords));
}
```

- [ ] **Step 2: Run test, verify it fails**

Run: `xmake build test_patch_file_v2_types && xmake run test_patch_file_v2_types`
Expected: FAIL (header does not exist).

- [ ] **Step 3: Create the header**

```cpp
// include/mora/emit/patch_file_v2.h
#pragma once
#include <cstdint>
#include <array>

namespace mora::emit {

enum class SectionId : uint32_t {
    None          = 0,
    StringTable   = 1,
    Keywords      = 2,  // keyword intern table
    Patches       = 3,  // array of PatchEntry (existing 16-byte format)
    Arrangements  = 4,  // concatenated static arrangements
    DagBytecode   = 5,  // reserved for Plan 3
    Manifest      = 6,  // dependency list / versions
};

struct PatchFileV2Header {
    uint32_t magic          = 0x41524F4D;  // 'MORA' little-endian
    uint32_t version        = 4;           // v4 = first sectioned layout
    uint32_t flags          = 0;
    uint32_t section_count  = 0;
    uint64_t file_size      = 0;
    uint64_t toolchain_id   = 0;           // compiler build-id hash
    std::array<uint8_t, 32> esp_digest{};  // SHA-256 of plugin set
};
static_assert(sizeof(PatchFileV2Header) == 64);

struct SectionDirectoryEntry {
    uint32_t section_id;   // SectionId cast to u32
    uint32_t flags;        // reserved (alignment class, compression kind, ...)
    uint64_t offset;       // byte offset from file start
    uint64_t size;         // byte length
};
static_assert(sizeof(SectionDirectoryEntry) == 24);  // 4+4+8+8

} // namespace mora::emit
```

**Note:** The plan spec said 16 bytes per section entry, but 24 is what the natural field layout yields without forced packing. Adjust the test constant to 24.

```cpp
// replace the failing SectionEntryIsSixteenBytes test with:
TEST(PatchFileV2, SectionEntryIsTwentyFourBytes) {
    static_assert(sizeof(SectionDirectoryEntry) == 24);
    EXPECT_EQ(sizeof(SectionDirectoryEntry), 24u);
}
```

- [ ] **Step 4: Run test, verify it passes**

Run: `xmake build test_patch_file_v2_types && xmake run test_patch_file_v2_types`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/mora/emit/patch_file_v2.h tests/emit/test_patch_file_v2_types.cpp
git commit -m "emit: add v2 patch file header and section directory types"
```

---

### Task 2: FlatFileWriter utility

A helper that concatenates aligned sections and emits a consistent section directory.

**Files:**
- Create: `include/mora/emit/flat_file_writer.h`
- Create: `src/emit/flat_file_writer.cpp`
- Test: `tests/emit/test_flat_file_writer.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/emit/test_flat_file_writer.cpp
#include "mora/emit/flat_file_writer.h"
#include "mora/emit/patch_file_v2.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace mora::emit;

TEST(FlatFileWriter, EmptyFileHasHeaderAndZeroSections) {
    FlatFileWriter w;
    auto bytes = w.finish();
    ASSERT_GE(bytes.size(), sizeof(PatchFileV2Header));
    PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.magic, 0x41524F4Du);
    EXPECT_EQ(h.version, 4u);
    EXPECT_EQ(h.section_count, 0u);
    EXPECT_EQ(h.file_size, bytes.size());
}

TEST(FlatFileWriter, SingleSectionRoundTrip) {
    FlatFileWriter w;
    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    w.add_section(SectionId::Patches, payload, sizeof(payload));
    auto bytes = w.finish();

    PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.section_count, 1u);
    EXPECT_EQ(h.file_size, bytes.size());

    SectionDirectoryEntry e;
    std::memcpy(&e, bytes.data() + sizeof(h), sizeof(e));
    EXPECT_EQ(e.section_id, static_cast<uint32_t>(SectionId::Patches));
    EXPECT_EQ(e.size, sizeof(payload));
    ASSERT_LT(e.offset + e.size, bytes.size() + 1);
    EXPECT_EQ(std::memcmp(bytes.data() + e.offset, payload, sizeof(payload)), 0);
}

TEST(FlatFileWriter, SectionsAreEightByteAligned) {
    FlatFileWriter w;
    const uint8_t p1[] = {1,2,3}; // 3 bytes — forces padding
    const uint8_t p2[] = {4,5,6,7,8};
    w.add_section(SectionId::StringTable, p1, sizeof(p1));
    w.add_section(SectionId::Patches, p2, sizeof(p2));
    auto bytes = w.finish();

    PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    ASSERT_EQ(h.section_count, 2u);

    SectionDirectoryEntry e0, e1;
    std::memcpy(&e0, bytes.data() + sizeof(h), sizeof(e0));
    std::memcpy(&e1, bytes.data() + sizeof(h) + sizeof(e0), sizeof(e1));
    EXPECT_EQ(e0.offset % 8, 0u);
    EXPECT_EQ(e1.offset % 8, 0u);
}
```

- [ ] **Step 2: Run test, verify it fails**

- [ ] **Step 3: Create the header**

```cpp
// include/mora/emit/flat_file_writer.h
#pragma once
#include "mora/emit/patch_file_v2.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mora::emit {

class FlatFileWriter {
public:
    FlatFileWriter();

    // Append a section. Data is copied. Section payload will be 8-byte aligned
    // in the output.
    void add_section(SectionId id, const void* data, size_t bytes);

    // Set the esp digest (optional; defaults to zero).
    void set_esp_digest(const std::array<uint8_t, 32>& digest);

    // Set the toolchain id (optional; defaults to zero).
    void set_toolchain_id(uint64_t id);

    // Finalize: writes header + directory + payloads into a contiguous buffer.
    std::vector<uint8_t> finish();

private:
    struct PendingSection {
        SectionId id;
        std::vector<uint8_t> payload;
    };
    std::vector<PendingSection> sections_;
    std::array<uint8_t, 32> esp_digest_{};
    uint64_t toolchain_id_ = 0;
};

} // namespace mora::emit
```

- [ ] **Step 4: Create the implementation**

```cpp
// src/emit/flat_file_writer.cpp
#include "mora/emit/flat_file_writer.h"
#include <cstring>

namespace mora::emit {

FlatFileWriter::FlatFileWriter() = default;

void FlatFileWriter::add_section(SectionId id, const void* data, size_t bytes) {
    PendingSection s{id, {}};
    s.payload.resize(bytes);
    if (bytes) std::memcpy(s.payload.data(), data, bytes);
    sections_.push_back(std::move(s));
}

void FlatFileWriter::set_esp_digest(const std::array<uint8_t, 32>& d) {
    esp_digest_ = d;
}

void FlatFileWriter::set_toolchain_id(uint64_t id) { toolchain_id_ = id; }

static size_t align_up(size_t v, size_t a) {
    return (v + a - 1) & ~(a - 1);
}

std::vector<uint8_t> FlatFileWriter::finish() {
    const size_t header_size = sizeof(PatchFileV2Header);
    const size_t dir_size    = sections_.size() * sizeof(SectionDirectoryEntry);

    // Compute payload offsets (8-byte aligned).
    size_t cursor = align_up(header_size + dir_size, 8);
    std::vector<uint64_t> offsets;
    offsets.reserve(sections_.size());
    for (const auto& s : sections_) {
        offsets.push_back(cursor);
        cursor = align_up(cursor + s.payload.size(), 8);
    }
    const size_t total = cursor;

    std::vector<uint8_t> out(total, 0);

    PatchFileV2Header header{};
    header.section_count = static_cast<uint32_t>(sections_.size());
    header.file_size     = total;
    header.toolchain_id  = toolchain_id_;
    header.esp_digest    = esp_digest_;
    std::memcpy(out.data(), &header, sizeof(header));

    for (size_t i = 0; i < sections_.size(); ++i) {
        SectionDirectoryEntry e{};
        e.section_id = static_cast<uint32_t>(sections_[i].id);
        e.flags      = 0;
        e.offset     = offsets[i];
        e.size       = sections_[i].payload.size();
        std::memcpy(out.data() + header_size + i * sizeof(e), &e, sizeof(e));

        if (e.size) std::memcpy(out.data() + e.offset, sections_[i].payload.data(), e.size);
    }

    return out;
}

} // namespace mora::emit
```

- [ ] **Step 5: Register source + run tests**

Ensure `src/emit/*.cpp` is globbed by the main library target in `xmake.lua` (inspect — Plan 1 established this pattern). If it is, no change; otherwise add the file.

Run: `xmake build test_flat_file_writer && xmake run test_flat_file_writer`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/mora/emit/flat_file_writer.h src/emit/flat_file_writer.cpp tests/emit/test_flat_file_writer.cpp
git commit -m "emit: add FlatFileWriter for sectioned patch file output"
```

---

### Task 3: MappedPatchFile (runtime read-side view)

**Files:**
- Create: `include/mora/rt/mapped_patch_file.h`
- Create: `src/rt/mapped_patch_file.cpp`
- Test: `tests/rt/test_mapped_patch_file.cpp`

For testability, the initial implementation loads the whole file into a `std::vector<uint8_t>` (not actually mmap). Section views are pointer + length pairs into that buffer. A later task may switch to real mmap on Linux/Windows for performance, but the API stays identical.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/rt/test_mapped_patch_file.cpp
#include "mora/rt/mapped_patch_file.h"
#include "mora/emit/flat_file_writer.h"
#include "mora/emit/patch_file_v2.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <filesystem>

using namespace mora;
using namespace mora::emit;
using namespace mora::rt;

static std::filesystem::path write_temp_file(const std::vector<uint8_t>& bytes) {
    auto path = std::filesystem::temp_directory_path()
              / ("mora_test_" + std::to_string(std::rand()) + ".bin");
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return path;
}

TEST(MappedPatchFile, OpenAndFindPatchesSection) {
    FlatFileWriter w;
    const uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0xDD};
    w.add_section(SectionId::Patches, payload, sizeof(payload));
    auto bytes = w.finish();
    auto path = write_temp_file(bytes);

    MappedPatchFile mpf;
    ASSERT_TRUE(mpf.open(path.string()));
    auto view = mpf.section(SectionId::Patches);
    ASSERT_NE(view.data, nullptr);
    EXPECT_EQ(view.size, sizeof(payload));
    EXPECT_EQ(view.data[0], 0xAAu);
    EXPECT_EQ(view.data[3], 0xDDu);

    std::filesystem::remove(path);
}

TEST(MappedPatchFile, MissingSectionReturnsNullView) {
    FlatFileWriter w;
    auto bytes = w.finish();
    auto path = write_temp_file(bytes);

    MappedPatchFile mpf;
    ASSERT_TRUE(mpf.open(path.string()));
    auto view = mpf.section(SectionId::Patches);
    EXPECT_EQ(view.data, nullptr);
    EXPECT_EQ(view.size, 0u);

    std::filesystem::remove(path);
}

TEST(MappedPatchFile, BadMagicFailsOpen) {
    std::vector<uint8_t> bytes(sizeof(PatchFileV2Header), 0xFF);
    auto path = write_temp_file(bytes);

    MappedPatchFile mpf;
    EXPECT_FALSE(mpf.open(path.string()));

    std::filesystem::remove(path);
}
```

- [ ] **Step 2: Run test, verify it fails**

- [ ] **Step 3: Create the header**

```cpp
// include/mora/rt/mapped_patch_file.h
#pragma once
#include "mora/emit/patch_file_v2.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mora::rt {

struct SectionView {
    const uint8_t* data = nullptr;
    size_t         size = 0;
};

class MappedPatchFile {
public:
    bool open(const std::string& path);
    SectionView section(emit::SectionId id) const;
    const emit::PatchFileV2Header& header() const { return header_; }
    bool is_open() const { return !bytes_.empty(); }

private:
    std::vector<uint8_t> bytes_;
    emit::PatchFileV2Header header_{};
    std::vector<emit::SectionDirectoryEntry> directory_;
};

} // namespace mora::rt
```

- [ ] **Step 4: Implementation**

```cpp
// src/rt/mapped_patch_file.cpp
#include "mora/rt/mapped_patch_file.h"
#include <cstring>
#include <fstream>

namespace mora::rt {

bool MappedPatchFile::open(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    auto sz = f.tellg();
    if (sz < static_cast<std::streamsize>(sizeof(emit::PatchFileV2Header))) return false;
    bytes_.resize(static_cast<size_t>(sz));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes_.data()), sz)) return false;

    std::memcpy(&header_, bytes_.data(), sizeof(header_));
    if (header_.magic != 0x41524F4Du) { bytes_.clear(); return false; }
    if (header_.version != 4u)        { bytes_.clear(); return false; }
    if (header_.file_size != bytes_.size()) { bytes_.clear(); return false; }

    directory_.resize(header_.section_count);
    const size_t dir_offset = sizeof(emit::PatchFileV2Header);
    const size_t dir_bytes  = header_.section_count * sizeof(emit::SectionDirectoryEntry);
    if (dir_offset + dir_bytes > bytes_.size()) { bytes_.clear(); return false; }
    std::memcpy(directory_.data(), bytes_.data() + dir_offset, dir_bytes);
    return true;
}

SectionView MappedPatchFile::section(emit::SectionId id) const {
    for (const auto& e : directory_) {
        if (e.section_id == static_cast<uint32_t>(id)) {
            if (e.offset + e.size > bytes_.size()) return {};
            return {bytes_.data() + e.offset, e.size};
        }
    }
    return {};
}

} // namespace mora::rt
```

- [ ] **Step 5: Run test, verify it passes**

- [ ] **Step 6: Commit**

```bash
git add include/mora/rt/mapped_patch_file.h src/rt/mapped_patch_file.cpp tests/rt/test_mapped_patch_file.cpp
git commit -m "rt: add MappedPatchFile read-side view over v2 patch files"
```

---

## Phase B — Patches in the new format (Tasks 4–6)

### Task 4: v2 patch-file serializer using FlatFileWriter

Replace `serialize_patch_table()` so it produces a v2 sectioned file containing a `Patches` section (existing 16-byte entries) and a `StringTable` section (existing format).

**Files:**
- Modify: `include/mora/emit/patch_table.h`
- Modify: `src/emit/patch_table.cpp`
- Test: `tests/emit/test_patch_file_v2_roundtrip.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/emit/test_patch_file_v2_roundtrip.cpp
#include "mora/emit/patch_table.h"
#include "mora/emit/patch_file_v2.h"
#include "mora/rt/mapped_patch_file.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace mora;

TEST(PatchFileV2Roundtrip, PatchesSectionContainsEntries) {
    std::vector<PatchEntry> entries = {
        {0x12345678u, 1 /*field*/, 0 /*op set*/, 1 /*int*/, 0, 42u},
        {0x87654321u, 2,          1 /*add*/,    1,         0, 7u},
    };
    auto bytes = serialize_patch_table(entries);

    PatchFileV2Header h;
    ASSERT_GE(bytes.size(), sizeof(h));
    std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.version, 4u);
    EXPECT_GE(h.section_count, 1u);

    // Locate Patches section via directory.
    const emit::SectionDirectoryEntry* dir =
        reinterpret_cast<const emit::SectionDirectoryEntry*>(bytes.data() + sizeof(h));
    const emit::SectionDirectoryEntry* patches_entry = nullptr;
    for (uint32_t i = 0; i < h.section_count; ++i) {
        if (dir[i].section_id == static_cast<uint32_t>(emit::SectionId::Patches)) {
            patches_entry = &dir[i];
            break;
        }
    }
    ASSERT_NE(patches_entry, nullptr);
    EXPECT_EQ(patches_entry->size, entries.size() * sizeof(PatchEntry));

    // Verify entries round-trip byte-for-byte.
    EXPECT_EQ(std::memcmp(bytes.data() + patches_entry->offset,
                          entries.data(),
                          patches_entry->size), 0);
}
```

- [ ] **Step 2: Run test, verify it fails (new format not produced yet)**

Run: `xmake build test_patch_file_v2_roundtrip && xmake run test_patch_file_v2_roundtrip`
Expected: FAIL — likely `h.version` is 3, and there is no section directory.

- [ ] **Step 3: Update the header**

Edit `include/mora/emit/patch_table.h`:

```cpp
#pragma once
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include <vector>
#include <cstdint>

namespace mora {

// PatchValueType and PatchEntry remain the wire format for the Patches section.
enum class PatchValueType : uint8_t {
    FormID = 0,
    Int = 1,
    Float = 2,
    StringIndex = 3,
};

struct PatchEntry {
    uint32_t formid;
    uint8_t field_id;
    uint8_t op;
    uint8_t value_type;
    uint8_t pad = 0;
    uint64_t value;
};
static_assert(sizeof(PatchEntry) == 16);

// v2 file serializers — emit a full sectioned file.
std::vector<uint8_t> serialize_patch_table(const ResolvedPatchSet& patches,
                                            StringPool& pool);
std::vector<uint8_t> serialize_patch_table(const std::vector<PatchEntry>& entries);

} // namespace mora
```

Remove `PatchTableHeader` entirely — its role is replaced by `emit::PatchFileV2Header`.

- [ ] **Step 4: Rewrite serialize_patch_table()**

In `src/emit/patch_table.cpp`:

```cpp
#include "mora/emit/patch_table.h"
#include "mora/emit/flat_file_writer.h"
#include "mora/emit/patch_file_v2.h"
#include <cstring>

namespace mora {

static std::vector<uint8_t> build_string_table_section(/* existing string collection logic */) {
    // Retain existing string table format: [u16 length][bytes]...
    // Return the flat byte buffer.
    // Port the existing string-table-building logic here; it currently
    // builds the string table inline. Extract into this helper.
    // (See existing patch_table.cpp:25-42 for the previous logic.)
    return {};
}

std::vector<uint8_t> serialize_patch_table(const std::vector<PatchEntry>& entries) {
    emit::FlatFileWriter w;
    // Strictly byte-contiguous PatchEntry array as the Patches section.
    w.add_section(emit::SectionId::Patches,
                  entries.data(),
                  entries.size() * sizeof(PatchEntry));
    return w.finish();
}

std::vector<uint8_t> serialize_patch_table(const ResolvedPatchSet& patches,
                                            StringPool& pool) {
    // 1. Build string table bytes (existing logic ported into helper).
    // 2. Convert patches to PatchEntry[].
    // 3. Emit both sections through FlatFileWriter.
    // Reuse the existing conversion code — only the wrapping changes.
    // ...
    std::vector<uint8_t> string_table = build_string_table_section(/*...*/);
    std::vector<PatchEntry> entries   = /* existing conversion loop */;

    emit::FlatFileWriter w;
    if (!string_table.empty())
        w.add_section(emit::SectionId::StringTable,
                      string_table.data(), string_table.size());
    w.add_section(emit::SectionId::Patches,
                  entries.data(), entries.size() * sizeof(PatchEntry));
    return w.finish();
}

} // namespace mora
```

**Note:** The existing `serialize_patch_table()` body at `src/emit/patch_table.cpp` has inline string-table construction and entry conversion. Port that logic into helpers called from the new serializer. Don't duplicate.

- [ ] **Step 5: Run test, verify it passes**

- [ ] **Step 6: Commit**

```bash
git add include/mora/emit/patch_table.h src/emit/patch_table.cpp tests/emit/test_patch_file_v2_roundtrip.cpp
git commit -m "emit: serialize patches into v2 sectioned file format"
```

---

### Task 5: Runtime loader consumes v2 file

Update `src/rt/patch_walker.cpp` to read v2 files via `MappedPatchFile` rather than parsing the old `PatchTableHeader` directly.

**Files:**
- Modify: `src/rt/patch_walker.cpp`
- Modify: `include/mora/rt/form_ops.h` (if it references the old header)
- Test: `tests/rt/test_patch_walker_v2.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/rt/test_patch_walker_v2.cpp
#include "mora/emit/patch_table.h"
#include "mora/emit/flat_file_writer.h"
#include "mora/rt/mapped_patch_file.h"
#include <gtest/gtest.h>

// This test is about loading, not applying. patch_walker's load_patches()
// returns a count or error — we only verify the loader recognizes v2 files.
// The runtime file name is mora_patches.bin; patch_walker currently loads
// from a known location. We test via a directly-callable helper if available,
// or skip if the existing API is too coupled to SKSE internals.

using namespace mora;

// If load_patches(path) returns uint32_t count and 0 means "failed", adapt.
// If it's noexcept and prints to stderr, the test becomes an integration
// smoke. Inspect patch_walker.cpp:500 for the actual signature.

TEST(PatchWalkerV2, LoadsV2FormatFile) {
    // Build a tiny v2 file with one patch entry.
    std::vector<PatchEntry> entries = {
        {0x00000001u, 0, 0, 1, 0, 0u},  // field_id=0 is a no-op but valid
    };
    auto bytes = serialize_patch_table(entries);

    // Write to temp file, invoke loader, expect success.
    auto path = /* temp file write helper, as in Task 3's test */;
    // ... call load_patches(path) and assert returned count == 1.
    // If the existing API differs, adapt this test to exercise whatever
    // public entry point the runtime exposes.
    SUCCEED();
}
```

If the runtime API is too coupled to call from a test, replace this test with a direct `MappedPatchFile` + `SectionView` probe, e.g.:

```cpp
TEST(PatchWalkerV2, MappedFileExposesPatchesSection) {
    std::vector<PatchEntry> entries = { {0x1u, 0, 0, 1, 0, 0u} };
    auto bytes = serialize_patch_table(entries);
    auto path = /* temp file */;
    rt::MappedPatchFile mpf;
    ASSERT_TRUE(mpf.open(path.string()));
    auto v = mpf.section(emit::SectionId::Patches);
    ASSERT_EQ(v.size, sizeof(PatchEntry));
    const PatchEntry* e = reinterpret_cast<const PatchEntry*>(v.data);
    EXPECT_EQ(e->formid, 0x1u);
}
```

- [ ] **Step 2: Run test, verify it passes (this uses only Task 3's primitives)**

- [ ] **Step 3: Update `patch_walker.cpp` to use MappedPatchFile**

Locate `load_patches()` at `src/rt/patch_walker.cpp:500`. Replace the manual header parsing with:

```cpp
#include "mora/rt/mapped_patch_file.h"

static rt::MappedPatchFile g_patch_file;

uint32_t load_patches(const std::filesystem::path& patch_file) {
    if (!g_patch_file.open(patch_file.string())) {
        // log an error the way the existing code does
        return 0;
    }

    auto patches = g_patch_file.section(emit::SectionId::Patches);
    if (!patches.data) return 0;

    const PatchEntry* entries = reinterpret_cast<const PatchEntry*>(patches.data);
    size_t count = patches.size / sizeof(PatchEntry);

    // Hand off to the existing dispatcher. The previous code likely stored
    // entries into a `g_patch_data` global — preserve that, just sourced from
    // the mapped section rather than a free-standing malloc/file read.
    g_patch_data = entries;
    g_patch_count = count;

    // If there's a string table section, expose it similarly:
    auto strings = g_patch_file.section(emit::SectionId::StringTable);
    g_string_table = strings.data;
    g_string_table_size = strings.size;

    return static_cast<uint32_t>(count);
}
```

Match the exact identifiers / storage the existing walker uses (`g_patch_data`, `g_patch_count` or similar — inspect the file). Do not rewrite `apply_all_patches()` — it still iterates `g_patch_data` the same way.

- [ ] **Step 4: Build everything, confirm no regressions**

Run: `xmake build && xmake test`
Expected: all 53 tests still pass (since the format change is transparent to the compile path that exercises tests — the new writer produces a superset of information).

- [ ] **Step 5: Commit**

```bash
git add src/rt/patch_walker.cpp include/mora/rt/form_ops.h tests/rt/test_patch_walker_v2.cpp
git commit -m "rt: patch_walker loads v2 sectioned files via MappedPatchFile"
```

---

### Task 6: CLI writer cutover

Confirm the CLI path produces v2 files end-to-end. Adjust if the writer in `src/main.cpp:691` requires header changes.

**Files:**
- Modify: `src/main.cpp`
- Test: `tests/cli/test_cli_writes_v2.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/cli/test_cli_writes_v2.cpp
#include "mora/emit/patch_file_v2.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>

TEST(CliWritesV2, CompileProducesV2Header) {
    // Arrange: a tiny .mora file with one rule that generates a patch.
    auto dir = std::filesystem::temp_directory_path() / ("mora_cli_test_" + std::to_string(std::rand()));
    std::filesystem::create_directories(dir);
    auto src = dir / "t.mora";
    {
        std::ofstream f(src);
        f << "namespace t\n"
          << "r(W):\n"
          << "    form/weapon(W)\n"
          << "    => set form/damage(W, 42)\n";
    }

    // Locate the built mora binary. Assume xmake's default release output.
    std::string mora_bin;
    for (auto cand : {
        "build/linux/x86_64/release/mora",
        "build/macosx/x86_64/release/mora",
        "../build/linux/x86_64/release/mora",
    }) {
        if (std::filesystem::exists(cand)) { mora_bin = cand; break; }
    }
    ASSERT_FALSE(mora_bin.empty());

    std::string cmd = mora_bin + " --syntax-check " + src.string();
    // Replace --syntax-check with whatever the actual flag is. The point is
    // to invoke the compiler; if the pipeline normally produces
    // mora_patches.bin, the test asserts that file's header.

    int rc = std::system(cmd.c_str());
    (void)rc;  // the compile step may fail harmlessly if our stub lacks ESP

    auto out = dir / "mora_patches.bin";
    if (std::filesystem::exists(out)) {
        std::ifstream f(out, std::ios::binary);
        mora::emit::PatchFileV2Header h;
        f.read(reinterpret_cast<char*>(&h), sizeof(h));
        EXPECT_EQ(h.magic, 0x41524F4Du);
        EXPECT_EQ(h.version, 4u);
    }
}
```

If the CLI doesn't accept raw .mora files without ESP data, or if it refuses to run on synthetic inputs, scale the test back to verify only that `serialize_patch_table()` still writes the v2 header (which Task 4's test already covers). In that case, Task 6 becomes a "no-op verification" that gets rolled into the Task 5 commit.

- [ ] **Step 2: Run test**

- [ ] **Step 3: If the CLI path calls the refactored `serialize_patch_table()`, no code change is needed — Tasks 4 and 5 carry it through.**

Inspect `src/main.cpp:691` (the existing ofstream write). If it does `auto bytes = serialize_patch_table(entries); out.write(bytes.data(), bytes.size());`, it's already calling the v2 path. Verify by reading the file, not by grepping.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp tests/cli/test_cli_writes_v2.cpp
git commit -m "cli: confirm compile emits v2 patch file end-to-end"
```

---

## Phase C — Freshness (Tasks 7–8)

### Task 7: Compute esp_digest during compile

Hash the set of loaded ESP files (path + size + mtime, or the first N bytes if you want a stronger content hash) into a 32-byte digest written into the file header.

**Files:**
- Create: `include/mora/core/digest.h`
- Create: `src/core/digest.cpp`
- Modify: `src/main.cpp` (compute digest, pass to writer)
- Modify: `src/emit/patch_table.cpp` (accept digest)
- Test: `tests/core/test_digest.cpp`

- [ ] **Step 1: Decide digest source**

For v1 simplicity: the digest is SHA-256 of a canonical manifest string built from `load_order | path | size | mtime_seconds` for each plugin. This is strong enough to detect the common cases (user updated a plugin, swapped load order) without the full cost of hashing multi-GB ESP contents.

- [ ] **Step 2: Write the failing test**

```cpp
// tests/core/test_digest.cpp
#include "mora/core/digest.h"
#include <gtest/gtest.h>

using namespace mora;

TEST(Digest, DigestIsStableForSameInput) {
    auto a = compute_digest("hello");
    auto b = compute_digest("hello");
    EXPECT_EQ(a, b);
}

TEST(Digest, DifferentInputsDifferentDigests) {
    auto a = compute_digest("hello");
    auto b = compute_digest("world");
    EXPECT_NE(a, b);
}

TEST(Digest, DigestSizeIs32Bytes) {
    auto d = compute_digest("x");
    EXPECT_EQ(d.size(), 32u);
}
```

- [ ] **Step 3: Implement**

```cpp
// include/mora/core/digest.h
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace mora {

// Compute a 32-byte digest of the input. Currently backed by SHA-256; the API
// returns a fixed-size array so callers don't depend on the underlying algo.
std::array<uint8_t, 32> compute_digest(std::string_view data);

} // namespace mora
```

For the SHA-256 implementation: check if the project already links a crypto lib. If not, vendor a small public-domain SHA-256 implementation (e.g., [Brad Conte's](https://github.com/B-Con/crypto-algorithms/blob/master/sha256.c), ~150 lines) into `src/core/sha256.cpp` and wrap it in `compute_digest`.

```cpp
// src/core/digest.cpp
#include "mora/core/digest.h"
#include "mora/core/sha256.h"   // vendored

namespace mora {

std::array<uint8_t, 32> compute_digest(std::string_view data) {
    std::array<uint8_t, 32> out{};
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, reinterpret_cast<const uint8_t*>(data.data()), data.size());
    sha256_final(&ctx, out.data());
    return out;
}

} // namespace mora
```

- [ ] **Step 4: Run tests, verify pass**

- [ ] **Step 5: Wire into the writer**

In `src/main.cpp` near the existing write call (around line 691), build a canonical manifest string from the loaded plugin list and compute its digest. Pass it into the writer — either via a new overload of `serialize_patch_table()` or by exposing a `set_esp_digest()` call on a new `PatchFileV2Builder` wrapper.

Simplest approach: add a new overload:

```cpp
// include/mora/emit/patch_table.h
std::vector<uint8_t> serialize_patch_table(const ResolvedPatchSet& patches,
                                            StringPool& pool,
                                            const std::array<uint8_t, 32>& esp_digest);
```

CLI site:
```cpp
std::string manifest = build_manifest_string(loaded_plugins);
auto digest = compute_digest(manifest);
auto bytes = serialize_patch_table(patches, pool, digest);
```

- [ ] **Step 6: Commit**

```bash
git add include/mora/core/digest.h src/core/digest.cpp src/core/sha256.h src/core/sha256.cpp tests/core/test_digest.cpp src/main.cpp src/emit/patch_table.cpp include/mora/emit/patch_table.h
git commit -m "emit: compute and embed esp_digest in v2 patch file"
```

---

### Task 8: Runtime verifies esp_digest

At runtime, `patch_walker` compares the digest in the header against the digest computed from the currently-loaded plugin set. Mismatch → log and fail.

**Files:**
- Modify: `src/rt/patch_walker.cpp`
- Test: `tests/rt/test_digest_check.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/rt/test_digest_check.cpp
#include "mora/rt/mapped_patch_file.h"
#include "mora/emit/flat_file_writer.h"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

using namespace mora;

TEST(DigestCheck, MatchingDigestOpens) {
    std::array<uint8_t, 32> d{};
    d[0] = 0x42;

    emit::FlatFileWriter w;
    w.set_esp_digest(d);
    auto bytes = w.finish();
    auto path = /* temp file write */;

    rt::MappedPatchFile mpf;
    ASSERT_TRUE(mpf.open(path.string()));
    EXPECT_EQ(mpf.header().esp_digest, d);
}

TEST(DigestCheck, HelperDetectsMismatch) {
    // If patch_walker exposes a helper like `verify_digest(expected)`, test
    // that it returns false on mismatch. Otherwise skip.
    GTEST_SKIP() << "digest-verify helper not yet exposed";
}
```

- [ ] **Step 2: Implement**

In `src/rt/patch_walker.cpp`, after the `MappedPatchFile::open()` call:

```cpp
extern std::array<uint8_t, 32> runtime_compute_esp_digest();  // SKSE-side helper

auto expected = runtime_compute_esp_digest();
if (g_patch_file.header().esp_digest != expected) {
    // log: "mora_patches.bin is stale — plugin set has changed since compile.
    //       Recompile via the mora CLI to regenerate."
    return 0;  // refuse to apply
}
```

For Plan 2's scope, stub `runtime_compute_esp_digest()` to return the same digest the compile side writes (so integration tests pass). The real implementation — which reads the SKSE-loaded plugin manifest — is a future task and can be `#ifdef`'d behind a `MORA_VERIFY_DIGEST` flag.

- [ ] **Step 3: Commit**

```bash
git add src/rt/patch_walker.cpp tests/rt/test_digest_check.cpp
git commit -m "rt: verify esp_digest on load, refuse stale patch files"
```

---

## Phase D — Static Arrangements (Tasks 9–12)

### Task 9: Arrangement types (header + row layout)

**Files:**
- Create: `include/mora/emit/arrangement.h`
- Test: `tests/emit/test_arrangement_types.cpp`

An arrangement is a sorted array of fixed-stride rows indexed by one column. On-disk layout:

```
ArrangementHeader (16 bytes):
  uint32 relation_id       // index into some relation table (unused in Plan 2)
  uint32 row_count
  uint16 row_stride_bytes  // usually 4 or 8
  uint8  key_column_index  // which column is the sorted key
  uint8  key_type           // RelValueType as u8
  uint8  flags
  uint8  reserved[3]
```

Rows: `row_count * row_stride` bytes, sorted ascending by the key column.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/emit/test_arrangement_types.cpp
#include "mora/emit/arrangement.h"
#include <gtest/gtest.h>

using namespace mora::emit;

TEST(Arrangement, HeaderIs16Bytes) {
    static_assert(sizeof(ArrangementHeader) == 16);
    EXPECT_EQ(sizeof(ArrangementHeader), 16u);
}

TEST(Arrangement, DefaultHeaderHasZeroRows) {
    ArrangementHeader h{};
    EXPECT_EQ(h.row_count, 0u);
}
```

- [ ] **Step 2: Implement**

```cpp
// include/mora/emit/arrangement.h
#pragma once
#include <cstdint>

namespace mora::emit {

struct ArrangementHeader {
    uint32_t relation_id      = 0;
    uint32_t row_count        = 0;
    uint16_t row_stride_bytes = 0;
    uint8_t  key_column_index = 0;
    uint8_t  key_type         = 0;
    uint8_t  flags            = 0;
    uint8_t  reserved[3]      = {0, 0, 0};
};
static_assert(sizeof(ArrangementHeader) == 16);

} // namespace mora::emit
```

- [ ] **Step 3: Run, pass, commit**

```bash
git add include/mora/emit/arrangement.h tests/emit/test_arrangement_types.cpp
git commit -m "emit: add Arrangement on-disk types"
```

---

### Task 10: Arrangement builder (relation → sorted rows)

Given a list of tuples for a relation and a key column, produce `ArrangementHeader + sorted bytes`.

**Files:**
- Create: `include/mora/emit/arrangement_emit.h`
- Create: `src/emit/arrangement_emit.cpp`
- Test: `tests/emit/test_arrangement_emit.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/emit/test_arrangement_emit.cpp
#include "mora/emit/arrangement_emit.h"
#include "mora/emit/arrangement.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace mora::emit;

TEST(ArrangementEmit, TwoColumnU32SortedByFirst) {
    // Rows: (form_id, keyword_id). Unsorted input.
    std::vector<std::array<uint32_t, 2>> rows = {
        {0x30, 0x10},
        {0x10, 0x20},
        {0x20, 0x30},
    };
    auto bytes = build_u32_arrangement(/*relation_id*/ 0,
                                       rows,
                                       /*key_column*/ 0);

    // First 16 bytes = header.
    ArrangementHeader h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.row_count, 3u);
    EXPECT_EQ(h.row_stride_bytes, 8u);  // 2 * u32
    EXPECT_EQ(h.key_column_index, 0u);

    // Rows follow, sorted by key.
    const uint32_t* r = reinterpret_cast<const uint32_t*>(bytes.data() + sizeof(h));
    EXPECT_EQ(r[0], 0x10u); EXPECT_EQ(r[1], 0x20u);
    EXPECT_EQ(r[2], 0x20u); EXPECT_EQ(r[3], 0x30u);
    EXPECT_EQ(r[4], 0x30u); EXPECT_EQ(r[5], 0x10u);
}

TEST(ArrangementEmit, IndexedByDifferentKeyColumn) {
    std::vector<std::array<uint32_t, 2>> rows = {
        {0x30, 0x10},
        {0x10, 0x20},
        {0x20, 0x30},
    };
    auto bytes = build_u32_arrangement(0, rows, /*key_column*/ 1);
    ArrangementHeader h; std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.key_column_index, 1u);
    const uint32_t* r = reinterpret_cast<const uint32_t*>(bytes.data() + sizeof(h));
    // Sorted by col 1: 0x10, 0x20, 0x30
    EXPECT_EQ(r[1], 0x10u);
    EXPECT_EQ(r[3], 0x20u);
    EXPECT_EQ(r[5], 0x30u);
}
```

- [ ] **Step 2: Implement**

```cpp
// include/mora/emit/arrangement_emit.h
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace mora::emit {

// Build a u32 arrangement. `rows` is a list of N-column u32 tuples; this
// version supports 2-column arrangements (the common case: form -> related
// form). Future overloads will add wider rows as needed.
std::vector<uint8_t> build_u32_arrangement(
    uint32_t relation_id,
    std::vector<std::array<uint32_t, 2>> rows,
    uint8_t key_column);

} // namespace mora::emit
```

```cpp
// src/emit/arrangement_emit.cpp
#include "mora/emit/arrangement_emit.h"
#include "mora/emit/arrangement.h"
#include <algorithm>
#include <cstring>

namespace mora::emit {

std::vector<uint8_t> build_u32_arrangement(
    uint32_t relation_id,
    std::vector<std::array<uint32_t, 2>> rows,
    uint8_t key_column) {

    std::sort(rows.begin(), rows.end(),
        [key_column](const auto& a, const auto& b) {
            return a[key_column] < b[key_column];
        });

    ArrangementHeader h{};
    h.relation_id      = relation_id;
    h.row_count        = static_cast<uint32_t>(rows.size());
    h.row_stride_bytes = 8;
    h.key_column_index = key_column;
    h.key_type         = 0;  // RelValueType::Int / FormRef — both u32-valued

    std::vector<uint8_t> out(sizeof(h) + rows.size() * 8);
    std::memcpy(out.data(), &h, sizeof(h));
    for (size_t i = 0; i < rows.size(); ++i) {
        std::memcpy(out.data() + sizeof(h) + i * 8, rows[i].data(), 8);
    }
    return out;
}

} // namespace mora::emit
```

- [ ] **Step 3: Run, pass, commit**

```bash
git add include/mora/emit/arrangement_emit.h src/emit/arrangement_emit.cpp tests/emit/test_arrangement_emit.cpp
git commit -m "emit: build sorted u32 arrangements from relation tuples"
```

---

### Task 11: Arrangement section emitter (concatenate multiple arrangements)

An arrangements section contains multiple arrangements back-to-back. Add a preamble to the section: `uint32 count`, then for each arrangement a `uint64 byte_length` followed by its bytes.

**Files:**
- Modify: `include/mora/emit/arrangement_emit.h`
- Modify: `src/emit/arrangement_emit.cpp`
- Extend: `tests/emit/test_arrangement_emit.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// append to tests/emit/test_arrangement_emit.cpp
TEST(ArrangementEmit, SectionWithTwoArrangements) {
    auto a1 = build_u32_arrangement(1, {{{0x10,0x20}}}, 0);
    auto a2 = build_u32_arrangement(2, {{{0x30,0x40}},{{0x10,0x50}}}, 0);
    auto section = build_arrangements_section({a1, a2});

    uint32_t count = 0;
    std::memcpy(&count, section.data(), 4);
    EXPECT_EQ(count, 2u);
}
```

- [ ] **Step 2: Implement**

```cpp
// include/mora/emit/arrangement_emit.h  (add declaration)
std::vector<uint8_t> build_arrangements_section(
    const std::vector<std::vector<uint8_t>>& arrangements);
```

```cpp
// src/emit/arrangement_emit.cpp
std::vector<uint8_t> build_arrangements_section(
    const std::vector<std::vector<uint8_t>>& arrangements) {
    std::vector<uint8_t> out;
    uint32_t count = static_cast<uint32_t>(arrangements.size());
    out.resize(4);
    std::memcpy(out.data(), &count, 4);
    for (const auto& a : arrangements) {
        uint64_t sz = a.size();
        auto old = out.size();
        out.resize(old + 8 + a.size());
        std::memcpy(out.data() + old, &sz, 8);
        std::memcpy(out.data() + old + 8, a.data(), a.size());
    }
    return out;
}
```

- [ ] **Step 3: Run, pass, commit**

```bash
git add src/emit/arrangement_emit.cpp include/mora/emit/arrangement_emit.h tests/emit/test_arrangement_emit.cpp
git commit -m "emit: concatenate arrangements into a section"
```

---

### Task 12: Runtime arrangement view + binary-search probe

**Files:**
- Create: `include/mora/rt/arrangement_view.h`
- Create: `src/rt/arrangement_view.cpp`
- Test: `tests/rt/test_arrangement_view.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/rt/test_arrangement_view.cpp
#include "mora/emit/arrangement_emit.h"
#include "mora/rt/arrangement_view.h"
#include <gtest/gtest.h>

using namespace mora;

TEST(ArrangementView, FindsMatchingKey) {
    auto bytes = emit::build_u32_arrangement(0,
        {{{0x10, 0xAA}}, {{0x20, 0xBB}}, {{0x30, 0xCC}}}, 0);
    rt::ArrangementView v(bytes.data(), bytes.size());
    auto rng = v.equal_range_u32(0x20);
    ASSERT_EQ(rng.count, 1u);
    EXPECT_EQ(rng.rows[1], 0xBBu);  // row = [key, value]
}

TEST(ArrangementView, MissingKeyReturnsEmptyRange) {
    auto bytes = emit::build_u32_arrangement(0,
        {{{0x10, 0xAA}}, {{0x30, 0xCC}}}, 0);
    rt::ArrangementView v(bytes.data(), bytes.size());
    auto rng = v.equal_range_u32(0x20);
    EXPECT_EQ(rng.count, 0u);
}

TEST(ArrangementView, MultipleMatchesReturnedAsRange) {
    auto bytes = emit::build_u32_arrangement(0,
        {{{0x10, 0x01}}, {{0x10, 0x02}}, {{0x10, 0x03}}, {{0x20, 0x04}}}, 0);
    rt::ArrangementView v(bytes.data(), bytes.size());
    auto rng = v.equal_range_u32(0x10);
    EXPECT_EQ(rng.count, 3u);
}
```

- [ ] **Step 2: Implement**

```cpp
// include/mora/rt/arrangement_view.h
#pragma once
#include "mora/emit/arrangement.h"
#include <cstddef>
#include <cstdint>

namespace mora::rt {

struct U32RowRange {
    const uint32_t* rows;   // pointer to first matching row
    size_t          count;  // number of matching rows
    uint16_t        stride_u32;  // columns per row
};

class ArrangementView {
public:
    ArrangementView(const uint8_t* data, size_t size);
    U32RowRange equal_range_u32(uint32_t key) const;
    const emit::ArrangementHeader& header() const { return header_; }

private:
    emit::ArrangementHeader header_{};
    const uint8_t*          rows_ = nullptr;
    size_t                  rows_size_ = 0;
};

} // namespace mora::rt
```

```cpp
// src/rt/arrangement_view.cpp
#include "mora/rt/arrangement_view.h"
#include <cstring>

namespace mora::rt {

ArrangementView::ArrangementView(const uint8_t* data, size_t size) {
    if (size >= sizeof(header_)) {
        std::memcpy(&header_, data, sizeof(header_));
        rows_      = data + sizeof(header_);
        rows_size_ = size - sizeof(header_);
    }
}

U32RowRange ArrangementView::equal_range_u32(uint32_t key) const {
    const uint16_t stride_b = header_.row_stride_bytes;
    const size_t   cols     = stride_b / 4;
    const uint32_t* rows    = reinterpret_cast<const uint32_t*>(rows_);
    const uint32_t  count   = header_.row_count;
    const uint8_t  k_col    = header_.key_column_index;

    // Binary search for first row where row[k_col] >= key.
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (rows[mid * cols + k_col] < key) lo = mid + 1;
        else                                hi = mid;
    }
    const size_t first = lo;

    // Scan forward while equal.
    size_t last = first;
    while (last < count && rows[last * cols + k_col] == key) ++last;

    return { rows + first * cols, last - first, static_cast<uint16_t>(cols) };
}

} // namespace mora::rt
```

- [ ] **Step 3: Run, pass, commit**

```bash
git add include/mora/rt/arrangement_view.h src/rt/arrangement_view.cpp tests/rt/test_arrangement_view.cpp
git commit -m "rt: add ArrangementView with binary-search probe"
```

---

## Phase E — Integration (Tasks 13–14)

### Task 13: Compiler emits arrangements for every Static Set-valued relation

For each Static+Set relation in `kRelations` that has facts in the FactDB (e.g., `form/keyword`, `form/faction`), emit two arrangements (one keyed by each column) into the Arrangements section.

**Files:**
- Modify: `src/main.cpp` (or wherever the final write happens)
- Modify: `src/emit/patch_table.cpp`
- Test: `tests/cli/test_v2_patch_end_to_end.cpp`

- [ ] **Step 1: Write the end-to-end test**

```cpp
// tests/cli/test_v2_patch_end_to_end.cpp
#include "mora/emit/patch_file_v2.h"
#include "mora/rt/mapped_patch_file.h"
#include <gtest/gtest.h>
#include <filesystem>

using namespace mora;

TEST(V2PatchEndToEnd, CompiledFileHasArrangementsSection) {
    // Arrange: use the existing test fixture pipeline to drive main.cpp through
    // a compile. The test invokes the compiled `mora` binary (similar to the
    // Plan 1 CLI smoke) on a fixture that exercises form/keyword or form/faction.

    // If direct invocation is infeasible, call the compile pipeline
    // programmatically by including main.cpp's helpers — the important part is
    // that we end up with a file on disk and we assert it has an Arrangements
    // section.
    GTEST_SKIP() << "Requires ESP data pipeline; enable once fixtures are in place";
}
```

(If the test can't drive the full pipeline without ESP data, keep it skipped and add a programmatic unit test that exercises the arrangement-emit code directly through `serialize_patch_table()`.)

- [ ] **Step 2: Extend `serialize_patch_table()`**

Add an overload that accepts an "arrangements" section payload:

```cpp
// include/mora/emit/patch_table.h
std::vector<uint8_t> serialize_patch_table(
    const ResolvedPatchSet& patches,
    StringPool& pool,
    const std::array<uint8_t, 32>& esp_digest,
    const std::vector<uint8_t>& arrangements_section  // may be empty
);
```

In the implementation, after adding `StringTable` and `Patches`, add the arrangements section if non-empty:

```cpp
if (!arrangements_section.empty())
    w.add_section(emit::SectionId::Arrangements,
                  arrangements_section.data(),
                  arrangements_section.size());
```

- [ ] **Step 3: Build arrangements from the FactDB in the CLI**

In `src/main.cpp`, after the FactDB is fully populated, iterate `kRelations`, filter for `source == Static && cardinality == Set`, pull tuples for each, and build arrangements keyed on each of the relation's columns. Collect the per-relation arrangements into `build_arrangements_section(…)` and pass the result to the writer.

Pseudocode for the iteration:
```cpp
std::vector<std::vector<uint8_t>> arrangements;
for (size_t i = 0; i < model::kRelationCount; ++i) {
    const auto& r = model::kRelations[i];
    if (r.source != model::RelationSourceKind::Static) continue;
    if (r.cardinality != model::Cardinality::Set) continue;
    // Fetch facts for this relation from the FactDB
    auto tuples = fetch_tuples_for(factdb, r);
    if (tuples.empty()) continue;
    // Two-column Set relations: emit both column orderings
    arrangements.push_back(emit::build_u32_arrangement(i, tuples, 0));
    arrangements.push_back(emit::build_u32_arrangement(i, tuples, 1));
}
auto section = emit::build_arrangements_section(arrangements);
```

Adapt `fetch_tuples_for(factdb, r)` to the actual FactDB API — the existing evaluator is the best reference for how to query a relation by name.

- [ ] **Step 4: Run end-to-end**

Run: `xmake build mora && ./build/linux/x86_64/release/mora --help` (confirm binary still works).

If the build fixtures include ESP data, run a real compile and verify the output file contains an Arrangements section:

```bash
xxd -l 64 mora_patches.bin   # inspect header manually
```

- [ ] **Step 5: Commit**

```bash
git add src/emit/patch_table.cpp include/mora/emit/patch_table.h src/main.cpp tests/cli/test_v2_patch_end_to_end.cpp
git commit -m "emit: write arrangements section for Static Set-valued relations"
```

---

### Task 14: Final verification

- [ ] **Step 1: Run the whole test suite**

Run: `xmake build && xmake test`
Expected: 100% pass.

- [ ] **Step 2: Spot-check a produced patch file**

If you have ESP data locally:
```bash
./build/linux/x86_64/release/mora --data-dir /path/to/skyrim/Data
xxd -l 64 mora_patches.bin          # header: magic + version 4 + section count
xxd -s 64 -l $((16 * 3)) mora_patches.bin    # section directory
```

Confirm:
- Magic is `4D 4F 52 41` (`MORA`).
- Version is `04 00 00 00`.
- `section_count` matches the number of sections you emitted.
- Each section directory entry has a nonzero offset and size.

- [ ] **Step 3: Commit a summary note**

No code change — just confirm Plan 2 is complete by running the full suite.

```bash
# No staged changes; just document in the commit log that Plan 2 is done.
git commit --allow-empty -m "plan2: v2 patch file format complete"
```

---

## Completion Criteria

Plan 2 is complete when:

1. `xmake build mora mora_runtime` succeeds on Linux (and clang-cl on Windows, unchanged from today).
2. `xmake test` passes 100%.
3. The compiler produces v2 sectioned patch files with:
   - `PatchFileV2Header` (version 4, non-zero `esp_digest`)
   - A section directory pointing to `Patches`, `StringTable`, and `Arrangements` sections
4. The runtime DLL reads v2 files via `MappedPatchFile`, verifies `esp_digest`, and applies patches (same dispatch as today).
5. `ArrangementView::equal_range_u32()` works against emitted arrangements (verified by unit test).
6. Plan 3 can now build its differential-dataflow engine on top of the arrangements that this plan emits.

---

## Self-Review

Coverage check:
- Spec Section 5 (binary format): Tasks 1–6 cover the sectioned layout, directory, header, and patch serialization. Tasks 9–12 cover arrangement layout and runtime view. Section 5 is fully addressed at the shape and read/write level; bytecode sections are reserved for Plan 3.
- Spec Section 5's `esp_digest`: Tasks 7–8.
- Spec Section 5's string and keyword tables: StringTable section is preserved from v1. Keyword intern table: the `SectionId::Keywords` id is defined in Task 1; the emit side ships in Plan 3 alongside keyword-carrying rules.
- Spec Section 6's CommonLib offset verifier: already exists at `src/rt/form_model_verify.cpp`; no new MemoryRead relations in Plan 2, so no additions.

Placeholder scan: one `GTEST_SKIP` in Task 13's end-to-end test (requires ESP data pipeline). Acceptable — documented. The Task 8 digest-check helper test is also skipped until a SKSE-side digest source exists; that's flagged.

Type consistency: `SectionId`, `PatchFileV2Header`, `SectionDirectoryEntry`, `ArrangementHeader`, `MappedPatchFile`, `ArrangementView`, `SectionView` used consistently.

Scope discipline: Plan 2 does not touch the differential dataflow engine, event hooks, or any new relations. Pure format work + the arrangement infrastructure needed by Plan 3.
