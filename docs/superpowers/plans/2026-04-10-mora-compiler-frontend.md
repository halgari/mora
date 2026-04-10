# Mora Compiler Frontend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Mora compiler frontend so that `mora check` can parse `.mora` files, build a typed AST, resolve names, type-check rules, and report diagnostics with polished Rust/Elm-style error messages.

**Architecture:** Hand-written recursive descent parser producing a lossless CST that lowers to a typed AST. Single-pass type inference over Datalog-style rules. Diagnostics collected throughout and rendered with ANSI colors. Arena allocator for AST nodes, interned string table for identifiers.

**Tech Stack:** C++20, xmake, Google Test, no external dependencies beyond the C++ standard library.

---

## File Structure

```
mora/
├── xmake.lua                         # top-level build config
├── include/
│   └── mora/
│       ├── core/
│       │   ├── arena.h               # bump allocator for AST nodes
│       │   ├── string_pool.h         # interned string table
│       │   └── source_location.h     # file/line/col spans
│       ├── lexer/
│       │   ├── token.h               # TokenKind enum + Token struct
│       │   └── lexer.h               # Lexer class
│       ├── ast/
│       │   ├── types.h               # MoraType hierarchy
│       │   └── ast.h                 # AST node definitions
│       ├── parser/
│       │   └── parser.h              # Parser class
│       ├── sema/
│       │   ├── name_resolver.h       # namespace/import/symbol resolution
│       │   └── type_checker.h        # type inference and validation
│       ├── diag/
│       │   ├── diagnostic.h          # Diagnostic struct
│       │   └── renderer.h            # pretty-print diagnostics
│       └── cli/
│           ├── terminal.h            # ANSI color, TTY detection
│           └── progress.h            # phase progress display
├── src/
│   ├── core/
│   │   ├── arena.cpp
│   │   ├── string_pool.cpp
│   │   └── source_location.cpp
│   ├── lexer/
│   │   ├── token.cpp
│   │   └── lexer.cpp
│   ├── ast/
│   │   ├── types.cpp
│   │   └── ast.cpp
│   ├── parser/
│   │   └── parser.cpp
│   ├── sema/
│   │   ├── name_resolver.cpp
│   │   └── type_checker.cpp
│   ├── diag/
│   │   ├── diagnostic.cpp
│   │   └── renderer.cpp
│   ├── cli/
│   │   ├── terminal.cpp
│   │   └── progress.cpp
│   └── main.cpp                      # CLI entry point
└── tests/
    ├── arena_test.cpp
    ├── string_pool_test.cpp
    ├── lexer_test.cpp
    ├── parser_test.cpp
    ├── type_checker_test.cpp
    ├── name_resolver_test.cpp
    └── diagnostic_test.cpp
```

---

### Task 1: Project Scaffolding

**Files:**
- Create: `xmake.lua`
- Create: `src/main.cpp`

- [ ] **Step 1: Create xmake.lua**

```lua
set_project("mora")
set_version("0.1.0")

set_languages("c++20")
set_warnings("all", "error")

-- Static library with all compiler sources (tests and exe both link this)
target("mora_lib")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
              "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
              "src/cli/*.cpp")

-- Main executable
target("mora")
    set_kind("binary")
    add_files("src/main.cpp")
    add_deps("mora_lib")

-- Tests
add_requires("gtest")

for _, testfile in ipairs(os.files("tests/*_test.cpp")) do
    local name = path.basename(testfile)
    target(name)
        set_kind("binary")
        set_default(false)
        add_files(testfile)
        add_deps("mora_lib")
        add_packages("gtest")
        add_tests(name)
end
```

- [ ] **Step 2: Create minimal main.cpp**

```cpp
#include <cstdio>

int main(int argc, char* argv[]) {
    std::printf("mora v0.1.0\n");
    return 0;
}
```

- [ ] **Step 3: Create directory structure**

Run:
```bash
mkdir -p include/mora/{core,lexer,ast,parser,sema,diag,cli}
mkdir -p src/{core,lexer,ast,parser,sema,diag,cli}
mkdir -p tests
```

Create placeholder files so xmake globs don't fail on empty directories:

```bash
touch src/core/.gitkeep src/lexer/.gitkeep src/ast/.gitkeep
touch src/parser/.gitkeep src/sema/.gitkeep src/diag/.gitkeep src/cli/.gitkeep
```

- [ ] **Step 4: Build and verify**

Run:
```bash
xmake build mora
xmake run mora
```
Expected: prints `mora v0.1.0`

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: project scaffolding with xmake and Google Test"
```

---

### Task 2: Core Utilities — Arena Allocator

**Files:**
- Create: `include/mora/core/arena.h`
- Create: `src/core/arena.cpp`
- Create: `tests/arena_test.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/arena_test.cpp
#include <gtest/gtest.h>
#include "mora/core/arena.h"

struct TestNode {
    int value;
    TestNode* next;
};

TEST(ArenaTest, AllocateSingleObject) {
    mora::Arena arena;
    auto* node = arena.alloc<TestNode>();
    ASSERT_NE(node, nullptr);
    node->value = 42;
    EXPECT_EQ(node->value, 42);
}

TEST(ArenaTest, AllocateMultipleObjects) {
    mora::Arena arena;
    auto* a = arena.alloc<TestNode>();
    auto* b = arena.alloc<TestNode>();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
    a->value = 1;
    b->value = 2;
    EXPECT_EQ(a->value, 1);
    EXPECT_EQ(b->value, 2);
}

TEST(ArenaTest, AllocateManyObjectsAcrossChunks) {
    mora::Arena arena(64); // small chunk size to force multiple chunks
    std::vector<TestNode*> nodes;
    for (int i = 0; i < 100; i++) {
        auto* n = arena.alloc<TestNode>();
        ASSERT_NE(n, nullptr);
        n->value = i;
        nodes.push_back(n);
    }
    // verify none were corrupted
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(nodes[i]->value, i);
    }
}

TEST(ArenaTest, ResetFreesAll) {
    mora::Arena arena;
    arena.alloc<TestNode>();
    arena.alloc<TestNode>();
    size_t before = arena.bytes_allocated();
    EXPECT_GT(before, 0u);
    arena.reset();
    EXPECT_EQ(arena.bytes_allocated(), 0u);
}
```

The test is auto-discovered by the glob in `xmake.lua` — no registration needed.

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build arena_test && xmake run arena_test`
Expected: FAIL — `mora/core/arena.h` not found

- [ ] **Step 3: Implement Arena**

```cpp
// include/mora/core/arena.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

namespace mora {

class Arena {
public:
    explicit Arena(size_t chunk_size = 4096)
        : chunk_size_(chunk_size) {}

    ~Arena() { reset(); }

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&) = default;
    Arena& operator=(Arena&&) = default;

    template <typename T, typename... Args>
    T* alloc(Args&&... args) {
        void* mem = alloc_raw(sizeof(T), alignof(T));
        return new (mem) T(std::forward<Args>(args)...);
    }

    void reset();
    size_t bytes_allocated() const;

private:
    void* alloc_raw(size_t size, size_t align);

    struct Chunk {
        std::unique_ptr<std::byte[]> data;
        size_t size;
    };

    size_t chunk_size_;
    std::vector<Chunk> chunks_;
    size_t current_offset_ = 0;
    size_t total_allocated_ = 0;
};

} // namespace mora
```

```cpp
// src/core/arena.cpp
#include "mora/core/arena.h"

namespace mora {

void* Arena::alloc_raw(size_t size, size_t align) {
    if (chunks_.empty() || current_offset_ + size > chunks_.back().size) {
        size_t alloc_size = std::max(chunk_size_, size + align);
        auto& chunk = chunks_.emplace_back();
        chunk.data = std::make_unique<std::byte[]>(alloc_size);
        chunk.size = alloc_size;
        current_offset_ = 0;
    }

    auto* base = chunks_.back().data.get() + current_offset_;
    size_t space = chunks_.back().size - current_offset_;
    void* aligned = base;
    if (!std::align(align, size, aligned, space)) {
        // alignment pushed us past chunk end, allocate new chunk
        size_t alloc_size = std::max(chunk_size_, size + align);
        auto& chunk = chunks_.emplace_back();
        chunk.data = std::make_unique<std::byte[]>(alloc_size);
        chunk.size = alloc_size;
        current_offset_ = 0;
        aligned = chunk.data.get();
        space = chunk.size;
        std::align(align, size, aligned, space);
    }

    current_offset_ = static_cast<std::byte*>(aligned) - chunks_.back().data.get() + size;
    total_allocated_ += size;
    return aligned;
}

void Arena::reset() {
    chunks_.clear();
    current_offset_ = 0;
    total_allocated_ = 0;
}

size_t Arena::bytes_allocated() const {
    return total_allocated_;
}

} // namespace mora
```

The glob `src/core/*.cpp` in `xmake.lua` auto-discovers this file.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build arena_test && xmake run arena_test`
Expected: All 4 tests PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: arena allocator for AST node allocation"
```

---

### Task 3: Core Utilities — String Pool

**Files:**
- Create: `include/mora/core/string_pool.h`
- Create: `src/core/string_pool.cpp`
- Create: `tests/string_pool_test.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/string_pool_test.cpp
#include <gtest/gtest.h>
#include "mora/core/string_pool.h"

TEST(StringPoolTest, InternReturnsSamePointer) {
    mora::StringPool pool;
    auto a = pool.intern("hello");
    auto b = pool.intern("hello");
    // same content -> same interned ID
    EXPECT_EQ(a, b);
}

TEST(StringPoolTest, DifferentStringsGetDifferentIds) {
    mora::StringPool pool;
    auto a = pool.intern("hello");
    auto b = pool.intern("world");
    EXPECT_NE(a, b);
}

TEST(StringPoolTest, CanRetrieveString) {
    mora::StringPool pool;
    auto id = pool.intern("test_string");
    EXPECT_EQ(pool.get(id), "test_string");
}

TEST(StringPoolTest, EmptyString) {
    mora::StringPool pool;
    auto id = pool.intern("");
    EXPECT_EQ(pool.get(id), "");
}

TEST(StringPoolTest, ManyStrings) {
    mora::StringPool pool;
    std::vector<mora::StringId> ids;
    for (int i = 0; i < 1000; i++) {
        ids.push_back(pool.intern("str_" + std::to_string(i)));
    }
    for (int i = 0; i < 1000; i++) {
        EXPECT_EQ(pool.get(ids[i]), "str_" + std::to_string(i));
    }
    // re-interning gives same IDs
    for (int i = 0; i < 1000; i++) {
        EXPECT_EQ(pool.intern("str_" + std::to_string(i)), ids[i]);
    }
}
```

The test is auto-discovered by the glob in `xmake.lua` — no registration needed.

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build string_pool_test && xmake run string_pool_test`
Expected: FAIL — header not found

- [ ] **Step 3: Implement StringPool**

```cpp
// include/mora/core/string_pool.h
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mora {

// Opaque handle to an interned string.
// Cheap to copy, compare by value.
struct StringId {
    uint32_t index = 0;

    bool operator==(const StringId& other) const = default;
    bool operator!=(const StringId& other) const = default;
    explicit operator bool() const { return index != 0; }
};

class StringPool {
public:
    StringPool();

    StringId intern(std::string_view str);
    std::string_view get(StringId id) const;
    size_t size() const { return strings_.size(); }

private:
    std::vector<std::string> strings_;
    std::unordered_map<std::string_view, StringId> lookup_;
};

} // namespace mora
```

```cpp
// src/core/string_pool.cpp
#include "mora/core/string_pool.h"

namespace mora {

StringPool::StringPool() {
    // Index 0 is reserved as "invalid/empty"
    strings_.emplace_back("");
}

StringId StringPool::intern(std::string_view str) {
    auto it = lookup_.find(str);
    if (it != lookup_.end()) {
        return it->second;
    }
    StringId id{static_cast<uint32_t>(strings_.size())};
    strings_.emplace_back(str);
    // The string_view in the map must point to the owned string
    lookup_.emplace(std::string_view(strings_.back()), id);
    return id;
}

std::string_view StringPool::get(StringId id) const {
    return strings_[id.index];
}

} // namespace mora
```

The glob in `xmake.lua` auto-discovers this file.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build string_pool_test && xmake run string_pool_test`
Expected: All 5 tests PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: interned string pool for fast identifier comparison"
```

---

### Task 4: Core Utilities — Source Locations

**Files:**
- Create: `include/mora/core/source_location.h`
- Create: `src/core/source_location.cpp`

- [ ] **Step 1: Write the failing test (inline in arena_test or new file)**

This is a simple data struct — write a minimal test:

```cpp
// tests/source_location_test.cpp
#include <gtest/gtest.h>
#include "mora/core/source_location.h"

TEST(SourceLocationTest, SpanContainsPosition) {
    mora::SourceSpan span{"test.mora", 5, 10, 5, 25};
    EXPECT_EQ(span.file, "test.mora");
    EXPECT_EQ(span.start_line, 5u);
    EXPECT_EQ(span.start_col, 10u);
    EXPECT_EQ(span.end_line, 5u);
    EXPECT_EQ(span.end_col, 25u);
}

TEST(SourceLocationTest, MergeSpans) {
    mora::SourceSpan a{"test.mora", 5, 1, 5, 10};
    mora::SourceSpan b{"test.mora", 7, 1, 7, 20};
    auto merged = mora::merge_spans(a, b);
    EXPECT_EQ(merged.start_line, 5u);
    EXPECT_EQ(merged.start_col, 1u);
    EXPECT_EQ(merged.end_line, 7u);
    EXPECT_EQ(merged.end_col, 20u);
}
```

The test is auto-discovered by the glob in `xmake.lua` — no registration needed.

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build source_location_test && xmake run source_location_test`
Expected: FAIL

- [ ] **Step 3: Implement SourceSpan**

```cpp
// include/mora/core/source_location.h
#pragma once

#include <cstdint>
#include <string>

namespace mora {

struct SourceSpan {
    std::string file;
    uint32_t start_line = 0;
    uint32_t start_col = 0;
    uint32_t end_line = 0;
    uint32_t end_col = 0;
};

SourceSpan merge_spans(const SourceSpan& a, const SourceSpan& b);

} // namespace mora
```

```cpp
// src/core/source_location.cpp
#include "mora/core/source_location.h"
#include <algorithm>

namespace mora {

SourceSpan merge_spans(const SourceSpan& a, const SourceSpan& b) {
    SourceSpan result;
    result.file = a.file;
    if (a.start_line < b.start_line ||
        (a.start_line == b.start_line && a.start_col <= b.start_col)) {
        result.start_line = a.start_line;
        result.start_col = a.start_col;
    } else {
        result.start_line = b.start_line;
        result.start_col = b.start_col;
    }
    if (a.end_line > b.end_line ||
        (a.end_line == b.end_line && a.end_col >= b.end_col)) {
        result.end_line = a.end_line;
        result.end_col = a.end_col;
    } else {
        result.end_line = b.end_line;
        result.end_col = b.end_col;
    }
    return result;
}

} // namespace mora
```

The glob in `xmake.lua` auto-discovers this file.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build source_location_test && xmake run source_location_test`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: source location spans for diagnostic reporting"
```

---

### Task 5: Diagnostics Engine

**Files:**
- Create: `include/mora/diag/diagnostic.h`
- Create: `src/diag/diagnostic.cpp`
- Create: `include/mora/diag/renderer.h`
- Create: `src/diag/renderer.cpp`
- Create: `include/mora/cli/terminal.h`
- Create: `src/cli/terminal.cpp`
- Create: `tests/diagnostic_test.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/diagnostic_test.cpp
#include <gtest/gtest.h>
#include "mora/diag/diagnostic.h"
#include "mora/diag/renderer.h"

TEST(DiagnosticTest, CreateError) {
    mora::Diagnostic diag;
    diag.level = mora::DiagLevel::Error;
    diag.code = "E012";
    diag.message = "type mismatch";
    diag.span = {"weapons.mora", 14, 21, 14, 31};
    diag.source_line = "    has_faction(NPC, :IronSword)";

    EXPECT_EQ(diag.level, mora::DiagLevel::Error);
    EXPECT_EQ(diag.code, "E012");
}

TEST(DiagnosticTest, CreateWarning) {
    mora::Diagnostic diag;
    diag.level = mora::DiagLevel::Warning;
    diag.code = "W003";
    diag.message = "rule matches 0 records";
    EXPECT_EQ(diag.level, mora::DiagLevel::Warning);
}

TEST(DiagnosticTest, AddNotes) {
    mora::Diagnostic diag;
    diag.level = mora::DiagLevel::Error;
    diag.code = "E012";
    diag.message = "type mismatch";
    diag.span = {"weapons.mora", 14, 21, 14, 31};
    diag.source_line = "    has_faction(NPC, :IronSword)";
    diag.notes.push_back(":IronSword is Skyrim.esm|0x00012EB7 (weapon)");
    diag.notes.push_back("did you mean :BanditFaction?");

    EXPECT_EQ(diag.notes.size(), 2u);
}

TEST(DiagnosticTest, DiagBagCollectsMultiple) {
    mora::DiagBag bag;
    bag.error("E001", "first error", {"test.mora", 1, 1, 1, 5}, "line1");
    bag.error("E002", "second error", {"test.mora", 2, 1, 2, 5}, "line2");
    bag.warning("W001", "a warning", {"test.mora", 3, 1, 3, 5}, "line3");

    EXPECT_EQ(bag.error_count(), 2u);
    EXPECT_EQ(bag.warning_count(), 1u);
    EXPECT_TRUE(bag.has_errors());
    EXPECT_EQ(bag.all().size(), 3u);
}

TEST(DiagnosticRendererTest, RenderErrorPlainText) {
    mora::Diagnostic diag;
    diag.level = mora::DiagLevel::Error;
    diag.code = "E012";
    diag.message = "type mismatch";
    diag.span = {"weapons.mora", 14, 21, 14, 31};
    diag.source_line = "    has_faction(NPC, :IronSword)";
    diag.notes.push_back("expected FactionID, found WeaponID");

    mora::DiagRenderer renderer(/*use_color=*/false);
    std::string output = renderer.render(diag);

    // Should contain the error code
    EXPECT_NE(output.find("E012"), std::string::npos);
    // Should contain the file location
    EXPECT_NE(output.find("weapons.mora:14:21"), std::string::npos);
    // Should contain the source line
    EXPECT_NE(output.find("has_faction"), std::string::npos);
    // Should contain underline characters
    EXPECT_NE(output.find("──"), std::string::npos);
    // Should contain the note
    EXPECT_NE(output.find("expected FactionID"), std::string::npos);
}
```

The test is auto-discovered by the glob in `xmake.lua` — no registration needed.

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build diagnostic_test && xmake run diagnostic_test`
Expected: FAIL

- [ ] **Step 3: Implement Terminal utilities**

```cpp
// include/mora/cli/terminal.h
#pragma once

#include <string>

namespace mora {

struct TermStyle {
    static std::string red(const std::string& s, bool color);
    static std::string yellow(const std::string& s, bool color);
    static std::string cyan(const std::string& s, bool color);
    static std::string green(const std::string& s, bool color);
    static std::string bold(const std::string& s, bool color);
    static std::string dim(const std::string& s, bool color);
    static std::string reset(bool color);
};

bool stdout_is_tty();
bool color_enabled(); // checks NO_COLOR env, TTY status

} // namespace mora
```

```cpp
// src/cli/terminal.cpp
#include "mora/cli/terminal.h"
#include <cstdlib>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace mora {

static std::string wrap(const std::string& s, const char* code, bool color) {
    if (!color) return s;
    return std::string(code) + s + "\033[0m";
}

std::string TermStyle::red(const std::string& s, bool color) { return wrap(s, "\033[31m", color); }
std::string TermStyle::yellow(const std::string& s, bool color) { return wrap(s, "\033[33m", color); }
std::string TermStyle::cyan(const std::string& s, bool color) { return wrap(s, "\033[36m", color); }
std::string TermStyle::green(const std::string& s, bool color) { return wrap(s, "\033[32m", color); }
std::string TermStyle::bold(const std::string& s, bool color) { return wrap(s, "\033[1m", color); }
std::string TermStyle::dim(const std::string& s, bool color) { return wrap(s, "\033[2m", color); }
std::string TermStyle::reset(bool color) { return color ? "\033[0m" : ""; }

bool stdout_is_tty() {
    return isatty(fileno(stdout));
}

bool color_enabled() {
    if (std::getenv("NO_COLOR")) return false;
    return stdout_is_tty();
}

} // namespace mora
```

The glob in `xmake.lua` auto-discovers this file.

- [ ] **Step 4: Implement Diagnostic and DiagBag**

```cpp
// include/mora/diag/diagnostic.h
#pragma once

#include "mora/core/source_location.h"
#include <string>
#include <vector>

namespace mora {

enum class DiagLevel { Error, Warning, Note };

struct Diagnostic {
    DiagLevel level = DiagLevel::Error;
    std::string code;        // e.g. "E012", "W003"
    std::string message;     // e.g. "type mismatch"
    SourceSpan span;
    std::string source_line; // the actual line of source code
    std::vector<std::string> notes; // additional context lines
};

class DiagBag {
public:
    void error(const std::string& code, const std::string& msg,
               const SourceSpan& span, const std::string& source_line);
    void warning(const std::string& code, const std::string& msg,
                 const SourceSpan& span, const std::string& source_line);
    void add(Diagnostic diag);

    size_t error_count() const { return error_count_; }
    size_t warning_count() const { return warning_count_; }
    bool has_errors() const { return error_count_ > 0; }
    const std::vector<Diagnostic>& all() const { return diags_; }

private:
    std::vector<Diagnostic> diags_;
    size_t error_count_ = 0;
    size_t warning_count_ = 0;
};

} // namespace mora
```

```cpp
// src/diag/diagnostic.cpp
#include "mora/diag/diagnostic.h"

namespace mora {

void DiagBag::error(const std::string& code, const std::string& msg,
                    const SourceSpan& span, const std::string& source_line) {
    Diagnostic d;
    d.level = DiagLevel::Error;
    d.code = code;
    d.message = msg;
    d.span = span;
    d.source_line = source_line;
    add(std::move(d));
}

void DiagBag::warning(const std::string& code, const std::string& msg,
                      const SourceSpan& span, const std::string& source_line) {
    Diagnostic d;
    d.level = DiagLevel::Warning;
    d.code = code;
    d.message = msg;
    d.span = span;
    d.source_line = source_line;
    add(std::move(d));
}

void DiagBag::add(Diagnostic diag) {
    if (diag.level == DiagLevel::Error) error_count_++;
    if (diag.level == DiagLevel::Warning) warning_count_++;
    diags_.push_back(std::move(diag));
}

} // namespace mora
```

- [ ] **Step 5: Implement DiagRenderer**

The renderer produces output matching the spec's format:
```
  error[E012]: type mismatch
    ┌─ weapons.mora:14:21
    │
  14│     has_faction(NPC, :IronSword)
    │                      ──────────
    │                      expected FactionID, found WeaponID
    │
    = note text here
```

```cpp
// include/mora/diag/renderer.h
#pragma once

#include "mora/diag/diagnostic.h"
#include "mora/cli/terminal.h"
#include <string>

namespace mora {

class DiagRenderer {
public:
    explicit DiagRenderer(bool use_color = true) : color_(use_color) {}

    std::string render(const Diagnostic& diag) const;
    std::string render_all(const DiagBag& bag) const;

private:
    bool color_;

    std::string level_str(DiagLevel level) const;
    std::string underline(uint32_t start_col, uint32_t end_col,
                          const std::string& label) const;
};

} // namespace mora
```

```cpp
// src/diag/renderer.cpp
#include "mora/diag/renderer.h"
#include <sstream>
#include <iomanip>

namespace mora {

std::string DiagRenderer::level_str(DiagLevel level) const {
    switch (level) {
        case DiagLevel::Error:
            return TermStyle::red("error", color_);
        case DiagLevel::Warning:
            return TermStyle::yellow("warning", color_);
        case DiagLevel::Note:
            return TermStyle::dim("note", color_);
    }
    return "unknown";
}

std::string DiagRenderer::underline(uint32_t start_col, uint32_t end_col,
                                     const std::string& label) const {
    std::string result;
    // Pad to start column (accounting for line number gutter)
    for (uint32_t i = 0; i < start_col - 1; i++) {
        result += ' ';
    }
    // Draw underline
    uint32_t len = end_col > start_col ? end_col - start_col : 1;
    std::string line_chars;
    for (uint32_t i = 0; i < len; i++) {
        line_chars += "\u2500"; // ─
    }
    result += TermStyle::red(line_chars, color_);
    if (!label.empty()) {
        result += "\n";
        for (uint32_t i = 0; i < start_col - 1; i++) {
            result += ' ';
        }
        result += TermStyle::red(label, color_);
    }
    return result;
}

std::string DiagRenderer::render(const Diagnostic& diag) const {
    std::ostringstream out;
    std::string pipe = TermStyle::cyan("\u2502", color_); // │
    std::string corner = TermStyle::cyan("\u250c\u2500", color_); // ┌─

    // Header: error[E012]: type mismatch
    out << "  " << level_str(diag.level)
        << "[" << TermStyle::bold(diag.code, color_) << "]"
        << ": " << TermStyle::bold(diag.message, color_) << "\n";

    // Location: ┌─ weapons.mora:14:21
    out << "    " << corner << " "
        << TermStyle::cyan(diag.span.file, color_)
        << ":" << diag.span.start_line
        << ":" << diag.span.start_col << "\n";

    // Empty pipe line
    out << "    " << pipe << "\n";

    // Source line with line number
    std::ostringstream line_num;
    line_num << std::setw(4) << diag.span.start_line;
    out << TermStyle::cyan(line_num.str(), color_)
        << pipe << " " << diag.source_line << "\n";

    // Underline
    out << "    " << pipe << " "
        << underline(diag.span.start_col, diag.span.end_col, "") << "\n";

    // Notes
    if (!diag.notes.empty()) {
        out << "    " << pipe << "\n";
        for (const auto& note : diag.notes) {
            out << "    = " << TermStyle::dim(note, color_) << "\n";
        }
    }

    return out.str();
}

std::string DiagRenderer::render_all(const DiagBag& bag) const {
    std::ostringstream out;
    for (const auto& diag : bag.all()) {
        out << render(diag) << "\n";
    }

    // Summary line
    if (bag.has_errors()) {
        out << "  " << TermStyle::red("\u2717", color_) << " Failed with "
            << bag.error_count() << " error"
            << (bag.error_count() != 1 ? "s" : "");
    } else {
        out << "  " << TermStyle::green("\u2713", color_) << " Passed";
    }
    if (bag.warning_count() > 0) {
        out << ", " << bag.warning_count() << " warning"
            << (bag.warning_count() != 1 ? "s" : "");
    }
    out << "\n";

    return out.str();
}

} // namespace mora
```

The globs in `xmake.lua` auto-discover all new `.cpp` files.

- [ ] **Step 6: Run test to verify it passes**

Run: `xmake build diagnostic_test && xmake run diagnostic_test`
Expected: All 5 tests PASS

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: diagnostic engine with colored Rust-style error rendering"
```

---

### Task 6: Lexer — Token Types

**Files:**
- Create: `include/mora/lexer/token.h`
- Create: `src/lexer/token.cpp`

- [ ] **Step 1: Define TokenKind enum and Token struct**

```cpp
// include/mora/lexer/token.h
#pragma once

#include "mora/core/source_location.h"
#include "mora/core/string_pool.h"
#include <string>
#include <string_view>

namespace mora {

enum class TokenKind {
    // Literals
    Integer,       // 42
    Float,         // 3.14
    String,        // "hello"
    Symbol,        // :BanditFaction
    Variable,      // NPC, Weapon (uppercase start)
    Identifier,    // bandit, has_keyword (lowercase start)
    Discard,       // _

    // Keywords
    KwNamespace,   // namespace
    KwRequires,    // requires
    KwMod,         // mod
    KwUse,         // use
    KwOnly,        // only
    KwNot,         // not
    KwImportSpid,  // import_spid
    KwImportKid,   // import_kid

    // Punctuation
    Colon,         // :
    DoubleColon,   // ::
    Comma,         // ,
    Dot,           // .
    LParen,        // (
    RParen,        // )
    LBracket,      // [
    RBracket,      // ]
    Arrow,         // =>
    Pipe,          // |
    Hash,          // # (comment start, consumed by lexer)

    // Comparison operators
    Eq,            // ==
    Neq,           // !=
    Lt,            // <
    Gt,            // >
    LtEq,          // <=
    GtEq,          // >=

    // Arithmetic operators
    Plus,          // +
    Minus,         // -
    Star,          // *
    Slash,         // /

    // Whitespace/structure
    Newline,       // significant for indentation
    Indent,        // increase in indentation level
    Dedent,        // decrease in indentation level

    // Special
    Eof,
    Error,         // lexer error token
};

struct Token {
    TokenKind kind;
    SourceSpan span;
    std::string_view text; // view into source buffer

    // Parsed values (filled for relevant token kinds)
    int64_t int_value = 0;
    double float_value = 0.0;
    StringId string_id;    // for identifiers, symbols, strings
};

const char* token_kind_name(TokenKind kind);

} // namespace mora
```

```cpp
// src/lexer/token.cpp
#include "mora/lexer/token.h"

namespace mora {

const char* token_kind_name(TokenKind kind) {
    switch (kind) {
        case TokenKind::Integer: return "integer";
        case TokenKind::Float: return "float";
        case TokenKind::String: return "string";
        case TokenKind::Symbol: return "symbol";
        case TokenKind::Variable: return "variable";
        case TokenKind::Identifier: return "identifier";
        case TokenKind::Discard: return "_";
        case TokenKind::KwNamespace: return "namespace";
        case TokenKind::KwRequires: return "requires";
        case TokenKind::KwMod: return "mod";
        case TokenKind::KwUse: return "use";
        case TokenKind::KwOnly: return "only";
        case TokenKind::KwNot: return "not";
        case TokenKind::KwImportSpid: return "import_spid";
        case TokenKind::KwImportKid: return "import_kid";
        case TokenKind::Colon: return ":";
        case TokenKind::DoubleColon: return "::";
        case TokenKind::Comma: return ",";
        case TokenKind::Dot: return ".";
        case TokenKind::LParen: return "(";
        case TokenKind::RParen: return ")";
        case TokenKind::LBracket: return "[";
        case TokenKind::RBracket: return "]";
        case TokenKind::Arrow: return "=>";
        case TokenKind::Pipe: return "|";
        case TokenKind::Hash: return "#";
        case TokenKind::Eq: return "==";
        case TokenKind::Neq: return "!=";
        case TokenKind::Lt: return "<";
        case TokenKind::Gt: return ">";
        case TokenKind::LtEq: return "<=";
        case TokenKind::GtEq: return ">=";
        case TokenKind::Plus: return "+";
        case TokenKind::Minus: return "-";
        case TokenKind::Star: return "*";
        case TokenKind::Slash: return "/";
        case TokenKind::Newline: return "newline";
        case TokenKind::Indent: return "indent";
        case TokenKind::Dedent: return "dedent";
        case TokenKind::Eof: return "eof";
        case TokenKind::Error: return "error";
    }
    return "unknown";
}

} // namespace mora
```

The glob in `xmake.lua` auto-discovers this file.

- [ ] **Step 2: Build to verify it compiles**

Run: `xmake build mora_lib`
Expected: compiles without errors

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: token types for Mora lexer"
```

---

### Task 7: Lexer — Implementation

**Files:**
- Create: `include/mora/lexer/lexer.h`
- Create: `src/lexer/lexer.cpp`
- Create: `tests/lexer_test.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/lexer_test.cpp
#include <gtest/gtest.h>
#include "mora/lexer/lexer.h"

class LexerTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    std::vector<mora::Token> lex(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        std::vector<mora::Token> tokens;
        while (true) {
            auto tok = lexer.next();
            tokens.push_back(tok);
            if (tok.kind == mora::TokenKind::Eof) break;
        }
        return tokens;
    }

    // Helper: collect non-whitespace token kinds
    std::vector<mora::TokenKind> kinds(const std::string& source) {
        auto tokens = lex(source);
        std::vector<mora::TokenKind> result;
        for (auto& t : tokens) {
            if (t.kind != mora::TokenKind::Newline &&
                t.kind != mora::TokenKind::Indent &&
                t.kind != mora::TokenKind::Dedent) {
                result.push_back(t.kind);
            }
        }
        return result;
    }
};

TEST_F(LexerTest, EmptyInput) {
    auto tokens = lex("");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Eof);
}

TEST_F(LexerTest, SimpleIdentifier) {
    auto tokens = lex("bandit");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Identifier);
    EXPECT_EQ(tokens[0].text, "bandit");
}

TEST_F(LexerTest, Variable) {
    auto tokens = lex("NPC");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Variable);
    EXPECT_EQ(tokens[0].text, "NPC");
}

TEST_F(LexerTest, Symbol) {
    auto tokens = lex(":BanditFaction");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Symbol);
    EXPECT_EQ(tokens[0].text, ":BanditFaction");
}

TEST_F(LexerTest, Keywords) {
    EXPECT_EQ(lex("namespace")[0].kind, mora::TokenKind::KwNamespace);
    EXPECT_EQ(lex("requires")[0].kind, mora::TokenKind::KwRequires);
    EXPECT_EQ(lex("use")[0].kind, mora::TokenKind::KwUse);
    EXPECT_EQ(lex("not")[0].kind, mora::TokenKind::KwNot);
    EXPECT_EQ(lex("only")[0].kind, mora::TokenKind::KwOnly);
    EXPECT_EQ(lex("mod")[0].kind, mora::TokenKind::KwMod);
}

TEST_F(LexerTest, Integer) {
    auto tokens = lex("42");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Integer);
    EXPECT_EQ(tokens[0].int_value, 42);
}

TEST_F(LexerTest, HexInteger) {
    auto tokens = lex("0x013BB9");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Integer);
    EXPECT_EQ(tokens[0].int_value, 0x013BB9);
}

TEST_F(LexerTest, Float) {
    auto tokens = lex("3.14");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Float);
    EXPECT_DOUBLE_EQ(tokens[0].float_value, 3.14);
}

TEST_F(LexerTest, StringLiteral) {
    auto tokens = lex("\"hello world\"");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::String);
    EXPECT_EQ(tokens[0].text, "\"hello world\"");
}

TEST_F(LexerTest, Operators) {
    auto k = kinds("== != < > <= >= + - * /");
    ASSERT_EQ(k.size(), 11u); // 10 operators + Eof
    EXPECT_EQ(k[0], mora::TokenKind::Eq);
    EXPECT_EQ(k[1], mora::TokenKind::Neq);
    EXPECT_EQ(k[2], mora::TokenKind::Lt);
    EXPECT_EQ(k[3], mora::TokenKind::Gt);
    EXPECT_EQ(k[4], mora::TokenKind::LtEq);
    EXPECT_EQ(k[5], mora::TokenKind::GtEq);
    EXPECT_EQ(k[6], mora::TokenKind::Plus);
    EXPECT_EQ(k[7], mora::TokenKind::Minus);
    EXPECT_EQ(k[8], mora::TokenKind::Star);
    EXPECT_EQ(k[9], mora::TokenKind::Slash);
}

TEST_F(LexerTest, Arrow) {
    auto tokens = lex("=>");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Arrow);
}

TEST_F(LexerTest, Punctuation) {
    auto k = kinds("( ) [ ] , .");
    EXPECT_EQ(k[0], mora::TokenKind::LParen);
    EXPECT_EQ(k[1], mora::TokenKind::RParen);
    EXPECT_EQ(k[2], mora::TokenKind::LBracket);
    EXPECT_EQ(k[3], mora::TokenKind::RBracket);
    EXPECT_EQ(k[4], mora::TokenKind::Comma);
    EXPECT_EQ(k[5], mora::TokenKind::Dot);
}

TEST_F(LexerTest, CommentSkipped) {
    auto tokens = lex("bandit # this is a comment");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Identifier);
    EXPECT_EQ(tokens[0].text, "bandit");
    // next meaningful token should be newline or eof
}

TEST_F(LexerTest, Discard) {
    auto tokens = lex("_");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Discard);
}

TEST_F(LexerTest, IndentDedent) {
    // A rule body is indented
    auto tokens = lex("rule:\n    body\n");
    // Expect: Identifier(rule) Colon Newline Indent Identifier(body) Newline Dedent Eof
    std::vector<mora::TokenKind> expected = {
        mora::TokenKind::Identifier,
        mora::TokenKind::Colon,
        mora::TokenKind::Newline,
        mora::TokenKind::Indent,
        mora::TokenKind::Identifier,
        mora::TokenKind::Newline,
        mora::TokenKind::Dedent,
        mora::TokenKind::Eof,
    };
    ASSERT_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(tokens[i].kind, expected[i])
            << "token " << i << ": expected " << mora::token_kind_name(expected[i])
            << ", got " << mora::token_kind_name(tokens[i].kind);
    }
}

TEST_F(LexerTest, FullRuleLex) {
    std::string source =
        "vampire_bane(Weapon):\n"
        "    weapon(Weapon)\n"
        "    has_keyword(Weapon, :WeapMaterialSilver)\n"
        "    => add_keyword(Weapon, :VampireBane)\n";

    auto tokens = lex(source);
    // Just verify it lexes without errors and starts correctly
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Identifier);
    EXPECT_EQ(tokens[0].text, "vampire_bane");
    EXPECT_EQ(tokens[1].kind, mora::TokenKind::LParen);
    EXPECT_EQ(tokens[2].kind, mora::TokenKind::Variable);
    EXPECT_EQ(tokens[2].text, "Weapon");
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(LexerTest, ErrorOnUnexpectedCharacter) {
    auto tokens = lex("@");
    EXPECT_TRUE(diags.has_errors());
}

TEST_F(LexerTest, SymbolWithPlugin) {
    // :Skyrim.esm|0x013BB9 lexes as Symbol + Pipe + Integer
    // Actually, this should be handled at the parser level.
    // The lexer sees :Skyrim as a symbol, then .esm as dot+identifier, etc.
    auto tokens = lex(":Skyrim");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Symbol);
    EXPECT_EQ(tokens[0].text, ":Skyrim");
}
```

The test is auto-discovered by the glob in `xmake.lua` — no registration needed.

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build lexer_test && xmake run lexer_test`
Expected: FAIL — `mora/lexer/lexer.h` not found

- [ ] **Step 3: Implement Lexer**

```cpp
// include/mora/lexer/lexer.h
#pragma once

#include "mora/lexer/token.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <string>
#include <string_view>
#include <vector>

namespace mora {

class Lexer {
public:
    Lexer(const std::string& source, const std::string& filename,
          StringPool& pool, DiagBag& diags);

    Token next();

    // Get a specific line of source (1-indexed) for error display
    std::string_view get_line(uint32_t line) const;

private:
    char peek() const;
    char peek_next() const;
    char advance();
    bool match(char expected);
    bool at_end() const;

    void skip_whitespace_on_line();
    void skip_comment();

    Token make_token(TokenKind kind);
    Token make_token(TokenKind kind, int64_t int_val);
    Token make_token(TokenKind kind, double float_val);
    Token error_token(const std::string& msg);

    Token lex_identifier_or_keyword();
    Token lex_number();
    Token lex_string();
    Token lex_symbol();
    Token handle_newline();

    std::string source_;
    std::string filename_;
    StringPool& pool_;
    DiagBag& diags_;

    size_t pos_ = 0;
    size_t token_start_ = 0;
    uint32_t line_ = 1;
    uint32_t col_ = 1;
    uint32_t token_start_line_ = 1;
    uint32_t token_start_col_ = 1;

    // Indentation tracking
    std::vector<int> indent_stack_ = {0};
    int pending_dedents_ = 0;
    bool at_line_start_ = true;

    // Cache line start positions for get_line()
    std::vector<size_t> line_starts_;
    void index_lines();
};

} // namespace mora
```

```cpp
// src/lexer/lexer.cpp
#include "mora/lexer/lexer.h"
#include <algorithm>
#include <cctype>
#include <charconv>

namespace mora {

Lexer::Lexer(const std::string& source, const std::string& filename,
             StringPool& pool, DiagBag& diags)
    : source_(source), filename_(filename), pool_(pool), diags_(diags) {
    index_lines();
}

void Lexer::index_lines() {
    line_starts_.push_back(0);
    for (size_t i = 0; i < source_.size(); i++) {
        if (source_[i] == '\n') {
            line_starts_.push_back(i + 1);
        }
    }
}

std::string_view Lexer::get_line(uint32_t line) const {
    if (line == 0 || line > line_starts_.size()) return "";
    size_t start = line_starts_[line - 1];
    size_t end = (line < line_starts_.size())
                     ? line_starts_[line] - 1
                     : source_.size();
    return std::string_view(source_).substr(start, end - start);
}

char Lexer::peek() const {
    if (at_end()) return '\0';
    return source_[pos_];
}

char Lexer::peek_next() const {
    if (pos_ + 1 >= source_.size()) return '\0';
    return source_[pos_ + 1];
}

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (at_end() || source_[pos_] != expected) return false;
    advance();
    return true;
}

bool Lexer::at_end() const {
    return pos_ >= source_.size();
}

void Lexer::skip_whitespace_on_line() {
    while (!at_end() && (peek() == ' ' || peek() == '\t') ) {
        advance();
    }
}

void Lexer::skip_comment() {
    while (!at_end() && peek() != '\n') {
        advance();
    }
}

Token Lexer::make_token(TokenKind kind) {
    Token tok;
    tok.kind = kind;
    tok.span = {filename_, token_start_line_, token_start_col_, line_, col_};
    tok.text = std::string_view(source_).substr(token_start_, pos_ - token_start_);
    tok.string_id = pool_.intern(tok.text);
    return tok;
}

Token Lexer::make_token(TokenKind kind, int64_t int_val) {
    auto tok = make_token(kind);
    tok.int_value = int_val;
    return tok;
}

Token Lexer::make_token(TokenKind kind, double float_val) {
    auto tok = make_token(kind);
    tok.float_value = float_val;
    return tok;
}

Token Lexer::error_token(const std::string& msg) {
    SourceSpan span{filename_, token_start_line_, token_start_col_, line_, col_};
    std::string src_line(get_line(token_start_line_));
    diags_.error("E001", msg, span, src_line);

    Token tok;
    tok.kind = TokenKind::Error;
    tok.span = span;
    tok.text = std::string_view(source_).substr(token_start_, pos_ - token_start_);
    return tok;
}

static bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

static bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static bool is_upper(char c) {
    return c >= 'A' && c <= 'Z';
}

Token Lexer::lex_identifier_or_keyword() {
    while (!at_end() && is_ident_char(peek())) {
        advance();
    }

    std::string_view text = std::string_view(source_).substr(
        token_start_, pos_ - token_start_);

    // Check for keywords
    if (text == "namespace") return make_token(TokenKind::KwNamespace);
    if (text == "requires") return make_token(TokenKind::KwRequires);
    if (text == "mod") return make_token(TokenKind::KwMod);
    if (text == "use") return make_token(TokenKind::KwUse);
    if (text == "only") return make_token(TokenKind::KwOnly);
    if (text == "not") return make_token(TokenKind::KwNot);
    if (text == "import_spid") return make_token(TokenKind::KwImportSpid);
    if (text == "import_kid") return make_token(TokenKind::KwImportKid);

    // _ alone is discard
    if (text == "_") return make_token(TokenKind::Discard);

    // Uppercase start = Variable, lowercase = Identifier
    if (is_upper(text[0])) return make_token(TokenKind::Variable);
    return make_token(TokenKind::Identifier);
}

Token Lexer::lex_number() {
    // Check for hex
    if (peek() == '0' && (peek_next() == 'x' || peek_next() == 'X')) {
        advance(); // 0
        advance(); // x
        while (!at_end() && std::isxdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
        std::string_view text = std::string_view(source_).substr(
            token_start_, pos_ - token_start_);
        int64_t val = 0;
        auto [ptr, ec] = std::from_chars(text.data() + 2, text.data() + text.size(), val, 16);
        if (ec != std::errc()) return error_token("invalid hex literal");
        return make_token(TokenKind::Integer, val);
    }

    while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }

    // Check for float
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_next()))) {
        advance(); // .
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
        std::string_view text = std::string_view(source_).substr(
            token_start_, pos_ - token_start_);
        double val = 0;
        auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), val);
        if (ec != std::errc()) return error_token("invalid float literal");
        return make_token(TokenKind::Float, val);
    }

    std::string_view text = std::string_view(source_).substr(
        token_start_, pos_ - token_start_);
    int64_t val = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), val);
    if (ec != std::errc()) return error_token("invalid integer literal");
    return make_token(TokenKind::Integer, val);
}

Token Lexer::lex_string() {
    advance(); // opening "
    while (!at_end() && peek() != '"' && peek() != '\n') {
        if (peek() == '\\') advance(); // skip escape
        advance();
    }
    if (at_end() || peek() == '\n') {
        return error_token("unterminated string literal");
    }
    advance(); // closing "
    return make_token(TokenKind::String);
}

Token Lexer::lex_symbol() {
    advance(); // :
    if (!is_ident_start(peek())) {
        return make_token(TokenKind::Colon);
    }
    while (!at_end() && is_ident_char(peek())) {
        advance();
    }
    return make_token(TokenKind::Symbol);
}

Token Lexer::handle_newline() {
    auto tok = make_token(TokenKind::Newline);
    at_line_start_ = true;
    return tok;
}

Token Lexer::next() {
    // Emit pending dedents
    if (pending_dedents_ > 0) {
        pending_dedents_--;
        token_start_ = pos_;
        token_start_line_ = line_;
        token_start_col_ = col_;
        return make_token(TokenKind::Dedent);
    }

    // Handle indentation at start of line
    if (at_line_start_) {
        at_line_start_ = false;

        // Count leading spaces
        int indent = 0;
        size_t indent_pos = pos_;
        while (indent_pos < source_.size() &&
               (source_[indent_pos] == ' ' || source_[indent_pos] == '\t')) {
            if (source_[indent_pos] == '\t') indent += 4;
            else indent++;
            indent_pos++;
        }

        // Skip blank lines and comment-only lines
        if (indent_pos < source_.size() &&
            (source_[indent_pos] == '\n' || source_[indent_pos] == '#')) {
            // Don't change indentation for blank/comment lines
        } else {
            int current = indent_stack_.back();
            if (indent > current) {
                indent_stack_.push_back(indent);
                // Consume the whitespace
                while (pos_ < indent_pos) advance();
                token_start_ = pos_;
                token_start_line_ = line_;
                token_start_col_ = col_;
                return make_token(TokenKind::Indent);
            } else if (indent < current) {
                // May need multiple dedents
                while (indent_stack_.back() > indent) {
                    indent_stack_.pop_back();
                    pending_dedents_++;
                }
                pending_dedents_--; // emit one now
                // Consume the whitespace
                while (pos_ < indent_pos) advance();
                token_start_ = pos_;
                token_start_line_ = line_;
                token_start_col_ = col_;
                return make_token(TokenKind::Dedent);
            }
        }
    }

    skip_whitespace_on_line();

    if (at_end()) {
        // Emit remaining dedents
        if (indent_stack_.size() > 1) {
            indent_stack_.pop_back();
            token_start_ = pos_;
            token_start_line_ = line_;
            token_start_col_ = col_;
            return make_token(TokenKind::Dedent);
        }
        token_start_ = pos_;
        token_start_line_ = line_;
        token_start_col_ = col_;
        return make_token(TokenKind::Eof);
    }

    token_start_ = pos_;
    token_start_line_ = line_;
    token_start_col_ = col_;

    char c = advance();

    // Comment
    if (c == '#') {
        skip_comment();
        if (!at_end() && peek() == '\n') {
            token_start_ = pos_;
            token_start_line_ = line_;
            token_start_col_ = col_;
            advance();
            return handle_newline();
        }
        return next(); // recurse to get next real token
    }

    // Newline
    if (c == '\n') return handle_newline();

    // Identifiers and keywords
    if (is_ident_start(c)) return lex_identifier_or_keyword();

    // Numbers
    if (std::isdigit(static_cast<unsigned char>(c))) return lex_number();

    // String literals
    if (c == '"') { pos_--; col_--; return lex_string(); }

    // Symbol or bare colon
    if (c == ':') {
        if (peek() == ':') { advance(); return make_token(TokenKind::DoubleColon); }
        if (is_ident_start(peek())) { pos_--; col_--; return lex_symbol(); }
        return make_token(TokenKind::Colon);
    }

    // Two-character operators
    if (c == '=' && peek() == '>') { advance(); return make_token(TokenKind::Arrow); }
    if (c == '=' && peek() == '=') { advance(); return make_token(TokenKind::Eq); }
    if (c == '!' && peek() == '=') { advance(); return make_token(TokenKind::Neq); }
    if (c == '<' && peek() == '=') { advance(); return make_token(TokenKind::LtEq); }
    if (c == '>' && peek() == '=') { advance(); return make_token(TokenKind::GtEq); }

    // Single-character tokens
    switch (c) {
        case '(': return make_token(TokenKind::LParen);
        case ')': return make_token(TokenKind::RParen);
        case '[': return make_token(TokenKind::LBracket);
        case ']': return make_token(TokenKind::RBracket);
        case ',': return make_token(TokenKind::Comma);
        case '.': return make_token(TokenKind::Dot);
        case '|': return make_token(TokenKind::Pipe);
        case '<': return make_token(TokenKind::Lt);
        case '>': return make_token(TokenKind::Gt);
        case '+': return make_token(TokenKind::Plus);
        case '-': return make_token(TokenKind::Minus);
        case '*': return make_token(TokenKind::Star);
        case '/': return make_token(TokenKind::Slash);
    }

    return error_token(std::string("unexpected character '") + c + "'");
}

} // namespace mora
```

The glob in `xmake.lua` auto-discovers this file.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build lexer_test && xmake run lexer_test`
Expected: All tests PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: Mora lexer with indentation tracking and full token set"
```

---

### Task 8: AST Node Definitions

**Files:**
- Create: `include/mora/ast/types.h`
- Create: `src/ast/types.cpp`
- Create: `include/mora/ast/ast.h`

- [ ] **Step 1: Define the Mora type system**

```cpp
// include/mora/ast/types.h
#pragma once

#include <string>
#include <memory>
#include <optional>

namespace mora {

enum class TypeKind {
    Int,
    Float,
    String,
    Bool,
    FormID,     // base FormID type
    NpcID,
    WeaponID,
    ArmorID,
    KeywordID,
    FactionID,
    SpellID,
    PerkID,
    QuestID,
    LocationID,
    CellID,
    RaceID,
    List,       // List<T>
    Unknown,    // not yet inferred
    Error,      // type error sentinel
};

struct MoraType {
    TypeKind kind;
    std::optional<TypeKind> element_type; // for List<T>

    bool operator==(const MoraType& other) const = default;

    static MoraType make(TypeKind k) { return {k, std::nullopt}; }
    static MoraType make_list(TypeKind elem) { return {TypeKind::List, elem}; }

    bool is_formid() const;
    bool is_numeric() const;
    bool is_subtype_of(const MoraType& parent) const;
    std::string to_string() const;
};

} // namespace mora
```

```cpp
// src/ast/types.cpp
#include "mora/ast/types.h"

namespace mora {

bool MoraType::is_formid() const {
    switch (kind) {
        case TypeKind::FormID:
        case TypeKind::NpcID:
        case TypeKind::WeaponID:
        case TypeKind::ArmorID:
        case TypeKind::KeywordID:
        case TypeKind::FactionID:
        case TypeKind::SpellID:
        case TypeKind::PerkID:
        case TypeKind::QuestID:
        case TypeKind::LocationID:
        case TypeKind::CellID:
        case TypeKind::RaceID:
            return true;
        default:
            return false;
    }
}

bool MoraType::is_numeric() const {
    return kind == TypeKind::Int || kind == TypeKind::Float;
}

bool MoraType::is_subtype_of(const MoraType& parent) const {
    if (*this == parent) return true;
    // All specific FormID subtypes are subtypes of FormID
    if (parent.kind == TypeKind::FormID && is_formid()) return true;
    return false;
}

std::string MoraType::to_string() const {
    switch (kind) {
        case TypeKind::Int: return "Int";
        case TypeKind::Float: return "Float";
        case TypeKind::String: return "String";
        case TypeKind::Bool: return "Bool";
        case TypeKind::FormID: return "FormID";
        case TypeKind::NpcID: return "NpcID";
        case TypeKind::WeaponID: return "WeaponID";
        case TypeKind::ArmorID: return "ArmorID";
        case TypeKind::KeywordID: return "KeywordID";
        case TypeKind::FactionID: return "FactionID";
        case TypeKind::SpellID: return "SpellID";
        case TypeKind::PerkID: return "PerkID";
        case TypeKind::QuestID: return "QuestID";
        case TypeKind::LocationID: return "LocationID";
        case TypeKind::CellID: return "CellID";
        case TypeKind::RaceID: return "RaceID";
        case TypeKind::List:
            if (element_type) {
                MoraType inner{*element_type, std::nullopt};
                return "List<" + inner.to_string() + ">";
            }
            return "List<unknown>";
        case TypeKind::Unknown: return "unknown";
        case TypeKind::Error: return "<error>";
    }
    return "<invalid>";
}

} // namespace mora
```

The glob in `xmake.lua` auto-discovers this file.

- [ ] **Step 2: Define AST nodes**

```cpp
// include/mora/ast/ast.h
#pragma once

#include "mora/ast/types.h"
#include "mora/core/source_location.h"
#include "mora/core/string_pool.h"
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace mora {

// Forward declarations
struct Expr;
struct Clause;
struct Rule;
struct Directive;

// ── Expressions ──────────────────────────────────────────

struct VariableExpr {
    StringId name;
    MoraType resolved_type = MoraType::make(TypeKind::Unknown);
    SourceSpan span;
};

struct SymbolExpr {
    StringId name;     // e.g. "BanditFaction" (without the colon prefix)
    MoraType resolved_type = MoraType::make(TypeKind::Unknown);
    SourceSpan span;
};

struct IntLiteral {
    int64_t value;
    SourceSpan span;
};

struct FloatLiteral {
    double value;
    SourceSpan span;
};

struct StringLiteral {
    StringId value;
    SourceSpan span;
};

struct DiscardExpr {
    SourceSpan span;
};

struct BinaryExpr {
    enum class Op { Add, Sub, Mul, Div, Eq, Neq, Lt, Gt, LtEq, GtEq };
    Op op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    SourceSpan span;
};

struct Expr {
    std::variant<
        VariableExpr,
        SymbolExpr,
        IntLiteral,
        FloatLiteral,
        StringLiteral,
        DiscardExpr,
        BinaryExpr
    > data;
    SourceSpan span;
};

// ── Clauses (rule body) ─────────────────────────────────

// A fact pattern: identifier(arg1, arg2, ...)
struct FactPattern {
    StringId name;           // e.g. "npc", "has_keyword"
    StringId qualifier;      // optional namespace qualifier, e.g. "skyrim.record"
    std::vector<Expr> args;
    bool negated = false;    // "not has_keyword(...)"
    SourceSpan span;
};

// A guard: Level >= 20
struct GuardClause {
    std::unique_ptr<Expr> expr;
    SourceSpan span;
};

// An effect (after =>): add_keyword(Weapon, :VampireBane)
struct Effect {
    StringId action;         // e.g. "add_keyword", "add_item", "set_field"
    std::vector<Expr> args;
    SourceSpan span;
};

// A conditional effect: Level >= 20 => add_item(NPC, :SilverSword)
struct ConditionalEffect {
    std::unique_ptr<Expr> guard;
    Effect effect;
    SourceSpan span;
};

// A clause is one line of a rule body
struct Clause {
    std::variant<FactPattern, GuardClause, Effect, ConditionalEffect> data;
    SourceSpan span;
};

// ── Top-level declarations ──────────────────────────────

struct Rule {
    StringId name;
    std::vector<Expr> head_args;     // rule(NPC, Weapon) -> [NPC, Weapon]
    std::vector<Clause> body;        // body clauses (facts, guards)
    std::vector<Effect> effects;     // unconditional effects (after bare =>)
    std::vector<ConditionalEffect> conditional_effects;
    SourceSpan span;
};

struct NamespaceDecl {
    StringId name;      // dot-separated: "my_mod.patches"
    SourceSpan span;
};

struct RequiresDecl {
    StringId mod_name;  // e.g. "Skyrim.esm"
    SourceSpan span;
};

struct UseDecl {
    StringId namespace_path;
    std::vector<StringId> only; // empty = import all
    SourceSpan span;
};

struct ImportIniDecl {
    enum class Kind { Spid, Kid };
    Kind kind;
    StringId path;
    SourceSpan span;
};

struct FactDecl {
    StringId name;
    std::vector<MoraType> param_types;
    SourceSpan span;
};

// ── Module (one .mora file) ─────────────────────────────

struct Module {
    std::string filename;
    std::optional<NamespaceDecl> ns;
    std::vector<RequiresDecl> requires_decls;
    std::vector<UseDecl> use_decls;
    std::vector<ImportIniDecl> import_decls;
    std::vector<FactDecl> fact_decls;
    std::vector<Rule> rules;
};

} // namespace mora
```

- [ ] **Step 3: Build to verify it compiles**

Run: `xmake build mora_lib`
Expected: compiles without errors

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: AST node definitions and Mora type system"
```

---

### Task 9: Parser

**Files:**
- Create: `include/mora/parser/parser.h`
- Create: `src/parser/parser.cpp`
- Create: `tests/parser_test.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/parser_test.cpp
#include <gtest/gtest.h>
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"

class ParserTest : public ::testing::Test {
protected:
    mora::Arena arena;
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::Module parse(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        return parser.parse_module();
    }
};

TEST_F(ParserTest, EmptyFile) {
    auto mod = parse("");
    EXPECT_TRUE(mod.rules.empty());
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(ParserTest, NamespaceDecl) {
    auto mod = parse("namespace my_mod.patches\n");
    ASSERT_TRUE(mod.ns.has_value());
    EXPECT_EQ(pool.get(mod.ns->name), "my_mod.patches");
}

TEST_F(ParserTest, RequiresDecl) {
    auto mod = parse("requires mod(\"Skyrim.esm\")\n");
    ASSERT_EQ(mod.requires_decls.size(), 1u);
    EXPECT_EQ(pool.get(mod.requires_decls[0].mod_name), "Skyrim.esm");
}

TEST_F(ParserTest, UseDecl) {
    auto mod = parse("use skyrim.record\n");
    ASSERT_EQ(mod.use_decls.size(), 1u);
    EXPECT_EQ(pool.get(mod.use_decls[0].namespace_path), "skyrim.record");
    EXPECT_TRUE(mod.use_decls[0].only.empty());
}

TEST_F(ParserTest, UseDeclWithOnly) {
    auto mod = parse("use requiem.combat only [is_lethal, damage_mult]\n");
    ASSERT_EQ(mod.use_decls.size(), 1u);
    ASSERT_EQ(mod.use_decls[0].only.size(), 2u);
    EXPECT_EQ(pool.get(mod.use_decls[0].only[0]), "is_lethal");
    EXPECT_EQ(pool.get(mod.use_decls[0].only[1]), "damage_mult");
}

TEST_F(ParserTest, SimpleRule) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
    );
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(pool.get(mod.rules[0].name), "bandit");
    ASSERT_EQ(mod.rules[0].head_args.size(), 1u);
    // Body should have 2 fact patterns
    EXPECT_EQ(mod.rules[0].body.size(), 2u);
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(ParserTest, RuleWithEffect) {
    auto mod = parse(
        "vampire_bane(Weapon):\n"
        "    weapon(Weapon)\n"
        "    has_keyword(Weapon, :WeapMaterialSilver)\n"
        "    => add_keyword(Weapon, :VampireBane)\n"
    );
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(mod.rules[0].body.size(), 2u);
    EXPECT_EQ(mod.rules[0].effects.size(), 1u);
    EXPECT_EQ(pool.get(mod.rules[0].effects[0].action), "add_keyword");
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(ParserTest, RuleWithNegation) {
    auto mod = parse(
        "test(W):\n"
        "    weapon(W)\n"
        "    not has_keyword(W, :Foo)\n"
    );
    ASSERT_EQ(mod.rules.size(), 1u);
    ASSERT_EQ(mod.rules[0].body.size(), 2u);
    // The second clause should be a negated fact pattern
    auto& clause = mod.rules[0].body[1];
    auto* fact = std::get_if<mora::FactPattern>(&clause.data);
    ASSERT_NE(fact, nullptr);
    EXPECT_TRUE(fact->negated);
}

TEST_F(ParserTest, RuleWithConditionalEffects) {
    auto mod = parse(
        "bandit_weapons(NPC):\n"
        "    npc(NPC)\n"
        "    level(NPC, Level)\n"
        "    Level >= 20 => add_item(NPC, :SilverSword)\n"
        "    Level < 20 => add_item(NPC, :IronSword)\n"
    );
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(mod.rules[0].body.size(), 2u); // npc and level
    EXPECT_EQ(mod.rules[0].conditional_effects.size(), 2u);
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(ParserTest, MultipleRules) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "\n"
        "high_level(NPC):\n"
        "    base_level(NPC, Level)\n"
        "    Level >= 30\n"
    );
    ASSERT_EQ(mod.rules.size(), 2u);
    EXPECT_EQ(pool.get(mod.rules[0].name), "bandit");
    EXPECT_EQ(pool.get(mod.rules[1].name), "high_level");
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(ParserTest, FullFile) {
    auto mod = parse(
        "namespace my_mod.patches\n"
        "\n"
        "requires mod(\"Skyrim.esm\")\n"
        "\n"
        "use skyrim.record\n"
        "\n"
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "\n"
        "vampire_bane(Weapon):\n"
        "    weapon(Weapon)\n"
        "    has_keyword(Weapon, :WeapMaterialSilver)\n"
        "    not has_keyword(Weapon, :WeapTypeGreatsword)\n"
        "    => add_keyword(Weapon, :VampireBane)\n"
    );
    ASSERT_TRUE(mod.ns.has_value());
    EXPECT_EQ(mod.requires_decls.size(), 1u);
    EXPECT_EQ(mod.use_decls.size(), 1u);
    EXPECT_EQ(mod.rules.size(), 2u);
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(ParserTest, ErrorRecovery) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC\n"           // missing closing paren
        "\n"
        "other(W):\n"
        "    weapon(W)\n"
    );
    // Should still parse the second rule even after the error
    EXPECT_TRUE(diags.has_errors());
    EXPECT_GE(mod.rules.size(), 1u); // at minimum parsed one rule
}
```

The test is auto-discovered by the glob in `xmake.lua` — no registration needed.

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build parser_test && xmake run parser_test`
Expected: FAIL

- [ ] **Step 3: Implement Parser**

```cpp
// include/mora/parser/parser.h
#pragma once

#include "mora/ast/ast.h"
#include "mora/lexer/lexer.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

namespace mora {

class Parser {
public:
    Parser(Lexer& lexer, StringPool& pool, DiagBag& diags);

    Module parse_module();

private:
    // Token management
    Token peek();
    Token advance();
    Token expect(TokenKind kind, const std::string& msg);
    bool check(TokenKind kind);
    bool match(TokenKind kind);
    void skip_newlines();
    void synchronize(); // error recovery: skip to next rule/directive

    // Parsing declarations
    NamespaceDecl parse_namespace();
    RequiresDecl parse_requires();
    UseDecl parse_use();
    ImportIniDecl parse_import_ini(ImportIniDecl::Kind kind);

    // Parsing rules
    Rule parse_rule();
    Clause parse_clause();
    FactPattern parse_fact_pattern(bool negated = false);
    Effect parse_effect();
    ConditionalEffect parse_conditional_effect(std::unique_ptr<Expr> guard);

    // Parsing expressions
    Expr parse_expr();
    Expr parse_comparison();
    Expr parse_additive();
    Expr parse_primary();

    // Helpers
    StringId parse_dotted_name();
    std::vector<Expr> parse_arg_list();

    Lexer& lexer_;
    StringPool& pool_;
    DiagBag& diags_;
    Token current_;
    bool has_current_ = false;
};

} // namespace mora
```

```cpp
// src/parser/parser.cpp
#include "mora/parser/parser.h"
#include <cassert>

namespace mora {

Parser::Parser(Lexer& lexer, StringPool& pool, DiagBag& diags)
    : lexer_(lexer), pool_(pool), diags_(diags) {}

Token Parser::peek() {
    if (!has_current_) {
        current_ = lexer_.next();
        has_current_ = true;
    }
    return current_;
}

Token Parser::advance() {
    Token tok = peek();
    has_current_ = false;
    return tok;
}

Token Parser::expect(TokenKind kind, const std::string& msg) {
    Token tok = peek();
    if (tok.kind == kind) {
        return advance();
    }
    std::string err = msg + ", got " + token_kind_name(tok.kind);
    if (!tok.text.empty()) err += " '" + std::string(tok.text) + "'";
    diags_.error("E002", err, tok.span, std::string(lexer_.get_line(tok.span.start_line)));
    return tok; // return the wrong token; caller decides how to recover
}

bool Parser::check(TokenKind kind) {
    return peek().kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

void Parser::skip_newlines() {
    while (check(TokenKind::Newline)) advance();
}

void Parser::synchronize() {
    // Skip tokens until we hit a newline at indentation level 0 (a new top-level declaration)
    while (!check(TokenKind::Eof)) {
        if (check(TokenKind::Dedent)) { advance(); continue; }
        if (check(TokenKind::Newline)) { advance(); continue; }
        // If we're at the top level (no indent), we've synchronized
        auto tok = peek();
        if (tok.kind == TokenKind::Identifier ||
            tok.kind == TokenKind::KwNamespace ||
            tok.kind == TokenKind::KwRequires ||
            tok.kind == TokenKind::KwUse ||
            tok.kind == TokenKind::KwImportSpid ||
            tok.kind == TokenKind::KwImportKid) {
            return;
        }
        advance();
    }
}

Module Parser::parse_module() {
    Module mod;
    mod.filename = "test.mora"; // will be set by caller

    skip_newlines();

    while (!check(TokenKind::Eof)) {
        auto tok = peek();
        switch (tok.kind) {
            case TokenKind::KwNamespace:
                mod.ns = parse_namespace();
                break;
            case TokenKind::KwRequires:
                mod.requires_decls.push_back(parse_requires());
                break;
            case TokenKind::KwUse:
                mod.use_decls.push_back(parse_use());
                break;
            case TokenKind::KwImportSpid:
                mod.import_decls.push_back(parse_import_ini(ImportIniDecl::Kind::Spid));
                break;
            case TokenKind::KwImportKid:
                mod.import_decls.push_back(parse_import_ini(ImportIniDecl::Kind::Kid));
                break;
            case TokenKind::Identifier:
                mod.rules.push_back(parse_rule());
                break;
            case TokenKind::Newline:
                advance();
                break;
            default:
                diags_.error("E003", "unexpected token at top level",
                            tok.span, std::string(lexer_.get_line(tok.span.start_line)));
                advance();
                break;
        }
    }

    return mod;
}

NamespaceDecl Parser::parse_namespace() {
    NamespaceDecl decl;
    auto kw = advance(); // consume 'namespace'
    decl.name = parse_dotted_name();
    decl.span = kw.span;
    skip_newlines();
    return decl;
}

RequiresDecl Parser::parse_requires() {
    RequiresDecl decl;
    auto kw = advance(); // consume 'requires'
    expect(TokenKind::KwMod, "expected 'mod'");
    expect(TokenKind::LParen, "expected '('");
    auto str_tok = expect(TokenKind::String, "expected mod filename string");
    // Strip quotes from the string
    if (str_tok.text.size() >= 2) {
        auto inner = str_tok.text.substr(1, str_tok.text.size() - 2);
        decl.mod_name = pool_.intern(inner);
    }
    expect(TokenKind::RParen, "expected ')'");
    decl.span = kw.span;
    skip_newlines();
    return decl;
}

UseDecl Parser::parse_use() {
    UseDecl decl;
    auto kw = advance(); // consume 'use'
    decl.namespace_path = parse_dotted_name();
    if (match(TokenKind::KwOnly)) {
        expect(TokenKind::LBracket, "expected '['");
        while (!check(TokenKind::RBracket) && !check(TokenKind::Eof)) {
            auto id = expect(TokenKind::Identifier, "expected identifier");
            decl.only.push_back(id.string_id);
            if (!check(TokenKind::RBracket)) {
                expect(TokenKind::Comma, "expected ','");
            }
        }
        expect(TokenKind::RBracket, "expected ']'");
    }
    decl.span = kw.span;
    skip_newlines();
    return decl;
}

ImportIniDecl Parser::parse_import_ini(ImportIniDecl::Kind kind) {
    ImportIniDecl decl;
    decl.kind = kind;
    auto kw = advance(); // consume import_spid or import_kid
    auto str_tok = expect(TokenKind::String, "expected INI filename string");
    if (str_tok.text.size() >= 2) {
        auto inner = str_tok.text.substr(1, str_tok.text.size() - 2);
        decl.path = pool_.intern(inner);
    }
    decl.span = kw.span;
    skip_newlines();
    return decl;
}

StringId Parser::parse_dotted_name() {
    std::string name;
    auto first = expect(TokenKind::Identifier, "expected name");
    name = std::string(first.text);
    while (match(TokenKind::Dot)) {
        auto part = expect(TokenKind::Identifier, "expected name after '.'");
        name += ".";
        name += std::string(part.text);
    }
    return pool_.intern(name);
}

Rule Parser::parse_rule() {
    Rule rule;
    auto name_tok = advance(); // rule name identifier
    rule.name = name_tok.string_id;
    rule.span = name_tok.span;

    // Parse head arguments: rule_name(Arg1, Arg2)
    expect(TokenKind::LParen, "expected '(' after rule name");
    if (!check(TokenKind::RParen)) {
        rule.head_args = parse_arg_list();
    }
    expect(TokenKind::RParen, "expected ')'");
    expect(TokenKind::Colon, "expected ':' after rule head");

    // Expect newline + indent for body
    skip_newlines();
    if (!match(TokenKind::Indent)) {
        diags_.error("E004", "expected indented rule body",
                    peek().span, std::string(lexer_.get_line(peek().span.start_line)));
        synchronize();
        return rule;
    }

    // Parse body clauses until dedent
    while (!check(TokenKind::Dedent) && !check(TokenKind::Eof)) {
        skip_newlines();
        if (check(TokenKind::Dedent) || check(TokenKind::Eof)) break;

        auto tok = peek();

        // Check for bare "=> effect"
        if (check(TokenKind::Arrow)) {
            advance(); // consume =>
            rule.effects.push_back(parse_effect());
            skip_newlines();
            continue;
        }

        // Check for "not" (negated fact pattern)
        if (check(TokenKind::KwNot)) {
            advance();
            auto fact = parse_fact_pattern(/*negated=*/true);
            Clause clause;
            clause.data = std::move(fact);
            clause.span = tok.span;
            rule.body.push_back(std::move(clause));
            skip_newlines();
            continue;
        }

        // Could be: fact pattern, guard, or conditional effect
        // Try to parse a fact pattern or expression
        if (check(TokenKind::Identifier)) {
            // Could be a fact pattern: name(args)
            // Save position to disambiguate
            auto id_tok = advance();

            if (check(TokenKind::LParen)) {
                // It's a fact pattern
                advance(); // (
                std::vector<Expr> args;
                if (!check(TokenKind::RParen)) {
                    args = parse_arg_list();
                }
                expect(TokenKind::RParen, "expected ')'");

                FactPattern fact;
                fact.name = id_tok.string_id;
                fact.args = std::move(args);
                fact.span = id_tok.span;

                Clause clause;
                clause.data = std::move(fact);
                clause.span = id_tok.span;
                rule.body.push_back(std::move(clause));
            } else {
                // Not a fact pattern — error
                diags_.error("E005", "expected '(' after identifier in rule body",
                            id_tok.span, std::string(lexer_.get_line(id_tok.span.start_line)));
            }
            skip_newlines();
            continue;
        }

        // Variable at start of line — could be a guard or conditional effect
        if (check(TokenKind::Variable)) {
            auto expr = parse_comparison();

            // Check for => (conditional effect)
            if (check(TokenKind::Arrow)) {
                advance();
                auto effect = parse_effect();
                ConditionalEffect ce;
                ce.guard = std::make_unique<Expr>(std::move(expr));
                ce.effect = std::move(effect);
                ce.span = tok.span;
                rule.conditional_effects.push_back(std::move(ce));
            } else {
                // It's a guard clause
                Clause clause;
                GuardClause gc;
                gc.expr = std::make_unique<Expr>(std::move(expr));
                gc.span = tok.span;
                clause.data = std::move(gc);
                clause.span = tok.span;
                rule.body.push_back(std::move(clause));
            }
            skip_newlines();
            continue;
        }

        // Unrecognized — skip and try to recover
        diags_.error("E006", "unexpected token in rule body",
                    tok.span, std::string(lexer_.get_line(tok.span.start_line)));
        advance();
        skip_newlines();
    }

    if (check(TokenKind::Dedent)) advance();

    return rule;
}

Effect Parser::parse_effect() {
    Effect effect;
    auto name_tok = expect(TokenKind::Identifier, "expected effect name");
    effect.action = name_tok.string_id;
    effect.span = name_tok.span;
    expect(TokenKind::LParen, "expected '(' after effect name");
    if (!check(TokenKind::RParen)) {
        effect.args = parse_arg_list();
    }
    expect(TokenKind::RParen, "expected ')'");
    return effect;
}

std::vector<Expr> Parser::parse_arg_list() {
    std::vector<Expr> args;
    args.push_back(parse_expr());
    while (match(TokenKind::Comma)) {
        args.push_back(parse_expr());
    }
    return args;
}

Expr Parser::parse_expr() {
    return parse_comparison();
}

Expr Parser::parse_comparison() {
    auto left = parse_additive();

    if (check(TokenKind::Eq) || check(TokenKind::Neq) ||
        check(TokenKind::Lt) || check(TokenKind::Gt) ||
        check(TokenKind::LtEq) || check(TokenKind::GtEq)) {

        auto op_tok = advance();
        auto right = parse_additive();

        BinaryExpr::Op op;
        switch (op_tok.kind) {
            case TokenKind::Eq: op = BinaryExpr::Op::Eq; break;
            case TokenKind::Neq: op = BinaryExpr::Op::Neq; break;
            case TokenKind::Lt: op = BinaryExpr::Op::Lt; break;
            case TokenKind::Gt: op = BinaryExpr::Op::Gt; break;
            case TokenKind::LtEq: op = BinaryExpr::Op::LtEq; break;
            case TokenKind::GtEq: op = BinaryExpr::Op::GtEq; break;
            default: op = BinaryExpr::Op::Eq; break;
        }

        Expr expr;
        BinaryExpr bin;
        bin.op = op;
        bin.left = std::make_unique<Expr>(std::move(left));
        bin.right = std::make_unique<Expr>(std::move(right));
        bin.span = op_tok.span;
        expr.data = std::move(bin);
        expr.span = op_tok.span;
        return expr;
    }

    return left;
}

Expr Parser::parse_additive() {
    auto left = parse_primary();

    while (check(TokenKind::Plus) || check(TokenKind::Minus) ||
           check(TokenKind::Star) || check(TokenKind::Slash)) {
        auto op_tok = advance();
        auto right = parse_primary();

        BinaryExpr::Op op;
        switch (op_tok.kind) {
            case TokenKind::Plus: op = BinaryExpr::Op::Add; break;
            case TokenKind::Minus: op = BinaryExpr::Op::Sub; break;
            case TokenKind::Star: op = BinaryExpr::Op::Mul; break;
            case TokenKind::Slash: op = BinaryExpr::Op::Div; break;
            default: op = BinaryExpr::Op::Add; break;
        }

        Expr expr;
        BinaryExpr bin;
        bin.op = op;
        bin.left = std::make_unique<Expr>(std::move(left));
        bin.right = std::make_unique<Expr>(std::move(right));
        bin.span = op_tok.span;
        expr.data = std::move(bin);
        expr.span = op_tok.span;
        left = std::move(expr);
    }

    return left;
}

Expr Parser::parse_primary() {
    auto tok = peek();

    switch (tok.kind) {
        case TokenKind::Variable: {
            advance();
            Expr expr;
            VariableExpr var;
            var.name = tok.string_id;
            var.span = tok.span;
            expr.data = std::move(var);
            expr.span = tok.span;
            return expr;
        }
        case TokenKind::Symbol: {
            advance();
            Expr expr;
            SymbolExpr sym;
            // Strip leading colon for the name
            if (tok.text.size() > 1) {
                sym.name = pool_.intern(tok.text.substr(1));
            }
            sym.span = tok.span;
            expr.data = std::move(sym);
            expr.span = tok.span;
            return expr;
        }
        case TokenKind::Integer: {
            advance();
            Expr expr;
            IntLiteral lit;
            lit.value = tok.int_value;
            lit.span = tok.span;
            expr.data = std::move(lit);
            expr.span = tok.span;
            return expr;
        }
        case TokenKind::Float: {
            advance();
            Expr expr;
            FloatLiteral lit;
            lit.value = tok.float_value;
            lit.span = tok.span;
            expr.data = std::move(lit);
            expr.span = tok.span;
            return expr;
        }
        case TokenKind::String: {
            advance();
            Expr expr;
            StringLiteral lit;
            if (tok.text.size() >= 2) {
                lit.value = pool_.intern(tok.text.substr(1, tok.text.size() - 2));
            }
            lit.span = tok.span;
            expr.data = std::move(lit);
            expr.span = tok.span;
            return expr;
        }
        case TokenKind::Discard: {
            advance();
            Expr expr;
            DiscardExpr disc;
            disc.span = tok.span;
            expr.data = std::move(disc);
            expr.span = tok.span;
            return expr;
        }
        case TokenKind::Identifier: {
            // Could be a qualified name used as an expression
            advance();
            Expr expr;
            VariableExpr var;
            var.name = tok.string_id;
            var.span = tok.span;
            expr.data = std::move(var);
            expr.span = tok.span;
            return expr;
        }
        case TokenKind::LParen: {
            advance();
            auto expr = parse_expr();
            expect(TokenKind::RParen, "expected ')'");
            return expr;
        }
        default: {
            diags_.error("E007", "expected expression",
                        tok.span, std::string(lexer_.get_line(tok.span.start_line)));
            advance();
            Expr expr;
            expr.span = tok.span;
            DiscardExpr disc;
            disc.span = tok.span;
            expr.data = disc;
            return expr;
        }
    }
}

} // namespace mora
```

The glob in `xmake.lua` auto-discovers this file.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build parser_test && xmake run parser_test`
Expected: All tests PASS (some may need debugging — the indentation/newline interaction is the trickiest part)

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: recursive descent parser for Mora language"
```

---

### Task 10: Name Resolution

**Files:**
- Create: `include/mora/sema/name_resolver.h`
- Create: `src/sema/name_resolver.cpp`
- Create: `tests/name_resolver_test.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/name_resolver_test.cpp
#include <gtest/gtest.h>
#include "mora/sema/name_resolver.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"

class NameResolverTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::Module parse(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        return parser.parse_module();
    }
};

TEST_F(NameResolverTest, BuiltinFactsRegistered) {
    mora::NameResolver resolver(pool, diags);
    // npc, weapon, has_keyword etc. should be registered
    auto* fact = resolver.lookup_fact(pool.intern("npc"));
    ASSERT_NE(fact, nullptr);
    EXPECT_EQ(fact->param_types.size(), 1u); // npc(FormID)
    EXPECT_EQ(fact->param_types[0].kind, mora::TypeKind::NpcID);
}

TEST_F(NameResolverTest, ResolveRuleReferencingBuiltin) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(NameResolverTest, DerivedRuleUsableAsFact) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "\n"
        "elite_bandit(NPC):\n"
        "    bandit(NPC)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(NameResolverTest, UnknownFactError) {
    auto mod = parse(
        "test(NPC):\n"
        "    nonexistent_fact(NPC)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    EXPECT_TRUE(diags.has_errors());
}

TEST_F(NameResolverTest, DuplicateRuleError) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "\n"
        "bandit(NPC):\n"
        "    npc(NPC)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    EXPECT_TRUE(diags.has_errors());
}
```

The test is auto-discovered by the glob in `xmake.lua` — no registration needed.

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build name_resolver_test && xmake run name_resolver_test`
Expected: FAIL

- [ ] **Step 3: Implement NameResolver**

```cpp
// include/mora/sema/name_resolver.h
#pragma once

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <unordered_map>

namespace mora {

struct FactSignature {
    StringId name;
    std::vector<MoraType> param_types;
};

class NameResolver {
public:
    NameResolver(StringPool& pool, DiagBag& diags);

    void resolve(Module& mod);
    const FactSignature* lookup_fact(StringId name) const;

private:
    void register_builtins();
    void register_rule_as_fact(const Rule& rule);
    void resolve_rule(Rule& rule);
    void resolve_clause(Clause& clause);
    void check_fact_exists(const FactPattern& pattern);

    StringPool& pool_;
    DiagBag& diags_;
    std::unordered_map<uint32_t, FactSignature> facts_; // key: StringId.index
    std::unordered_map<uint32_t, bool> rules_; // key: StringId.index, for duplicate detection
};

} // namespace mora
```

```cpp
// src/sema/name_resolver.cpp
#include "mora/sema/name_resolver.h"

namespace mora {

NameResolver::NameResolver(StringPool& pool, DiagBag& diags)
    : pool_(pool), diags_(diags) {
    register_builtins();
}

void NameResolver::register_builtins() {
    auto reg = [&](const char* name, std::vector<MoraType> params) {
        auto id = pool_.intern(name);
        facts_[id.index] = {id, std::move(params)};
    };

    // Existence facts
    reg("npc", {MoraType::make(TypeKind::NpcID)});
    reg("weapon", {MoraType::make(TypeKind::WeaponID)});
    reg("armor", {MoraType::make(TypeKind::ArmorID)});
    reg("spell", {MoraType::make(TypeKind::SpellID)});
    reg("perk", {MoraType::make(TypeKind::PerkID)});
    reg("keyword", {MoraType::make(TypeKind::KeywordID)});
    reg("faction", {MoraType::make(TypeKind::FactionID)});
    reg("race", {MoraType::make(TypeKind::RaceID)});
    reg("leveled_list", {MoraType::make(TypeKind::FormID)});

    // Property facts
    reg("has_keyword", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::KeywordID)});
    reg("has_faction", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::FactionID)});
    reg("has_perk", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::PerkID)});
    reg("has_spell", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::SpellID)});
    reg("base_level", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Int)});
    reg("level", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Int)});
    reg("race_of", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::RaceID)});
    reg("name", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::String)});
    reg("editor_id", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::String)});
    reg("gold_value", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Int)});
    reg("weight", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Float)});
    reg("damage", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Int)});
    reg("armor_rating", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Int)});

    // Relationship facts
    reg("template_of", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::FormID)});
    reg("leveled_entry", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Int)});
    reg("outfit_has", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::FormID)});

    // Instance facts
    reg("current_level", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Int)});
    reg("current_location", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::LocationID)});
    reg("current_cell", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::CellID)});
    reg("equipped", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::FormID)});
    reg("in_inventory", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Int)});
    reg("quest_stage", {MoraType::make(TypeKind::QuestID), MoraType::make(TypeKind::Int)});
    reg("is_alive", {MoraType::make(TypeKind::FormID)});

    // Built-in effects (actions)
    reg("add_keyword", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::KeywordID)});
    reg("add_item", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::FormID)});
    reg("add_spell", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::SpellID)});
    reg("add_perk", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::PerkID)});
    reg("remove_keyword", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::KeywordID)});
    reg("set_name", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::String)});
    reg("set_damage", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Int)});
    reg("set_armor_rating", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Int)});
    reg("set_gold_value", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Int)});
    reg("set_weight", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Float)});
    reg("distribute_items", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::FormID)});
    reg("set_game_setting", {MoraType::make(TypeKind::FormID), MoraType::make(TypeKind::Float)});
}

void NameResolver::register_rule_as_fact(const Rule& rule) {
    // The rule head becomes a derived fact that other rules can reference
    std::vector<MoraType> params;
    // Initially all params are Unknown — type checker will refine
    for (size_t i = 0; i < rule.head_args.size(); i++) {
        params.push_back(MoraType::make(TypeKind::Unknown));
    }
    facts_[rule.name.index] = {rule.name, std::move(params)};
}

void NameResolver::resolve(Module& mod) {
    // First pass: register all rule names (so forward references work)
    for (const auto& rule : mod.rules) {
        if (rules_.count(rule.name.index)) {
            diags_.error("E010", "duplicate rule '" + std::string(pool_.get(rule.name)) + "'",
                        rule.span, "");
        } else {
            rules_[rule.name.index] = true;
            register_rule_as_fact(rule);
        }
    }

    // Second pass: resolve references within rule bodies
    for (auto& rule : mod.rules) {
        resolve_rule(rule);
    }
}

void NameResolver::resolve_rule(Rule& rule) {
    for (auto& clause : rule.body) {
        resolve_clause(clause);
    }
    for (auto& effect : rule.effects) {
        check_fact_exists({effect.action, {}, effect.args, false, effect.span});
    }
    for (auto& ce : rule.conditional_effects) {
        check_fact_exists({ce.effect.action, {}, ce.effect.args, false, ce.effect.span});
    }
}

void NameResolver::resolve_clause(Clause& clause) {
    if (auto* fact = std::get_if<FactPattern>(&clause.data)) {
        check_fact_exists(*fact);
    }
    // Guards and conditional effects don't need name resolution beyond what's already bound
}

void NameResolver::check_fact_exists(const FactPattern& pattern) {
    if (facts_.find(pattern.name.index) == facts_.end()) {
        diags_.error("E011", "unknown fact or rule '" + std::string(pool_.get(pattern.name)) + "'",
                    pattern.span, "");
    }
}

const FactSignature* NameResolver::lookup_fact(StringId name) const {
    auto it = facts_.find(name.index);
    if (it != facts_.end()) return &it->second;
    return nullptr;
}

} // namespace mora
```

The glob in `xmake.lua` auto-discovers this file.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build name_resolver_test && xmake run name_resolver_test`
Expected: All tests PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: name resolver with builtin Skyrim fact registry"
```

---

### Task 11: Type Checker

**Files:**
- Create: `include/mora/sema/type_checker.h`
- Create: `src/sema/type_checker.cpp`
- Create: `tests/type_checker_test.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/type_checker_test.cpp
#include <gtest/gtest.h>
#include "mora/sema/type_checker.h"
#include "mora/sema/name_resolver.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"

class TypeCheckerTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::Module parse_and_resolve(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        auto mod = parser.parse_module();
        mora::NameResolver resolver(pool, diags);
        resolver.resolve(mod);
        return mod;
    }

    void type_check(mora::Module& mod) {
        mora::NameResolver resolver(pool, diags);
        resolver.resolve(mod);
        mora::TypeChecker checker(pool, diags, resolver);
        checker.check(mod);
    }
};

TEST_F(TypeCheckerTest, ValidRule) {
    auto mod = parse_and_resolve(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(TypeCheckerTest, TypeMismatchInFact) {
    auto mod = parse_and_resolve(
        "wrong(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :IronSword)\n"
    );
    // :IronSword would be WeaponID, has_faction expects FactionID
    // Note: in Phase 1, symbols don't have resolved types from ESP data,
    // so this test validates the infrastructure is in place.
    // Full type checking of symbols requires ESP data (Plan 2).
    // For now, test that the checker runs without crashing.
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    // No assertion on errors — symbol type resolution requires ESP data
}

TEST_F(TypeCheckerTest, ArityMismatch) {
    auto mod = parse_and_resolve(
        "wrong(NPC):\n"
        "    npc(NPC, :Extra)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    EXPECT_TRUE(diags.has_errors()); // npc takes 1 arg, got 2
}

TEST_F(TypeCheckerTest, VariableTypeInference) {
    auto mod = parse_and_resolve(
        "test(NPC):\n"
        "    npc(NPC)\n"
        "    base_level(NPC, Level)\n"
        "    Level >= 20\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    EXPECT_FALSE(diags.has_errors());
    // NPC should be inferred as NpcID, Level as Int
}

TEST_F(TypeCheckerTest, UnusedVariableWarning) {
    auto mod = parse_and_resolve(
        "test(NPC):\n"
        "    npc(NPC)\n"
        "    base_level(NPC, Level)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    // Level is bound but unused — should produce a warning
    EXPECT_GT(diags.warning_count(), 0u);
}

TEST_F(TypeCheckerTest, DiscardNoWarning) {
    auto mod = parse_and_resolve(
        "test(NPC):\n"
        "    npc(NPC)\n"
        "    base_level(NPC, _)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    EXPECT_EQ(diags.warning_count(), 0u);
}

TEST_F(TypeCheckerTest, RuleComposition) {
    auto mod = parse_and_resolve(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "\n"
        "elite_bandit(NPC):\n"
        "    bandit(NPC)\n"
        "    base_level(NPC, Level)\n"
        "    Level >= 30\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    EXPECT_FALSE(diags.has_errors());
}
```

The test is auto-discovered by the glob in `xmake.lua` — no registration needed.

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build type_checker_test && xmake run type_checker_test`
Expected: FAIL

- [ ] **Step 3: Implement TypeChecker**

```cpp
// include/mora/sema/type_checker.h
#pragma once

#include "mora/ast/ast.h"
#include "mora/sema/name_resolver.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <unordered_map>
#include <unordered_set>

namespace mora {

class TypeChecker {
public:
    TypeChecker(StringPool& pool, DiagBag& diags, const NameResolver& resolver);

    void check(Module& mod);

private:
    void check_rule(Rule& rule);
    void check_fact_pattern(const FactPattern& pattern);
    void check_effect(const Effect& effect);
    void check_guard(const Expr& expr);
    MoraType infer_expr_type(const Expr& expr);

    void bind_variable(StringId name, MoraType type, const SourceSpan& span);
    MoraType lookup_variable(StringId name) const;
    void check_unused_variables(const Rule& rule);

    StringPool& pool_;
    DiagBag& diags_;
    const NameResolver& resolver_;

    // Per-rule variable bindings: variable name -> inferred type
    std::unordered_map<uint32_t, MoraType> var_types_;
    // Per-rule variable usage tracking
    std::unordered_set<uint32_t> var_used_;
    std::unordered_map<uint32_t, SourceSpan> var_def_spans_;
};

} // namespace mora
```

```cpp
// src/sema/type_checker.cpp
#include "mora/sema/type_checker.h"

namespace mora {

TypeChecker::TypeChecker(StringPool& pool, DiagBag& diags, const NameResolver& resolver)
    : pool_(pool), diags_(diags), resolver_(resolver) {}

void TypeChecker::check(Module& mod) {
    for (auto& rule : mod.rules) {
        check_rule(rule);
    }
}

void TypeChecker::check_rule(Rule& rule) {
    var_types_.clear();
    var_used_.clear();
    var_def_spans_.clear();

    // Bind head arguments as variables
    for (auto& arg : rule.head_args) {
        if (auto* var = std::get_if<VariableExpr>(&arg.data)) {
            bind_variable(var->name, MoraType::make(TypeKind::Unknown), var->span);
            var_used_.insert(var->name.index); // head args are always "used"
        }
    }

    // Check body clauses
    for (auto& clause : rule.body) {
        if (auto* fact = std::get_if<FactPattern>(&clause.data)) {
            check_fact_pattern(*fact);
        } else if (auto* guard = std::get_if<GuardClause>(&clause.data)) {
            check_guard(*guard->expr);
        }
    }

    // Check effects
    for (auto& effect : rule.effects) {
        check_effect(effect);
    }
    for (auto& ce : rule.conditional_effects) {
        check_guard(*ce.guard);
        check_effect(ce.effect);
    }

    // Check for unused variables
    check_unused_variables(rule);
}

void TypeChecker::check_fact_pattern(const FactPattern& pattern) {
    const auto* sig = resolver_.lookup_fact(pattern.name);
    if (!sig) return; // name resolver already reported error

    // Arity check
    if (pattern.args.size() != sig->param_types.size()) {
        diags_.error("E020",
            "'" + std::string(pool_.get(pattern.name)) + "' expects " +
            std::to_string(sig->param_types.size()) + " argument(s), got " +
            std::to_string(pattern.args.size()),
            pattern.span, "");
        return;
    }

    // Bind/check each argument
    for (size_t i = 0; i < pattern.args.size(); i++) {
        const auto& arg = pattern.args[i];
        const auto& expected = sig->param_types[i];

        if (auto* var = std::get_if<VariableExpr>(&arg.data)) {
            auto existing = lookup_variable(var->name);
            if (existing.kind == TypeKind::Unknown) {
                // First binding of this variable — assign type
                bind_variable(var->name, expected, var->span);
            } else {
                // Variable already bound — check compatibility
                if (!existing.is_subtype_of(expected) && !expected.is_subtype_of(existing)) {
                    diags_.error("E012",
                        "type mismatch: variable '" + std::string(pool_.get(var->name)) +
                        "' has type " + existing.to_string() +
                        " but " + std::string(pool_.get(pattern.name)) +
                        " expects " + expected.to_string(),
                        var->span, "");
                }
            }
            var_used_.insert(var->name.index);
        } else if (auto* disc = std::get_if<DiscardExpr>(&arg.data)) {
            // Discard — no binding needed
        } else {
            // Literal or expression — check type compatibility
            auto actual = infer_expr_type(arg);
            if (actual.kind != TypeKind::Unknown && actual.kind != TypeKind::Error &&
                !actual.is_subtype_of(expected)) {
                diags_.error("E012",
                    "type mismatch: expected " + expected.to_string() +
                    ", got " + actual.to_string(),
                    arg.span, "");
            }
        }
    }
}

void TypeChecker::check_effect(const Effect& effect) {
    const auto* sig = resolver_.lookup_fact(effect.action);
    if (!sig) return;

    if (effect.args.size() != sig->param_types.size()) {
        diags_.error("E020",
            "'" + std::string(pool_.get(effect.action)) + "' expects " +
            std::to_string(sig->param_types.size()) + " argument(s), got " +
            std::to_string(effect.args.size()),
            effect.span, "");
        return;
    }

    for (size_t i = 0; i < effect.args.size(); i++) {
        const auto& arg = effect.args[i];
        if (auto* var = std::get_if<VariableExpr>(&arg.data)) {
            var_used_.insert(var->name.index);
        }
    }
}

void TypeChecker::check_guard(const Expr& expr) {
    // Mark variables as used in the guard expression
    if (auto* bin = std::get_if<BinaryExpr>(&expr.data)) {
        check_guard(*bin->left);
        check_guard(*bin->right);
    } else if (auto* var = std::get_if<VariableExpr>(&expr.data)) {
        var_used_.insert(var->name.index);
    }
}

MoraType TypeChecker::infer_expr_type(const Expr& expr) {
    if (auto* var = std::get_if<VariableExpr>(&expr.data)) {
        var_used_.insert(var->name.index);
        return lookup_variable(var->name);
    }
    if (std::get_if<IntLiteral>(&expr.data)) return MoraType::make(TypeKind::Int);
    if (std::get_if<FloatLiteral>(&expr.data)) return MoraType::make(TypeKind::Float);
    if (std::get_if<StringLiteral>(&expr.data)) return MoraType::make(TypeKind::String);
    if (std::get_if<SymbolExpr>(&expr.data)) return MoraType::make(TypeKind::Unknown); // resolved later with ESP data
    if (std::get_if<DiscardExpr>(&expr.data)) return MoraType::make(TypeKind::Unknown);
    if (auto* bin = std::get_if<BinaryExpr>(&expr.data)) {
        auto left = infer_expr_type(*bin->left);
        auto right = infer_expr_type(*bin->right);
        // Comparison operators return Bool
        switch (bin->op) {
            case BinaryExpr::Op::Eq:
            case BinaryExpr::Op::Neq:
            case BinaryExpr::Op::Lt:
            case BinaryExpr::Op::Gt:
            case BinaryExpr::Op::LtEq:
            case BinaryExpr::Op::GtEq:
                return MoraType::make(TypeKind::Bool);
            case BinaryExpr::Op::Add:
            case BinaryExpr::Op::Sub:
            case BinaryExpr::Op::Mul:
            case BinaryExpr::Op::Div:
                if (left.kind == TypeKind::Float || right.kind == TypeKind::Float)
                    return MoraType::make(TypeKind::Float);
                return MoraType::make(TypeKind::Int);
        }
    }
    return MoraType::make(TypeKind::Unknown);
}

void TypeChecker::bind_variable(StringId name, MoraType type, const SourceSpan& span) {
    var_types_[name.index] = type;
    var_def_spans_[name.index] = span;
}

MoraType TypeChecker::lookup_variable(StringId name) const {
    auto it = var_types_.find(name.index);
    if (it != var_types_.end()) return it->second;
    return MoraType::make(TypeKind::Unknown);
}

void TypeChecker::check_unused_variables(const Rule& rule) {
    for (auto& [name_idx, span] : var_def_spans_) {
        if (var_used_.find(name_idx) == var_used_.end()) {
            StringId id{name_idx};
            auto name_str = pool_.get(id);
            if (name_str != "_") {
                diags_.warning("W007", "unused variable '" + std::string(name_str) + "'",
                              span, "");
            }
        }
    }
}

} // namespace mora
```

The glob in `xmake.lua` auto-discovers this file.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build type_checker_test && xmake run type_checker_test`
Expected: All tests PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: type checker with inference, arity checks, and unused variable warnings"
```

---

### Task 12: CLI — Progress Display

**Files:**
- Create: `include/mora/cli/progress.h`
- Create: `src/cli/progress.cpp`

- [ ] **Step 1: Implement progress display**

```cpp
// include/mora/cli/progress.h
#pragma once

#include "mora/cli/terminal.h"
#include <chrono>
#include <string>

namespace mora {

class Progress {
public:
    explicit Progress(bool use_color = true, bool is_tty = true);

    void start_phase(const std::string& name);
    void finish_phase(const std::string& detail, const std::string& timing);
    void print_header(const std::string& version);
    void print_success(const std::string& message);
    void print_failure(const std::string& message);
    void print_summary(size_t frozen_rules, size_t frozen_patches,
                       size_t dynamic_rules, size_t conflicts,
                       size_t errors, size_t warnings,
                       const std::string& spatch_size);

private:
    bool color_;
    bool is_tty_;
    std::chrono::steady_clock::time_point phase_start_;
};

} // namespace mora
```

```cpp
// src/cli/progress.cpp
#include "mora/cli/progress.h"
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace mora {

Progress::Progress(bool use_color, bool is_tty)
    : color_(use_color), is_tty_(is_tty) {}

void Progress::print_header(const std::string& version) {
    std::printf("\n  %s\n\n",
        TermStyle::bold("mora v" + version, color_).c_str());
}

void Progress::start_phase(const std::string& name) {
    phase_start_ = std::chrono::steady_clock::now();
    if (is_tty_) {
        std::printf("  %s ...\r",
            TermStyle::bold(name, color_).c_str());
        std::fflush(stdout);
    }
}

void Progress::finish_phase(const std::string& detail, const std::string& timing) {
    // Build the dots line
    std::string name_and_dots;
    // Pad to fixed width for alignment
    std::string line = "  " + detail;
    int pad = 60 - static_cast<int>(line.size());
    if (pad < 2) pad = 2;
    std::string dots(pad, '\xC2'); // placeholder, we'll use actual dots
    // Actually just use spaces + dots pattern
    std::string padded = line;
    while (padded.size() < 55) padded += " \xC2\xB7"; // · (middle dot UTF-8)
    // Simpler approach: just right-align the timing
    if (is_tty_) {
        std::printf("\r");
    }
    std::printf("  %-50s %s\n",
        detail.c_str(),
        TermStyle::dim(timing, color_).c_str());
}

void Progress::print_success(const std::string& message) {
    std::printf("\n  %s %s\n",
        TermStyle::green("\u2713", color_).c_str(),
        TermStyle::green(message, color_).c_str());
}

void Progress::print_failure(const std::string& message) {
    std::printf("\n  %s %s\n",
        TermStyle::red("\u2717", color_).c_str(),
        TermStyle::red(message, color_).c_str());
}

void Progress::print_summary(size_t frozen_rules, size_t frozen_patches,
                              size_t dynamic_rules, size_t conflicts,
                              size_t errors, size_t warnings,
                              const std::string& spatch_size) {
    std::printf("\n  Summary:\n");
    std::printf("    %s rules frozen \u2192 mora.spatch (%s, %zu patches)\n",
        TermStyle::green(std::to_string(frozen_rules), color_).c_str(),
        spatch_size.c_str(), frozen_patches);
    std::printf("    %s rules dynamic \u2192 mora.rt\n",
        TermStyle::cyan(std::to_string(dynamic_rules), color_).c_str());
    if (conflicts > 0) {
        std::printf("    %s conflicts resolved (see mora.log)\n",
            TermStyle::yellow(std::to_string(conflicts), color_).c_str());
    }
    std::printf("    %zu errors, %zu warnings\n\n", errors, warnings);
}

} // namespace mora
```

The glob in `xmake.lua` auto-discovers this file.

- [ ] **Step 2: Build to verify it compiles**

Run: `xmake build mora_lib`
Expected: compiles

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: CLI progress display with colored output"
```

---

### Task 13: CLI Entry Point — `mora check` Command

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Implement CLI argument parsing and `mora check`**

```cpp
// src/main.cpp
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
#include "mora/diag/diagnostic.h"
#include "mora/diag/renderer.h"
#include "mora/cli/terminal.h"
#include "mora/cli/progress.h"
#include "mora/core/string_pool.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string read_file(const fs::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::fprintf(stderr, "error: could not open file: %s\n", path.string().c_str());
        return "";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::vector<fs::path> find_mora_files(const fs::path& dir) {
    std::vector<fs::path> files;
    if (!fs::exists(dir)) return files;
    for (auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.path().extension() == ".mora") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

static void print_usage() {
    std::printf("Usage: mora <command> [options] [path]\n\n");
    std::printf("Commands:\n");
    std::printf("  check     Type check and lint .mora files\n");
    std::printf("  compile   Compile .mora files (not yet implemented)\n");
    std::printf("  inspect   Dump .spatch contents (not yet implemented)\n");
    std::printf("  info      Show project status (not yet implemented)\n");
    std::printf("\nOptions:\n");
    std::printf("  --no-color    Disable colored output\n");
    std::printf("  -v            Verbose output\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];
    bool force_no_color = false;
    bool verbose = false;
    std::string target_path = ".";

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-color") force_no_color = true;
        else if (arg == "-v") verbose = true;
        else target_path = arg;
    }

    bool use_color = !force_no_color && mora::color_enabled();
    bool is_tty = mora::stdout_is_tty();
    mora::Progress progress(use_color, is_tty);

    if (command == "check") {
        auto start = std::chrono::steady_clock::now();
        progress.print_header("0.1.0");

        // Find .mora files
        auto files = find_mora_files(target_path);
        if (files.empty()) {
            std::fprintf(stderr, "  No .mora files found in %s\n", target_path.c_str());
            return 1;
        }

        mora::StringPool pool;
        mora::DiagBag diags;

        // Parse all files
        progress.start_phase("Parsing");
        std::vector<mora::Module> modules;
        for (auto& file : files) {
            auto source = read_file(file);
            if (source.empty()) continue;
            mora::Lexer lexer(source, file.string(), pool, diags);
            mora::Parser parser(lexer, pool, diags);
            auto mod = parser.parse_module();
            mod.filename = file.string();
            modules.push_back(std::move(mod));
        }
        auto parse_end = std::chrono::steady_clock::now();
        auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(parse_end - start).count();
        progress.finish_phase(
            "Parsing " + std::to_string(files.size()) + " files",
            std::to_string(parse_ms) + "ms");

        // Resolve names
        progress.start_phase("Resolving");
        mora::NameResolver resolver(pool, diags);
        size_t rule_count = 0;
        for (auto& mod : modules) {
            resolver.resolve(mod);
            rule_count += mod.rules.size();
        }
        auto resolve_end = std::chrono::steady_clock::now();
        auto resolve_ms = std::chrono::duration_cast<std::chrono::milliseconds>(resolve_end - parse_end).count();
        progress.finish_phase(
            "Resolving " + std::to_string(rule_count) + " rules",
            std::to_string(resolve_ms) + "ms");

        // Type check
        progress.start_phase("Type checking");
        mora::TypeChecker checker(pool, diags, resolver);
        for (auto& mod : modules) {
            checker.check(mod);
        }
        auto check_end = std::chrono::steady_clock::now();
        auto check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(check_end - resolve_end).count();
        progress.finish_phase(
            "Type checking " + std::to_string(rule_count) + " rules",
            std::to_string(check_ms) + "ms");

        // Render diagnostics
        mora::DiagRenderer renderer(use_color);
        if (!diags.all().empty()) {
            std::printf("\n");
            std::printf("%s", renderer.render_all(diags).c_str());
        }

        auto total_end = std::chrono::steady_clock::now();
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - start).count();

        if (diags.has_errors()) {
            progress.print_failure("Failed with " + std::to_string(diags.error_count()) +
                " error(s) in " + std::to_string(total_ms) + "ms");
            return 1;
        } else {
            std::string msg = "Checked " + std::to_string(rule_count) + " rules in " +
                std::to_string(total_ms) + "ms";
            if (diags.warning_count() > 0) {
                msg += " (" + std::to_string(diags.warning_count()) + " warnings)";
            }
            progress.print_success(msg);
            return 0;
        }
    }

    if (command == "compile" || command == "inspect" || command == "info" || command == "dump") {
        std::fprintf(stderr, "  '%s' command not yet implemented (see Plan 2)\n", command.c_str());
        return 1;
    }

    print_usage();
    return 1;
}
```

- [ ] **Step 2: Create a test .mora file and run `mora check`**

Create `test_data/example.mora`:
```python
namespace test.example

requires mod("Skyrim.esm")

bandit(NPC):
    npc(NPC)
    has_faction(NPC, :BanditFaction)

vampire_bane(Weapon):
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialSilver)
    not has_keyword(Weapon, :WeapTypeGreatsword)
    => add_keyword(Weapon, :VampireBane)

bandit_weapons(NPC):
    bandit(NPC)
    level(NPC, Level)
    Level >= 20 => add_item(NPC, :SilverSword)
    Level < 20 => add_item(NPC, :IronSword)
```

Run:
```bash
xmake run mora check test_data/
```
Expected: Shows parsing/resolving/type checking progress, reports success with rule count.

- [ ] **Step 3: Test with a file containing errors**

Create `test_data/errors.mora`:
```python
namespace test.errors

wrong(NPC):
    npc(NPC, :Extra)

also_wrong(NPC):
    nonexistent_fact(NPC)
```

Run:
```bash
xmake run mora check test_data/errors.mora
```
Expected: Shows error diagnostics with file locations, underlines, and colored output.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: mora check command with end-to-end parsing, resolution, and type checking"
```

---

### Task 14: Integration Test — Full Pipeline

**Files:**
- Create: `tests/integration_test.cpp`

- [ ] **Step 1: Write end-to-end integration tests**

```cpp
// tests/integration_test.cpp
#include <gtest/gtest.h>
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
#include "mora/diag/renderer.h"

class IntegrationTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    // Full pipeline: parse -> resolve -> type check
    bool check(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        auto mod = parser.parse_module();
        mora::NameResolver resolver(pool, diags);
        resolver.resolve(mod);
        mora::TypeChecker checker(pool, diags, resolver);
        checker.check(mod);
        return !diags.has_errors();
    }
};

TEST_F(IntegrationTest, CompleteValidFile) {
    EXPECT_TRUE(check(
        "namespace test.patches\n"
        "\n"
        "requires mod(\"Skyrim.esm\")\n"
        "\n"
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "\n"
        "high_level(NPC):\n"
        "    base_level(NPC, Level)\n"
        "    Level >= 30\n"
        "\n"
        "elite_bandit(NPC):\n"
        "    bandit(NPC)\n"
        "    high_level(NPC)\n"
        "\n"
        "vampire_bane(Weapon):\n"
        "    weapon(Weapon)\n"
        "    has_keyword(Weapon, :WeapMaterialSilver)\n"
        "    not has_keyword(Weapon, :WeapTypeGreatsword)\n"
        "    => add_keyword(Weapon, :VampireBane)\n"
        "\n"
        "bandit_weapons(NPC):\n"
        "    bandit(NPC)\n"
        "    level(NPC, Level)\n"
        "    Level >= 20 => add_item(NPC, :SilverSword)\n"
        "    Level < 20 => add_item(NPC, :IronSword)\n"
    ));
}

TEST_F(IntegrationTest, ArityMismatchDetected) {
    EXPECT_FALSE(check(
        "wrong(NPC):\n"
        "    npc(NPC, :Extra)\n"
    ));
}

TEST_F(IntegrationTest, UnknownFactDetected) {
    EXPECT_FALSE(check(
        "wrong(NPC):\n"
        "    completely_fake(NPC)\n"
    ));
}

TEST_F(IntegrationTest, RuleCompositionWorks) {
    EXPECT_TRUE(check(
        "a(X):\n"
        "    npc(X)\n"
        "\n"
        "b(X):\n"
        "    a(X)\n"
        "\n"
        "c(X):\n"
        "    b(X)\n"
        "    => add_keyword(X, :TestKeyword)\n"
    ));
}

TEST_F(IntegrationTest, DiagnosticsRender) {
    check(
        "wrong(NPC):\n"
        "    npc(NPC, :Extra)\n"
    );
    mora::DiagRenderer renderer(/*use_color=*/false);
    auto output = renderer.render_all(diags);
    EXPECT_NE(output.find("E020"), std::string::npos);
    EXPECT_NE(output.find("expects 1"), std::string::npos);
}
```

The test is auto-discovered by the glob in `xmake.lua` — no registration needed.

- [ ] **Step 2: Run all tests**

Run: `xmake build integration_test && xmake run integration_test`
Expected: All tests PASS

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: integration tests for full check pipeline"
```

---

### Task 15: Final Build Verification

**Files:**
- Verify: `xmake.lua`

- [ ] **Step 1: Full build and test run**

Run:
```bash
xmake build
xmake test
```
Expected: Everything builds and all tests pass.

- [ ] **Step 2: Run mora check on test data**

Run:
```bash
xmake run mora check test_data/
```
Expected: Clean output with progress display, no errors.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "chore: final build verification"
```

---

## Summary

This plan delivers a working `mora check` command that:
- Lexes `.mora` files with full indentation tracking
- Parses into a typed AST via recursive descent
- Resolves names against built-in Skyrim fact registry + derived rules
- Type checks with inference, arity validation, and subtype checking
- Reports unused variables, unknown facts, and type mismatches
- Renders diagnostics with colored Rust/Elm-style formatting
- Shows progress phases with timing

**Not covered (deferred to Plan 2: Compiler Backend):**
- Phase classification (static vs dynamic)
- ESP parser library
- Rule evaluation engine
- `.spatch` and `.mora.rt` binary format emission
- `mora compile`, `mora inspect`, `mora info` commands
- Conflict resolution
- Stale cache detection
