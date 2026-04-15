# VS Code Mora — Phase 3: LSP semantic features

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add hover, goto-definition, find-references, document/workspace symbols, and semantic tokens to `mora lsp`. Hoist `SchemaRegistry` into the workspace, add re-parse debouncing, and best-effort atom (`@Foo`) resolution from the user's Skyrim data folder.

**Architecture:** A new per-document `SymbolIndex` (built after sema runs) maps `(line, col) → SymbolEntry`. Every semantic feature is a position lookup against that index plus a small handler that builds the LSP response. The Workspace owns one shared `SchemaRegistry` (loaded on `initialize`) plus an optional `EditorIdRegistry` for atom resolution. Re-parse debouncing uses a non-blocking poll loop: when a `didChange` arrives, the document records a `reparse_after` deadline; the run loop polls stdin with a short timeout and runs deferred parses when their deadlines pass.

**Tech Stack:** C++20 (xmake + g++/MSVC), gtest, nlohmann/json (existing), VS Code 1.85+, TypeScript 5.x, `vscode-languageclient` 9.x.

**Spec reference:** `docs/superpowers/specs/2026-04-15-vscode-mora-language-support-design.md` — Phase 3 section + Section 4 (LSP feature behaviour) + Section 5 deferred items (debouncing, shared SchemaRegistry, atom hover).

---

## File structure

### Created in this plan

| Path | Purpose |
| --- | --- |
| `include/mora/lsp/symbol_index.h` | `SymbolEntry`, `SymbolKind`, `SymbolIndex` API. |
| `src/lsp/symbol_index.cpp` | Builds index by walking a resolved `Module`. Position-lookup helpers. |
| `include/mora/lsp/editor_id_registry.h` | Atom (`@Foo`) → FormID lookup, populated lazily from `mora.dataDir`. |
| `src/lsp/editor_id_registry.cpp` | Best-effort ESP scan; returns `std::nullopt` when unset. |
| `src/lsp/handlers/hover.cpp` | `textDocument/hover` — markdown by symbol kind. |
| `src/lsp/handlers/definition.cpp` | `textDocument/definition` — rule head, YAML line, variable binding. |
| `src/lsp/handlers/references.cpp` | `textDocument/references` — workspace-wide scan of indexes. |
| `src/lsp/handlers/document_symbols.cpp` | `textDocument/documentSymbol` — outline tree from one Document. |
| `src/lsp/handlers/workspace_symbols.cpp` | `workspace/symbol` — fuzzy across all documents' indexes. |
| `src/lsp/handlers/semantic_tokens.cpp` | `textDocument/semanticTokens/full` (delta deferred — see follow-ups). |
| `tests/lsp/test_symbol_index.cpp` | Unit tests for the index. |
| `tests/lsp/test_hover.cpp` | gtest: hover on each symbol kind. |
| `tests/lsp/test_definition.cpp` | gtest: definition on each symbol kind. |
| `tests/lsp/test_references.cpp` | gtest: references for each symbol kind. |
| `tests/lsp/test_document_symbols.cpp` | gtest: outline shape. |
| `tests/lsp/test_semantic_tokens.cpp` | gtest: token spans + delta-encoding. |

### Modified in this plan

| Path | Change |
| --- | --- |
| `include/mora/lsp/workspace.h` | Owns `SchemaRegistry` + `EditorIdRegistry`. Adds `schema()`, `editor_ids()`, `relations_dir()` accessors. New `set_data_dir(path)` and `set_relations_dir(path)`. |
| `src/lsp/workspace.cpp` | Loads SchemaRegistry on `mark_initialized()`. Re-parse debouncing helpers. |
| `include/mora/lsp/document.h` | Holds parsed `Module` + `SymbolIndex` after parse; exposes them. Takes `const Workspace&` reference for shared registries. |
| `src/lsp/document.cpp` | Uses workspace's SchemaRegistry. After parse/resolve/typecheck, builds `SymbolIndex`. |
| `src/lsp/handlers/lifecycle.cpp` | `initialize` advertises the new capabilities. Reads `initializationOptions.dataDir` (fallback to `rootUri`-relative `data/relations/`). Calls `ws.set_data_dir` / `ws.set_relations_dir` before `ws.mark_initialized()`. |
| `src/lsp/server.cpp` | Forward-declares + registers all new handlers. Run-loop becomes poll-based to support debouncing. |
| `include/mora/sema/type_checker.h` | Public accessor for `var_def_spans_` (read-only) so the SymbolIndex can find variable binding sites without re-walking. |
| `src/sema/type_checker.cpp` | Trivial getter implementation. |
| `include/mora/sema/name_resolver.h` | Add `const std::unordered_map<uint32_t, FactSignature>& facts() const` so the SymbolIndex can enumerate user-defined facts. |
| `editors/vscode/src/extension.ts` | Pass `mora.dataDir` setting via `initializationOptions`. Register the semantic-tokens legend on the client side (matching the server's). |
| `editors/vscode/package.json` | Bump version to `0.3.0`. |
| `editors/vscode/CHANGELOG.md` | 0.3.0 entry. |
| `xmake.lua` | Bump `set_version("0.3.0")`. |

---

## Conventions

- **Working directory:** the worktree root (`.worktrees/<branch>` if executing in a worktree, else the repo root). Subagents must `cd` to that root before each Bash invocation.
- **xmake quirk:** when in a worktree, always invoke `xmake -P . <subcommand>` to scope to the worktree's project (xmake walks up to the parent repo otherwise). Use `xmake build -P . -y <target>`, `xmake run -P . <target>`, `xmake test -P . -v`.
- **Public headers** under `include/mora/lsp/`. Implementation under `src/lsp/`. Handlers under `src/lsp/handlers/`. (Established convention from Phase 2.)
- **Commits:** one per task, conventional-commits style. Plan-specified commit messages are mandatory.
- **No push to remote** during execution.

---

## Coding constraints

These apply across all tasks:

1. **Single-threaded run loop** — same as Phase 2. Debouncing uses non-blocking stdin polling, not threads.
2. **No exceptions across the JSON-RPC boundary** — `Dispatcher::dispatch` already catches; new handlers must continue to use `.at()` etc. and let the dispatcher convert.
3. **Shared `SchemaRegistry` is read-only** during request handling — handlers may only call `const` methods on it.
4. **Atom resolution is best-effort** — if `mora.dataDir` is unset or invalid, `EditorIdRegistry::lookup` returns `std::nullopt`. Handlers degrade hover content but never fail.
5. **Capability negotiation:** the new capabilities are added to `initialize` *only* when their handlers are wired up. Don't advertise hover before Task 8 lands, etc. (See Task 26 for the consolidated capability bump.)

---

### Task 1: Hoist SchemaRegistry into Workspace

**Goal:** The Workspace owns one shared `SchemaRegistry`, loaded on `initialize`. Documents stop building their own.

**Files:**
- Modify: `include/mora/lsp/workspace.h`
- Modify: `src/lsp/workspace.cpp`
- Modify: `include/mora/lsp/document.h`
- Modify: `src/lsp/document.cpp`
- Modify: `src/lsp/handlers/lifecycle.cpp`
- Test: `tests/lsp/test_workspace.cpp` (extend)

- [ ] **Step 1: Add SchemaRegistry to Workspace**

In `include/mora/lsp/workspace.h`, add:

```cpp
#include "mora/data/schema_registry.h"
#include <filesystem>
#include <memory>
```

Inside `class Workspace`, add public methods:

```cpp
    void set_relations_dir(std::filesystem::path dir);
    const mora::SchemaRegistry& schema() const { return *schema_; }

    // Iterate all open documents — used by find-references, workspace-symbol,
    // and goto-def-across-files. Returns raw pointers; documents are owned
    // by docs_.
    std::vector<Document*> all_documents() {
        std::vector<Document*> out;
        out.reserve(docs_.size());
        for (auto& [uri, doc] : docs_) out.push_back(doc.get());
        return out;
    }
```

And private members:

```cpp
    std::filesystem::path relations_dir_;
    std::unique_ptr<mora::SchemaRegistry> schema_;
```

Initialize `schema_` to a default-constructed `SchemaRegistry` in the constructor body (so unit tests that don't touch initialize() still get a valid empty registry). The `set_relations_dir` method should re-load the registry from YAML files under that dir.

- [ ] **Step 2: Implement Workspace::set_relations_dir**

In `src/lsp/workspace.cpp`:

```cpp
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/data/schema_registry.h"
#include <filesystem>

namespace mora::lsp {

Workspace::Workspace()
    : schema_(std::make_unique<mora::SchemaRegistry>()) {}
Workspace::~Workspace() = default;

void Workspace::set_relations_dir(std::filesystem::path dir) {
    relations_dir_ = std::move(dir);
    schema_ = std::make_unique<mora::SchemaRegistry>();
    if (!std::filesystem::is_directory(relations_dir_)) return;
    schema_->load_directory(relations_dir_.string());  // assumes this method exists
}

// ... existing methods unchanged ...

} // namespace mora::lsp
```

If `SchemaRegistry::load_directory(string)` doesn't exist with that exact signature, find the right method by reading `include/mora/data/schema_registry.h` and `src/data/schema_registry.cpp`. The CLI's `mora compile` command loads relations YAML — find that call site (search `src/main.cpp` for `SchemaRegistry`) and mirror it.

- [ ] **Step 3: Document references workspace's SchemaRegistry**

Modify `include/mora/lsp/document.h` so the Document constructor takes a `const Workspace&` reference (or just `const SchemaRegistry&` — pick the cleaner one; if Document needs more workspace state later, take the Workspace ref):

```cpp
namespace mora::lsp {
class Workspace;

class Document {
public:
    Document(const Workspace& ws, std::string uri, std::string text, int version);
    // ... existing API ...

private:
    const Workspace& ws_;
    // ... existing private state ...
};
}
```

In `src/lsp/document.cpp::diagnostics()`, use `ws_.schema()` instead of building a new SchemaRegistry. The exact wiring depends on how `NameResolver`/`TypeChecker` consume the schema — read those constructors. If they don't take a SchemaRegistry today (sema may rely on built-ins only), then this step is just plumbing without semantic effect; that's fine.

- [ ] **Step 4: Update Workspace::open to pass `*this` to the new Document constructor**

In `src/lsp/workspace.cpp`:

```cpp
Document* Workspace::open(std::string uri, std::string text, int version) {
    auto doc = std::make_unique<Document>(*this, uri, std::move(text), version);
    Document* raw = doc.get();
    docs_.emplace(std::move(uri), std::move(doc));
    return raw;
}
```

- [ ] **Step 5: Wire `set_relations_dir` from initialize**

In `src/lsp/handlers/lifecycle.cpp`, modify `on_initialize` to inspect `params.initializationOptions.relationsDir` and `params.rootUri`:

```cpp
Result on_initialize(Workspace& ws, const nlohmann::json& params) {
    // Default: <rootUri>/data/relations/  (matches the project convention)
    std::filesystem::path relations_dir;
    if (params.contains("rootUri") && params["rootUri"].is_string()) {
        std::string root_uri = params["rootUri"].get<std::string>();
        std::string root_path = path_from_uri(root_uri);
        if (!root_path.empty()) {
            relations_dir = std::filesystem::path(root_path) / "data" / "relations";
        }
    }
    // initializationOptions.relationsDir overrides if provided
    if (params.contains("initializationOptions")) {
        const auto& opts = params["initializationOptions"];
        if (opts.contains("relationsDir") && opts["relationsDir"].is_string()) {
            relations_dir = opts["relationsDir"].get<std::string>();
        }
    }
    ws.set_relations_dir(relations_dir);

    return nlohmann::json{
        {"capabilities", server_capabilities()},
        {"serverInfo", {{"name", "mora-lsp"}, {"version", "0.3.0"}}},
    };
}
```

(`path_from_uri` is in `mora/lsp/uri.h` — add the include if not present.)

- [ ] **Step 6: Update workspace tests**

In `tests/lsp/test_workspace.cpp`, add:

```cpp
TEST(LspWorkspace, SetRelationsDirLoadsRegistry) {
    Workspace ws;
    // Empty path: registry stays empty (no exception).
    ws.set_relations_dir(std::filesystem::path{});
    EXPECT_EQ(ws.schema().relation_count(), 0u);

    // Real path from the project: load the actual relations.
    auto root = std::filesystem::current_path();
    while (!root.empty() && !std::filesystem::exists(root / "data" / "relations")) {
        if (root == root.parent_path()) break;
        root = root.parent_path();
    }
    ASSERT_FALSE(root.empty());
    ws.set_relations_dir(root / "data" / "relations");
    EXPECT_GT(ws.schema().relation_count(), 0u);
}
```

If `SchemaRegistry::relation_count()` doesn't exist, use whatever method enumerates registered relations and compare its size. If no enumeration exists, add a `size_t relation_count() const` method to `SchemaRegistry` as part of this task.

- [ ] **Step 7: Build, run all tests, commit**

```bash
xmake build -P . -y
xmake test -P .
```

Expected: existing tests still pass (no regressions); the new `SetRelationsDirLoadsRegistry` test passes.

```bash
git add include/mora/lsp/workspace.h src/lsp/workspace.cpp \
        include/mora/lsp/document.h src/lsp/document.cpp \
        src/lsp/handlers/lifecycle.cpp tests/lsp/test_workspace.cpp
git commit -m "feat(lsp): hoist SchemaRegistry into Workspace, load on initialize"
```

---

### Task 2: Re-parse debouncing

**Goal:** When `didChange` arrives, defer the actual re-parse by ~150 ms so rapid keystrokes coalesce.

**Files:**
- Modify: `src/lsp/server.cpp`
- Modify: `src/lsp/handlers/textsync.cpp`
- Modify: `include/mora/lsp/document.h` (use existing `schedule_reparse` / `reparse_due`)
- Modify: `include/mora/lsp/workspace.h` (add `documents_due_for_reparse(now)` helper)
- Test: `tests/lsp/test_workspace.cpp` (extend)

- [ ] **Step 1: Stop publishing diagnostics from didChange immediately**

In `src/lsp/handlers/textsync.cpp`:

```cpp
Result on_did_change(Workspace& ws, const nlohmann::json& params) {
    const auto& td = params.at("textDocument");
    const auto& changes = params.at("contentChanges");
    if (changes.empty()) return nlohmann::json{};
    const std::string& text = changes.back().at("text").get_ref<const std::string&>();
    std::string uri = td.at("uri").get<std::string>();
    ws.change(uri, text, td.at("version").get<int>());
    // Schedule a reparse 150ms in the future. The run loop polls
    // documents_due_for_reparse() and publishes diagnostics when due.
    auto* doc = ws.get(uri);
    if (doc) {
        doc->schedule_reparse(std::chrono::steady_clock::now() + std::chrono::milliseconds(150));
    }
    return nlohmann::json{};
}
```

(Leave `on_did_open` publishing immediately — initial open should not be debounced; the user just clicked the file open and expects squigglies fast.)

- [ ] **Step 2: Add Workspace::documents_due_for_reparse**

In `include/mora/lsp/workspace.h`:

```cpp
#include <chrono>
#include <vector>

class Workspace {
public:
    // ... existing ...

    // Returns documents whose reparse_after deadline has passed and which
    // still have a stale parse cache. Caller is responsible for calling
    // diagnostics() on each (which clears the staleness) and publishing.
    std::vector<Document*> documents_due_for_reparse(std::chrono::steady_clock::time_point now);
};
```

In `src/lsp/workspace.cpp`:

```cpp
std::vector<Document*> Workspace::documents_due_for_reparse(
    std::chrono::steady_clock::time_point now) {
    std::vector<Document*> out;
    for (auto& [uri, doc] : docs_) {
        if (doc->reparse_due(now)) out.push_back(doc.get());
    }
    return out;
}
```

- [ ] **Step 3: Make the run loop poll**

In `src/lsp/server.cpp`, the current loop blocks on `read_message(std::cin, body)`. To support debouncing without threads, switch to non-blocking stdin polling using `select(2)` on POSIX. Add a helper near the top of the file:

```cpp
#include <sys/select.h>
#include <unistd.h>

namespace {

// Returns true if data is available on stdin within `timeout_ms` ms.
bool stdin_has_data(int timeout_ms) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int r = select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv);
    return r > 0 && FD_ISSET(STDIN_FILENO, &rfds);
}

} // namespace
```

Modify the run-loop body to poll-then-process:

```cpp
while (true) {
    auto now = std::chrono::steady_clock::now();
    // Process any debounced reparses that are due.
    for (Document* doc : ws.documents_due_for_reparse(now)) {
        // Calling diagnostics() runs the parse pipeline (clears staleness)
        // and we then enqueue a publishDiagnostics for it.
        const auto& diags = doc->diagnostics();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& d : diags) arr.push_back(diagnostic_to_json(d));
        ws.enqueue_notification({
            {"jsonrpc", "2.0"},
            {"method", "textDocument/publishDiagnostics"},
            {"params", {
                {"uri", doc->uri()},
                {"diagnostics", arr},
            }},
        });
    }

    // Drain any outgoing notifications.
    for (auto& note : ws.drain_outgoing()) {
        write_message(std::cout, note.dump());
    }

    // Wait briefly for stdin input.
    if (!stdin_has_data(50)) continue;

    std::string body;
    ReadResult r = read_message(std::cin, body);
    // ... existing read/dispatch logic ...
}
```

NOTE: This adds a new include of `<sys/select.h>` and `<unistd.h>`, both POSIX. Windows needs different code (`WaitForSingleObject` on a stdin handle). For v1 we ship POSIX-only LSP. Add a `#ifdef _WIN32` guard at the top of `server.cpp` that errors out if compiled on Windows — the LSP isn't built into the Windows DLL anyway, but the user might one day try to ship `mora lsp` for Windows users. For now, gate the Windows path with a clear error:

```cpp
#ifdef _WIN32
namespace mora::lsp {
int run(int, char**) {
    std::cerr << "mora lsp is not yet supported on Windows.\n";
    return 1;
}
} // namespace mora::lsp
// (rest of file is POSIX-only)
#else
// existing POSIX implementation
#endif
```

- [ ] **Step 4: Add a debouncing test**

In `tests/lsp/test_workspace.cpp`:

```cpp
TEST(LspWorkspace, ReparseDuesAfterDeadline) {
    Workspace ws;
    Document* doc = ws.open("file:///x.mora", "namespace t\n", 1);
    auto now = std::chrono::steady_clock::now();

    // Initial: not due (no reparse scheduled).
    EXPECT_TRUE(ws.documents_due_for_reparse(now).empty());

    // Schedule a reparse 50ms in the future.
    doc->schedule_reparse(now + std::chrono::milliseconds(50));
    EXPECT_TRUE(ws.documents_due_for_reparse(now).empty()) << "not due yet";

    // After deadline: due.
    auto later = now + std::chrono::milliseconds(60);
    auto due = ws.documents_due_for_reparse(later);
    EXPECT_EQ(due.size(), 1u);
    EXPECT_EQ(due[0], doc);

    // After diagnostics() is called (clears stale flag), no longer due.
    (void)doc->diagnostics();
    EXPECT_TRUE(ws.documents_due_for_reparse(later).empty());
}
```

The `Document::diagnostics()` function already runs the pipeline and clears `cache_stale_`; the existing `Document::reparse_due` checks both `cache_stale_` and the deadline.

- [ ] **Step 5: Build and run all tests**

```bash
xmake build -P . -y && xmake test -P .
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/lsp/server.cpp src/lsp/handlers/textsync.cpp \
        include/mora/lsp/workspace.h src/lsp/workspace.cpp \
        tests/lsp/test_workspace.cpp
git commit -m "feat(lsp): debounce reparse on didChange via select() poll"
```

---

### Task 3: EditorIdRegistry — best-effort atom resolution

**Goal:** Optionally load editor-IDs and FormIDs from the user's Skyrim data folder. When `mora.dataDir` is unset or invalid, hover handlers gracefully degrade to "FormID unknown".

**Files:**
- Create: `include/mora/lsp/editor_id_registry.h`
- Create: `src/lsp/editor_id_registry.cpp`
- Create: `tests/lsp/test_editor_id_registry.cpp`
- Modify: `include/mora/lsp/workspace.h` (add `editor_ids()` accessor + `set_data_dir`)
- Modify: `src/lsp/workspace.cpp`
- Modify: `src/lsp/handlers/lifecycle.cpp` (read `initializationOptions.dataDir`)

- [ ] **Step 1: Define the API**

`include/mora/lsp/editor_id_registry.h`:

```cpp
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mora::lsp {

struct EditorIdInfo {
    uint32_t form_id = 0;       // resolved FormID (e.g. 0x0001BCB1)
    std::string source_plugin;  // "Skyrim.esm", "Dawnguard.esm", ...
    std::string record_type;    // "FACT", "WEAP", etc.
};

class EditorIdRegistry {
public:
    EditorIdRegistry();

    // Replace the registry contents by scanning ESPs under `data_dir`.
    // Best-effort: silently no-ops if the path doesn't exist or is empty.
    void scan(const std::filesystem::path& data_dir);

    // Lookup an editor ID. Returns nullopt if not loaded or not found.
    std::optional<EditorIdInfo> lookup(std::string_view editor_id) const;

    // True if scan() has loaded at least one ESP.
    bool has_data() const { return !entries_.empty(); }

private:
    std::unordered_map<std::string, EditorIdInfo> entries_;
};

} // namespace mora::lsp
```

- [ ] **Step 2: Stub implementation (no actual ESP parsing yet)**

`src/lsp/editor_id_registry.cpp`:

```cpp
#include "mora/lsp/editor_id_registry.h"

namespace mora::lsp {

EditorIdRegistry::EditorIdRegistry() = default;

void EditorIdRegistry::scan(const std::filesystem::path& data_dir) {
    entries_.clear();
    if (data_dir.empty() || !std::filesystem::is_directory(data_dir)) return;
    // Phase-3 v1: this is a stub. A future task will use mora's existing ESP
    // reader (src/esp/) to walk the data dir and populate `entries_` with
    // FACT / WEAP / NPC_ / etc. records that have an EDID subrecord. For
    // now we leave entries_ empty; hover degrades to "FormID unknown".
    // The point of the stub is to wire all the plumbing so we can drop in
    // the real scan in a follow-up without touching call sites.
    (void)data_dir;
}

std::optional<EditorIdInfo> EditorIdRegistry::lookup(std::string_view editor_id) const {
    auto it = entries_.find(std::string(editor_id));
    if (it == entries_.end()) return std::nullopt;
    return it->second;
}

} // namespace mora::lsp
```

This is a deliberate v1 cut: the registry's API and plumbing are real, so callers (Task 11 hover handler) work today; the actual ESP scan is left for a follow-up. Hover for atoms will always show "Atom not resolved (data dir not loaded)". When the follow-up fills in `scan`, hover automatically improves.

- [ ] **Step 3: Tests**

`tests/lsp/test_editor_id_registry.cpp`:

```cpp
#include <gtest/gtest.h>
#include "mora/lsp/editor_id_registry.h"

using mora::lsp::EditorIdRegistry;

TEST(EditorIdRegistry, EmptyByDefault) {
    EditorIdRegistry r;
    EXPECT_FALSE(r.has_data());
    EXPECT_FALSE(r.lookup("BanditFaction").has_value());
}

TEST(EditorIdRegistry, ScanWithMissingDirIsNoOp) {
    EditorIdRegistry r;
    r.scan("/nonexistent/path/xyz");
    EXPECT_FALSE(r.has_data());
    EXPECT_FALSE(r.lookup("BanditFaction").has_value());
}

TEST(EditorIdRegistry, ScanWithEmptyPathIsNoOp) {
    EditorIdRegistry r;
    r.scan(std::filesystem::path{});
    EXPECT_FALSE(r.has_data());
}
```

(More substantive tests will land when the real ESP scan implementation does.)

- [ ] **Step 4: Hook into Workspace**

In `include/mora/lsp/workspace.h`:

```cpp
#include "mora/lsp/editor_id_registry.h"

class Workspace {
public:
    // ... existing ...
    void set_data_dir(std::filesystem::path dir);
    const EditorIdRegistry& editor_ids() const { return *editor_ids_; }

private:
    // ... existing ...
    std::unique_ptr<EditorIdRegistry> editor_ids_;
};
```

In `src/lsp/workspace.cpp`:

```cpp
Workspace::Workspace()
    : schema_(std::make_unique<mora::SchemaRegistry>()),
      editor_ids_(std::make_unique<EditorIdRegistry>()) {}

void Workspace::set_data_dir(std::filesystem::path dir) {
    editor_ids_->scan(dir);
}
```

- [ ] **Step 5: Wire from initialize**

In `src/lsp/handlers/lifecycle.cpp`'s `on_initialize`, after reading `relationsDir`:

```cpp
    if (params.contains("initializationOptions")) {
        const auto& opts = params["initializationOptions"];
        // ... existing relationsDir handling ...
        if (opts.contains("dataDir") && opts["dataDir"].is_string()) {
            ws.set_data_dir(opts["dataDir"].get<std::string>());
        }
    }
```

- [ ] **Step 6: Build, run, commit**

```bash
xmake build -P . -y && xmake test -P .
```

```bash
git add include/mora/lsp/editor_id_registry.h src/lsp/editor_id_registry.cpp \
        tests/lsp/test_editor_id_registry.cpp \
        include/mora/lsp/workspace.h src/lsp/workspace.cpp \
        src/lsp/handlers/lifecycle.cpp
git commit -m "feat(lsp): EditorIdRegistry stub (atom resolution plumbing)"
```

---

### Task 4: SymbolIndex — types and skeleton

**Goal:** A per-document index mapping `(line, col) → SymbolEntry` that the semantic-feature handlers query.

**Files:**
- Create: `include/mora/lsp/symbol_index.h`
- Create: `src/lsp/symbol_index.cpp`

- [ ] **Step 1: Define the types**

`include/mora/lsp/symbol_index.h`:

```cpp
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mora/core/source_location.h"
#include "mora/core/string_pool.h"

namespace mora {
class Module;
class NameResolver;
class TypeChecker;
}

namespace mora::lsp {

enum class SymbolKind : uint8_t {
    RuleHead,        // The name in `bandit(NPC):`
    RuleCall,        // A call site, e.g. the `bandit` in `bandit(X)` inside a rule body
    Relation,        // A built-in relation reference, e.g. `weapon` in `form/weapon(W)`
    Atom,            // `@BanditFaction`
    VariableBinding, // First-occurrence of a variable in a rule
    VariableUse,     // Subsequent occurrence of a variable
    Namespace,       // Top of file: `namespace test.example`
};

struct SymbolEntry {
    SymbolKind  kind;
    SourceSpan  span;       // Span of just this token (not the surrounding construct)
    StringId    name;       // The symbol's name (interned)
    std::string ns_path;    // For Relation: "form" / "ref" / etc. For RuleCall: empty.
    // For variables, the binding-site span (same as span if this entry IS the binding).
    SourceSpan  binding_span;
};

class SymbolIndex {
public:
    // Build the index by walking a fully-resolved Module. Position
    // information for variables is taken from the TypeChecker's
    // var_def_spans_ (exposed via the new public accessor).
    void build(const mora::Module& mod,
               const mora::NameResolver& resolver,
               const mora::TypeChecker& tc,
               const mora::StringPool& pool);

    // Find the entry whose span contains (line, col). Returns nullptr if
    // nothing matches. Lines/cols are 1-based (mora convention).
    const SymbolEntry* find_at(uint32_t line, uint32_t col) const;

    // All entries — used by find-references / document-symbols.
    const std::vector<SymbolEntry>& entries() const { return entries_; }

    void clear() { entries_.clear(); }

private:
    std::vector<SymbolEntry> entries_;
};

} // namespace mora::lsp
```

- [ ] **Step 2: Skeleton implementation (build does nothing yet — Task 5 fills it)**

`src/lsp/symbol_index.cpp`:

```cpp
#include "mora/lsp/symbol_index.h"
#include "mora/ast/ast.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"

namespace mora::lsp {

void SymbolIndex::build(const mora::Module& /*mod*/,
                        const mora::NameResolver& /*resolver*/,
                        const mora::TypeChecker& /*tc*/,
                        const mora::StringPool& /*pool*/) {
    entries_.clear();
    // Task 5 fills this in.
}

const SymbolEntry* SymbolIndex::find_at(uint32_t line, uint32_t col) const {
    for (const auto& e : entries_) {
        if (line < e.span.start_line || line > e.span.end_line) continue;
        if (line == e.span.start_line && col < e.span.start_col) continue;
        if (line == e.span.end_line   && col > e.span.end_col)   continue;
        return &e;
    }
    return nullptr;
}

} // namespace mora::lsp
```

- [ ] **Step 3: Build, commit (no test yet — Task 5 adds tests)**

```bash
xmake build -P . -y mora_lib
```

```bash
git add include/mora/lsp/symbol_index.h src/lsp/symbol_index.cpp
git commit -m "feat(lsp): SymbolIndex skeleton (types + position-lookup)"
```

---

### Task 5: SymbolIndex — AST walker

**Goal:** `SymbolIndex::build` walks the AST and populates entries for every named token.

**Files:**
- Modify: `src/lsp/symbol_index.cpp`
- Modify: `include/mora/sema/type_checker.h` (expose var_def_spans)
- Modify: `include/mora/sema/name_resolver.h` (expose facts)
- Create: `tests/lsp/test_symbol_index.cpp`

- [ ] **Step 1: Expose sema internals**

In `include/mora/sema/type_checker.h`, add a public accessor:

```cpp
public:
    // ... existing ...
    const std::unordered_map<uint32_t, SourceSpan>& variable_definition_spans() const {
        return var_def_spans_;
    }
```

In `include/mora/sema/name_resolver.h`:

```cpp
public:
    // ... existing ...
    const std::unordered_map<uint32_t, FactSignature>& facts() const { return facts_; }
    const std::unordered_map<uint32_t, bool>& rules() const { return rules_; }
```

These are read-only views; callers must not mutate.

- [ ] **Step 2: Write the failing tests**

`tests/lsp/test_symbol_index.cpp`:

```cpp
#include <gtest/gtest.h>
#include "mora/lsp/symbol_index.h"
#include "mora/lsp/document.h"
#include "mora/lsp/workspace.h"

using namespace mora::lsp;

namespace {
// Helper: open a document containing `src`, force a parse, return the index.
const SymbolIndex& parse_and_index(Workspace& ws, const std::string& src) {
    Document* doc = ws.open("file:///x.mora", src, 1);
    (void)doc->diagnostics();  // forces parse + index build
    return doc->symbol_index();
}
} // namespace

TEST(LspSymbolIndex, FindsRuleHead) {
    Workspace ws;
    const auto& idx = parse_and_index(ws,
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n");
    // bandit head is on line 2, starting at col 1.
    const auto* e = idx.find_at(2, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->kind, SymbolKind::RuleHead);
}

TEST(LspSymbolIndex, FindsRelationRefAndNamespace) {
    Workspace ws;
    const auto& idx = parse_and_index(ws,
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n");
    // form/npc — the `npc` portion is a Relation entry.
    // The exact column depends on indentation (4 spaces). `form` starts at
    // col 5; the slash is col 9; `npc` starts at col 10.
    const auto* relation = idx.find_at(3, 10);
    ASSERT_NE(relation, nullptr);
    EXPECT_EQ(relation->kind, SymbolKind::Relation);
    EXPECT_EQ(relation->ns_path, "form");
}

TEST(LspSymbolIndex, FindsVariableBindingAndUse) {
    Workspace ws;
    const auto& idx = parse_and_index(ws,
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n");
    // The NPC inside the head args is the BINDING (first occurrence).
    const auto* binding = idx.find_at(2, 8);   // bandit(NPC)
                                                //        ^col 8
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->kind, SymbolKind::VariableBinding);

    // The NPC inside form/npc(NPC) is a USE.
    const auto* use = idx.find_at(3, 14);
    ASSERT_NE(use, nullptr);
    EXPECT_EQ(use->kind, SymbolKind::VariableUse);
    // Use's binding_span should point back at the binding (line 2).
    EXPECT_EQ(use->binding_span.start_line, 2u);
}

TEST(LspSymbolIndex, FindsAtom) {
    Workspace ws;
    const auto& idx = parse_and_index(ws,
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/keyword(NPC, @BanditFaction)\n");
    // @BanditFaction starts roughly at col 23 on line 3; just scan a range.
    const SymbolEntry* atom = nullptr;
    for (const auto& e : idx.entries()) {
        if (e.kind == SymbolKind::Atom) { atom = &e; break; }
    }
    ASSERT_NE(atom, nullptr);
}
```

NOTE: column counts depend on exact tokenization. After the implementation works, you may need to adjust the columns (the lexer's span columns are 1-based and refer to the first character of the token).

- [ ] **Step 3: Implement the AST walker**

`src/lsp/symbol_index.cpp` — replace the stub `build()`:

```cpp
#include "mora/lsp/symbol_index.h"
#include "mora/ast/ast.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"

namespace mora::lsp {

namespace {

// Walk an Expr looking for variable references and atoms. `bindings_seen`
// is per-rule and tracks which variables have already been seen so we can
// distinguish binding vs. use.
void walk_expr(const mora::Expr& expr,
               const mora::TypeChecker& tc,
               std::unordered_map<uint32_t, mora::SourceSpan>& bindings_seen,
               std::vector<SymbolEntry>& out);

void emit_variable(const mora::Variable& var,
                   const mora::SourceSpan& span,
                   const mora::TypeChecker& tc,
                   std::unordered_map<uint32_t, mora::SourceSpan>& bindings_seen,
                   std::vector<SymbolEntry>& out) {
    SymbolEntry e;
    e.span = span;
    e.name = var.name;
    auto def_it = bindings_seen.find(var.name.value);
    if (def_it == bindings_seen.end()) {
        e.kind = SymbolKind::VariableBinding;
        e.binding_span = span;
        bindings_seen.emplace(var.name.value, span);
    } else {
        e.kind = SymbolKind::VariableUse;
        e.binding_span = def_it->second;
    }
    out.push_back(e);
}

void walk_expr(const mora::Expr& expr,
               const mora::TypeChecker& tc,
               std::unordered_map<uint32_t, mora::SourceSpan>& bindings_seen,
               std::vector<SymbolEntry>& out) {
    if (const auto* var = std::get_if<mora::Variable>(&expr.data)) {
        emit_variable(*var, expr.span, tc, bindings_seen, out);
    } else if (const auto* atom = std::get_if<mora::AtomLiteral>(&expr.data)) {
        SymbolEntry e;
        e.kind = SymbolKind::Atom;
        e.span = expr.span;
        e.name = atom->editor_id;  // adapt to the actual AtomLiteral field name
        out.push_back(e);
    }
    // Other expression kinds (BinaryOp, IntLiteral, etc.) recurse:
    else if (const auto* bin = std::get_if<mora::BinaryExpr>(&expr.data)) {
        if (bin->left)  walk_expr(*bin->left,  tc, bindings_seen, out);
        if (bin->right) walk_expr(*bin->right, tc, bindings_seen, out);
    }
    // Add other variant cases as needed by inspecting include/mora/ast/ast.h.
}

void walk_clause(const mora::Clause& clause,
                 const mora::NameResolver& /*resolver*/,
                 const mora::TypeChecker& tc,
                 std::unordered_map<uint32_t, mora::SourceSpan>& bindings_seen,
                 std::vector<SymbolEntry>& out) {
    if (const auto* fp = std::get_if<mora::FactPattern>(&clause.data)) {
        // Emit a Relation entry for fp->name. Its span is approximately
        // clause.span up to the '(' — but we don't have that exact span.
        // Use the fact-pattern's name_span if it exists; otherwise approximate.
        SymbolEntry e;
        e.kind = SymbolKind::Relation;     // could refine to RuleCall by checking resolver.facts()
        e.span = fp->name_span;            // adapt — see ast.h for exact field
        e.name = fp->name;
        e.ns_path = fp->namespace_path.empty() ? "" : pool_to_string(fp->namespace_path);
        out.push_back(e);
        for (const auto& arg : fp->args) {
            walk_expr(arg, tc, bindings_seen, out);
        }
    }
    // Other Clause variants (GuardClause, OrClause, etc.) — recurse into their exprs.
}

} // namespace

void SymbolIndex::build(const mora::Module& mod,
                        const mora::NameResolver& resolver,
                        const mora::TypeChecker& tc,
                        const mora::StringPool& /*pool*/) {
    entries_.clear();

    // 1. Namespace.
    if (mod.ns) {
        SymbolEntry e;
        e.kind = SymbolKind::Namespace;
        e.span = mod.ns->span;
        e.name = mod.ns->name;
        entries_.push_back(e);
    }

    // 2. Per rule: head + bound variables + body clauses.
    for (const auto& rule : mod.rules) {
        // Rule head emit.
        SymbolEntry head;
        head.kind = SymbolKind::RuleHead;
        head.span = rule.span;       // adjust if rule has a separate name_span
        head.name = rule.name;
        entries_.push_back(head);

        // Per-rule binding tracker.
        std::unordered_map<uint32_t, mora::SourceSpan> bindings_seen;

        // Head args are bindings.
        for (const auto& arg : rule.head_args) {
            walk_expr(arg, tc, bindings_seen, entries_);
        }

        // Body clauses.
        for (const auto& cl : rule.body) {
            walk_clause(cl, resolver, tc, bindings_seen, entries_);
        }

        // Effects + conditional effects similarly:
        for (const auto& eff : rule.effects) {
            // Adapt to the Effect struct's actual shape.
            (void)eff;
        }
    }
}

const SymbolEntry* SymbolIndex::find_at(uint32_t line, uint32_t col) const {
    // ... existing implementation unchanged ...
}

} // namespace mora::lsp
```

The above sketches the walker — adapt to the actual AST shape by reading `include/mora/ast/ast.h`. Specifically:

- `Variable`, `AtomLiteral`, `BinaryExpr` may have different names (e.g. `IdentifierExpr`, `EditorIdRef`).
- `FactPattern` may have a `name_span` field, or just `span` covering the full pattern. If only `span` is available, use it for now (the LSP can refine later).
- `mod.ns` may be `std::optional<NamespaceDecl>` or a plain `NamespaceDecl` with sentinel — handle both.

If a field doesn't exist and adding it requires non-trivial AST plumbing, leave that part as a TODO comment in `symbol_index.cpp` and skip the corresponding tests. Report it as `DONE_WITH_CONCERNS`.

- [ ] **Step 4: Wire SymbolIndex into Document**

In `include/mora/lsp/document.h`:

```cpp
#include "mora/lsp/symbol_index.h"
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"

class Document {
public:
    // ... existing ...

    // Phase-3 accessors. All return references valid until the next reparse.
    const SymbolIndex&        symbol_index() const { return index_; }
    const mora::StringPool&   pool() const         { return *pool_; }
    const mora::Module&       module() const       { return *module_; }
    // Lookup a Rule by its interned name. Returns nullptr if not found.
    const mora::Rule*         find_rule_by_name(mora::StringId name) const;

private:
    // ... existing ...
    // Phase-3: keep the parse outputs alive past diagnostics() so semantic
    // queries can read them. These are populated by diagnostics() on every
    // reparse and replaced wholesale.
    std::unique_ptr<mora::StringPool> pool_;
    std::unique_ptr<mora::Module>     module_;
    SymbolIndex                        index_;
};
```

Implement `find_rule_by_name` in `src/lsp/document.cpp` as a linear scan over `module_->rules`. Initialize `pool_` and `module_` to default-constructed instances in Document's constructor body so accessors are safe before the first parse.

Update the existing `diagnostics()` body so that after the parse pipeline runs, instead of letting StringPool / Module die at end of scope, it writes them into `pool_` / `module_` (via `std::make_unique<...>(std::move(...))` or by constructing them on the heap from the start). Then build the index against those persistent objects.

In `src/lsp/document.cpp::diagnostics()`, after sema runs:

```cpp
    // Build the symbol index for LSP semantic features.
    index_.build(mod, resolver, tc, pool);
```

(Place after the typecheck call but before draining diagnostics — order doesn't actually matter for correctness, just keep it inside the `cache_stale_` block so it only runs on reparse.)

- [ ] **Step 5: Run tests**

```bash
xmake build -P . -y test_symbol_index && xmake run -P . test_symbol_index
```

If a test fails because of a column mismatch, inspect the actual `entries()` and adjust the test's expected line/col to match. Don't change the implementation to fit the test's wrong-but-plausible column.

If a test fails because the AST doesn't expose what the walker needs, it's BLOCKED — report what you found and ask for guidance (don't add invasive AST changes without consultation).

- [ ] **Step 6: Commit**

```bash
git add include/mora/sema/type_checker.h include/mora/sema/name_resolver.h \
        src/lsp/symbol_index.cpp \
        include/mora/lsp/document.h src/lsp/document.cpp \
        tests/lsp/test_symbol_index.cpp
git commit -m "feat(lsp): SymbolIndex AST walker (rules/relations/vars/atoms)"
```

---

### Task 6: textDocument/hover handler

**Goal:** Hover yields markdown for whatever symbol the cursor sits on.

**Files:**
- Create: `src/lsp/handlers/hover.cpp`
- Create: `tests/lsp/test_hover.cpp`
- Modify: `src/lsp/server.cpp` (forward-declare + register)

- [ ] **Step 1: Write the failing tests**

`tests/lsp/test_hover.cpp`:

```cpp
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"

namespace mora::lsp {
void register_hover_handler(Dispatcher&);
}

using namespace mora::lsp;
using nlohmann::json;

namespace {
json req(int id, std::string_view method, json params) {
    return {{"jsonrpc","2.0"},{"id",id},{"method",std::string(method)},{"params",params}};
}
json hover_params(std::string_view uri, int line, int character) {
    return {
        {"textDocument", {{"uri", std::string(uri)}}},
        {"position", {{"line", line}, {"character", character}}},
    };
}
} // namespace

TEST(LspHover, RuleHeadShowsSignatureAndDocComment) {
    Workspace ws;
    Dispatcher d; register_hover_handler(d);
    ws.open("file:///x.mora",
        "namespace t\n"
        "# Tags every NPC in the bandit faction.\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n", 1);

    // hover on `bandit` (line 2 in 0-based = our line 3 in 1-based).
    auto reply = d.dispatch(ws, req(1, "textDocument/hover",
        hover_params("file:///x.mora", 2, 1)));
    ASSERT_TRUE(reply.has_value());
    ASSERT_TRUE(reply->contains("result"));
    auto md = (*reply)["result"]["contents"]["value"].get<std::string>();
    EXPECT_NE(md.find("bandit("), std::string::npos);
    EXPECT_NE(md.find("Tags every NPC"), std::string::npos);
}

TEST(LspHover, AtomFallbackWhenNoDataDir) {
    Workspace ws;
    Dispatcher d; register_hover_handler(d);
    ws.open("file:///x.mora",
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/keyword(NPC, @BanditFaction)\n", 1);

    // Hover roughly on @BanditFaction.
    auto reply = d.dispatch(ws, req(1, "textDocument/hover",
        hover_params("file:///x.mora", 2, 24)));
    ASSERT_TRUE(reply.has_value());
    if (reply->contains("result") && !(*reply)["result"].is_null()) {
        auto md = (*reply)["result"]["contents"]["value"].get<std::string>();
        EXPECT_NE(md.find("@BanditFaction"), std::string::npos);
        // Without dataDir, we expect the FormID-unknown text.
        EXPECT_NE(md.find("not resolved"), std::string::npos);
    }
}

TEST(LspHover, NoHoverOnEmptyPosition) {
    Workspace ws;
    Dispatcher d; register_hover_handler(d);
    ws.open("file:///x.mora", "namespace t\n", 1);
    // Hover on whitespace — should return null result, not error.
    auto reply = d.dispatch(ws, req(1, "textDocument/hover",
        hover_params("file:///x.mora", 99, 99)));
    ASSERT_TRUE(reply.has_value());
    EXPECT_TRUE((*reply)["result"].is_null());
}
```

- [ ] **Step 2: Implement the handler**

`src/lsp/handlers/hover.cpp`:

```cpp
#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/lsp/symbol_index.h"
#include "mora/lsp/editor_id_registry.h"
#include "mora/data/schema_registry.h"

namespace mora::lsp {

namespace {

// 0-based LSP coords → 1-based mora coords.
uint32_t to_one_based(int v) { return v < 0 ? 1 : static_cast<uint32_t>(v + 1); }

std::string hover_for_rule(const SymbolEntry& e, const Document& doc) {
    // Look up the Rule node from the Document's parsed module to get
    // doc_comment + signature.
    const auto* rule = doc.find_rule_by_name(e.name);
    std::string md = "```mora\n";
    md += /* signature */ std::string("rule ") + (rule ? "..." : "") + "\n```\n";
    if (rule && rule->doc_comment) {
        md += "\n";
        md += *rule->doc_comment;
    }
    return md;
}

std::string hover_for_relation(const SymbolEntry& e, const Workspace& ws,
                               const StringPool& pool) {
    std::string name = std::string(pool.get(e.name));
    std::string full = e.ns_path + "/" + name;
    std::string md = "```mora\n" + full + "\n```\n";
    auto* schema = ws.schema().lookup(e.name);  // adapt to actual API
    if (schema) {
        // Show docs string from YAML if present.
        // (RelationSchema may carry it; if not, leave the bare signature.)
    }
    return md;
}

std::string hover_for_atom(const SymbolEntry& e, const Workspace& ws,
                           const StringPool& pool) {
    std::string id = std::string(pool.get(e.name));
    std::string md = "```mora\n@" + id + "\n```\n";
    auto info = ws.editor_ids().lookup(id);
    if (info) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "0x%08X", info->form_id);
        md += "\nEditor ID `" + id + "` → FormID `" + buf + "` from `"
            + info->source_plugin + "` (record `" + info->record_type + "`)\n";
    } else {
        md += "\n⚠ Atom not resolved (data dir not loaded).\n";
    }
    return md;
}

std::string hover_for_variable(const SymbolEntry& e, const StringPool& pool) {
    std::string name = std::string(pool.get(e.name));
    std::string md = "```mora\n" + name + "\n```\n";
    if (e.kind == SymbolKind::VariableUse) {
        md += "\nBound on line " + std::to_string(e.binding_span.start_line) + ".\n";
    } else {
        md += "\n(Binding site)\n";
    }
    return md;
}

Result on_hover(Workspace& ws, const nlohmann::json& params) {
    std::string uri = params.at("textDocument").at("uri").get<std::string>();
    int line_zb   = params.at("position").at("line").get<int>();
    int char_zb   = params.at("position").at("character").get<int>();
    Document* doc = ws.get(uri);
    if (!doc) return nlohmann::json{nullptr};
    const auto* entry = doc->symbol_index().find_at(to_one_based(line_zb),
                                                    to_one_based(char_zb));
    if (!entry) return nlohmann::json{nullptr};

    std::string md;
    switch (entry->kind) {
        case SymbolKind::RuleHead:
        case SymbolKind::RuleCall:        md = hover_for_rule(*entry, *doc); break;
        case SymbolKind::Relation:        md = hover_for_relation(*entry, ws, doc->pool()); break;
        case SymbolKind::Atom:            md = hover_for_atom(*entry, ws, doc->pool()); break;
        case SymbolKind::VariableBinding:
        case SymbolKind::VariableUse:     md = hover_for_variable(*entry, doc->pool()); break;
        case SymbolKind::Namespace:       return nlohmann::json{nullptr};
    }

    return nlohmann::json{
        {"contents", {{"kind", "markdown"}, {"value", md}}},
    };
}

} // namespace

void register_hover_handler(Dispatcher& d) {
    d.on_request("textDocument/hover", on_hover);
}

} // namespace mora::lsp
```

The implementation references `doc.find_rule_by_name`, `doc.pool()`, `ws.schema().lookup` — these may not exist yet. Add them as needed:

- `Document::pool()` returns the `const StringPool&` from the latest parse. If Document throws away the pool after each parse (current behavior), keep it as a member instead.
- `Document::find_rule_by_name(StringId)` returns `const Rule*` or nullptr.

Both are small additions. Make them.

- [ ] **Step 3: Wire registration**

In `src/lsp/server.cpp`, forward-declare and register:

```cpp
void register_hover_handler(Dispatcher&);
```

```cpp
register_hover_handler(dispatcher);
```

- [ ] **Step 4: Build, run tests**

```bash
xmake build -P . -y && xmake run -P . test_hover
```

Expected: 3 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/lsp/handlers/hover.cpp tests/lsp/test_hover.cpp \
        src/lsp/server.cpp \
        include/mora/lsp/document.h src/lsp/document.cpp
git commit -m "feat(lsp): textDocument/hover for rules, relations, atoms, vars"
```

---

### Task 7: textDocument/definition handler

**Goal:** F12 jumps to the right place: rule call → rule head; relation → YAML line; variable → binding site.

**Files:**
- Create: `src/lsp/handlers/definition.cpp`
- Create: `tests/lsp/test_definition.cpp`
- Modify: `src/lsp/server.cpp`

- [ ] **Step 1: Write the failing tests**

`tests/lsp/test_definition.cpp`:

```cpp
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {
void register_definition_handler(Dispatcher&);
}

using namespace mora::lsp;
using nlohmann::json;

namespace {
json req(int id, std::string_view method, json params) {
    return {{"jsonrpc","2.0"},{"id",id},{"method",std::string(method)},{"params",params}};
}
json position_params(std::string_view uri, int line, int character) {
    return {
        {"textDocument", {{"uri", std::string(uri)}}},
        {"position", {{"line", line}, {"character", character}}},
    };
}
} // namespace

TEST(LspDefinition, VariableUseJumpsToBinding) {
    Workspace ws;
    Dispatcher d; register_definition_handler(d);
    ws.open("file:///x.mora",
        "namespace t\n"
        "bandit(NPC):\n"          // line 1 (0-based) = line 2 (1-based); NPC binds at col ~7
        "    form/npc(NPC)\n",    // line 2; NPC use at col ~13
        1);

    auto reply = d.dispatch(ws, req(1, "textDocument/definition",
        position_params("file:///x.mora", 2, 13)));
    ASSERT_TRUE(reply.has_value());
    ASSERT_TRUE(reply->contains("result"));
    auto& result = (*reply)["result"];
    ASSERT_TRUE(result.is_array() || (result.contains("uri") && result.contains("range")));
    if (result.is_array()) {
        ASSERT_FALSE(result.empty());
        EXPECT_EQ(result[0]["range"]["start"]["line"], 1);  // binding is on 0-based line 1
    } else {
        EXPECT_EQ(result["range"]["start"]["line"], 1);
    }
}

TEST(LspDefinition, NoMatchReturnsNull) {
    Workspace ws;
    Dispatcher d; register_definition_handler(d);
    ws.open("file:///x.mora", "namespace t\n", 1);
    auto reply = d.dispatch(ws, req(1, "textDocument/definition",
        position_params("file:///x.mora", 99, 99)));
    ASSERT_TRUE(reply.has_value());
    EXPECT_TRUE((*reply)["result"].is_null());
}
```

(Add tests for RuleCall→RuleHead and Relation→YAML once the basic plumbing works. Cross-file rule-call tests require putting two documents in the workspace.)

- [ ] **Step 2: Implement**

`src/lsp/handlers/definition.cpp`:

```cpp
#include <nlohmann/json.hpp>
#include <filesystem>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/lsp/symbol_index.h"
#include "mora/lsp/uri.h"

namespace mora::lsp {

namespace {

uint32_t to_one_based(int v) { return v < 0 ? 1 : static_cast<uint32_t>(v + 1); }

nlohmann::json span_to_range(const mora::SourceSpan& span) {
    auto zb = [](uint32_t one) -> int { return one == 0 ? 0 : static_cast<int>(one - 1); };
    return {
        {"start", {{"line", zb(span.start_line)}, {"character", zb(span.start_col)}}},
        {"end",   {{"line", zb(span.end_line)},   {"character", zb(span.end_col)}}},
    };
}

nlohmann::json location(const std::string& uri, const mora::SourceSpan& span) {
    return {
        {"uri", uri},
        {"range", span_to_range(span)},
    };
}

Result on_definition(Workspace& ws, const nlohmann::json& params) {
    std::string uri = params.at("textDocument").at("uri").get<std::string>();
    int line_zb = params.at("position").at("line").get<int>();
    int char_zb = params.at("position").at("character").get<int>();
    Document* doc = ws.get(uri);
    if (!doc) return nlohmann::json{nullptr};
    const auto* entry = doc->symbol_index().find_at(to_one_based(line_zb),
                                                    to_one_based(char_zb));
    if (!entry) return nlohmann::json{nullptr};

    switch (entry->kind) {
        case SymbolKind::VariableUse:
            return location(uri, entry->binding_span);
        case SymbolKind::RuleCall: {
            // Find the rule head with the same name in this doc OR any open doc.
            for (Document* d : ws.all_documents()) {
                if (const auto* rule = d->find_rule_by_name(entry->name)) {
                    return location(d->uri(), rule->span);
                }
            }
            return nlohmann::json{nullptr};
        }
        case SymbolKind::Relation: {
            // Jump to the YAML file, line 1 (we don't have per-relation
            // line numbers in the registry; pointing at the file is enough).
            auto& schema = ws.schema();
            auto path = schema.yaml_path_for(entry->name, entry->ns_path);
            if (!path.empty()) {
                mora::SourceSpan zero{};
                zero.start_line = 1; zero.start_col = 1;
                zero.end_line = 1;   zero.end_col = 1;
                return location(uri_from_path(path), zero);
            }
            return nlohmann::json{nullptr};
        }
        case SymbolKind::RuleHead:
        case SymbolKind::VariableBinding:
        case SymbolKind::Atom:
        case SymbolKind::Namespace:
            return nlohmann::json{nullptr};
    }
    return nlohmann::json{nullptr};
}

} // namespace

void register_definition_handler(Dispatcher& d) {
    d.on_request("textDocument/definition", on_definition);
}

} // namespace mora::lsp
```

References to `Workspace::all_documents()` and `SchemaRegistry::yaml_path_for` may not exist — add them:

- `Workspace::all_documents() -> std::vector<Document*>` — iterate `docs_`.
- `SchemaRegistry::yaml_path_for(StringId name, std::string_view ns) -> std::string` — return the path of the YAML file that defined this relation, or "" if unknown. The `SchemaRegistry::load_directory` function should record the source path per relation.

If recording source paths is non-trivial in the existing SchemaRegistry, leave Relation→YAML returning null for now and add to follow-ups. RuleCall→RuleHead and VariableUse→Binding are sufficient v1 value.

- [ ] **Step 3: Wire and test**

In `server.cpp`:

```cpp
void register_definition_handler(Dispatcher&);
register_definition_handler(dispatcher);
```

```bash
xmake build -P . -y && xmake run -P . test_definition
```

- [ ] **Step 4: Commit**

```bash
git add src/lsp/handlers/definition.cpp tests/lsp/test_definition.cpp \
        src/lsp/server.cpp include/mora/lsp/workspace.h src/lsp/workspace.cpp
git commit -m "feat(lsp): textDocument/definition for rules, relations, vars"
```

---

### Task 8: textDocument/references

**Goal:** Find-all-references for rules, relations, atoms, and variables across the workspace.

**Files:**
- Create: `src/lsp/handlers/references.cpp`
- Create: `tests/lsp/test_references.cpp`
- Modify: `src/lsp/server.cpp`

- [ ] **Step 1: Write the failing test**

`tests/lsp/test_references.cpp`:

```cpp
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {
void register_references_handler(Dispatcher&);
}

using namespace mora::lsp;
using nlohmann::json;

namespace {
json req(int id, std::string_view method, json params) {
    return {{"jsonrpc","2.0"},{"id",id},{"method",std::string(method)},{"params",params}};
}
} // namespace

TEST(LspReferences, FindsAllVariableOccurrencesInRule) {
    Workspace ws;
    Dispatcher d; register_references_handler(d);
    ws.open("file:///x.mora",
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n"
        "    form/level(NPC, _)\n", 1);

    auto reply = d.dispatch(ws, req(1, "textDocument/references", {
        {"textDocument", {{"uri", "file:///x.mora"}}},
        {"position",     {{"line", 1}, {"character", 7}}},  // NPC binding
        {"context",      {{"includeDeclaration", true}}},
    }));
    ASSERT_TRUE(reply.has_value());
    auto& result = (*reply)["result"];
    ASSERT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 3u);  // binding + 2 uses
}
```

- [ ] **Step 2: Implement**

`src/lsp/handlers/references.cpp`:

```cpp
#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/lsp/symbol_index.h"

namespace mora::lsp {

namespace {

uint32_t to_one_based(int v) { return v < 0 ? 1 : static_cast<uint32_t>(v + 1); }

nlohmann::json span_to_range(const mora::SourceSpan& span) {
    auto zb = [](uint32_t one) -> int { return one == 0 ? 0 : static_cast<int>(one - 1); };
    return {
        {"start", {{"line", zb(span.start_line)}, {"character", zb(span.start_col)}}},
        {"end",   {{"line", zb(span.end_line)},   {"character", zb(span.end_col)}}},
    };
}

bool entries_match_for_refs(const SymbolEntry& target, const SymbolEntry& candidate) {
    if (target.name.value != candidate.name.value) return false;
    // Variables: scoped per rule via binding_span identity.
    if (target.kind == SymbolKind::VariableBinding ||
        target.kind == SymbolKind::VariableUse) {
        return (candidate.kind == SymbolKind::VariableBinding ||
                candidate.kind == SymbolKind::VariableUse) &&
               candidate.binding_span.start_line == target.binding_span.start_line &&
               candidate.binding_span.start_col  == target.binding_span.start_col;
    }
    // Rules and relations: match across the workspace by name (and namespace for relations).
    if (target.kind == SymbolKind::RuleHead || target.kind == SymbolKind::RuleCall) {
        return candidate.kind == SymbolKind::RuleHead ||
               candidate.kind == SymbolKind::RuleCall;
    }
    if (target.kind == SymbolKind::Relation) {
        return candidate.kind == SymbolKind::Relation &&
               candidate.ns_path == target.ns_path;
    }
    if (target.kind == SymbolKind::Atom) {
        return candidate.kind == SymbolKind::Atom;
    }
    return false;
}

Result on_references(Workspace& ws, const nlohmann::json& params) {
    std::string uri = params.at("textDocument").at("uri").get<std::string>();
    int line_zb = params.at("position").at("line").get<int>();
    int char_zb = params.at("position").at("character").get<int>();
    bool include_decl = true;
    if (params.contains("context") && params["context"].contains("includeDeclaration")) {
        include_decl = params["context"]["includeDeclaration"].get<bool>();
    }

    Document* doc = ws.get(uri);
    if (!doc) return nlohmann::json::array();
    const auto* target = doc->symbol_index().find_at(to_one_based(line_zb),
                                                     to_one_based(char_zb));
    if (!target) return nlohmann::json::array();

    nlohmann::json refs = nlohmann::json::array();
    auto add = [&](const Document& d, const SymbolEntry& e) {
        if (!include_decl &&
            (e.kind == SymbolKind::RuleHead || e.kind == SymbolKind::VariableBinding)) {
            return;
        }
        refs.push_back({
            {"uri", d.uri()},
            {"range", span_to_range(e.span)},
        });
    };

    // For variables: search only the same document (single-rule scope).
    if (target->kind == SymbolKind::VariableBinding ||
        target->kind == SymbolKind::VariableUse) {
        for (const auto& e : doc->symbol_index().entries()) {
            if (entries_match_for_refs(*target, e)) add(*doc, e);
        }
        return refs;
    }

    // Otherwise: workspace-wide scan.
    for (Document* d : ws.all_documents()) {
        for (const auto& e : d->symbol_index().entries()) {
            if (entries_match_for_refs(*target, e)) add(*d, e);
        }
    }
    return refs;
}

} // namespace

void register_references_handler(Dispatcher& d) {
    d.on_request("textDocument/references", on_references);
}

} // namespace mora::lsp
```

- [ ] **Step 3: Wire and test**

```bash
xmake build -P . -y && xmake run -P . test_references
```

- [ ] **Step 4: Commit**

```bash
git add src/lsp/handlers/references.cpp tests/lsp/test_references.cpp \
        src/lsp/server.cpp
git commit -m "feat(lsp): textDocument/references for all symbol kinds"
```

---

### Task 9: textDocument/documentSymbol

**Goal:** Outline pane shows the namespace + rule list in the active file.

**Files:**
- Create: `src/lsp/handlers/document_symbols.cpp`
- Create: `tests/lsp/test_document_symbols.cpp`
- Modify: `src/lsp/server.cpp`

- [ ] **Step 1: Write the failing test**

`tests/lsp/test_document_symbols.cpp`:

```cpp
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {
void register_document_symbols_handler(Dispatcher&);
}

using namespace mora::lsp;
using nlohmann::json;

namespace {
json req(int id, std::string_view method, json params) {
    return {{"jsonrpc","2.0"},{"id",id},{"method",std::string(method)},{"params",params}};
}
} // namespace

TEST(LspDocumentSymbols, ListsNamespaceAndRules) {
    Workspace ws;
    Dispatcher d; register_document_symbols_handler(d);
    ws.open("file:///x.mora",
        "namespace test.example\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n"
        "iron_weapons(W):\n"
        "    form/weapon(W)\n", 1);

    auto reply = d.dispatch(ws, req(1, "textDocument/documentSymbol", {
        {"textDocument", {{"uri", "file:///x.mora"}}},
    }));
    ASSERT_TRUE(reply.has_value());
    auto& result = (*reply)["result"];
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);  // 1 top-level Namespace
    EXPECT_EQ(result[0]["name"], "test.example");
    EXPECT_EQ(result[0]["kind"], 3);  // Namespace
    auto& children = result[0]["children"];
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0]["name"], "bandit");
    EXPECT_EQ(children[0]["kind"], 12);  // Function
    EXPECT_EQ(children[1]["name"], "iron_weapons");
}
```

- [ ] **Step 2: Implement**

`src/lsp/handlers/document_symbols.cpp`:

```cpp
#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/lsp/symbol_index.h"

namespace mora::lsp {

namespace {

nlohmann::json span_to_range(const mora::SourceSpan& span) {
    auto zb = [](uint32_t one) -> int { return one == 0 ? 0 : static_cast<int>(one - 1); };
    return {
        {"start", {{"line", zb(span.start_line)}, {"character", zb(span.start_col)}}},
        {"end",   {{"line", zb(span.end_line)},   {"character", zb(span.end_col)}}},
    };
}

Result on_document_symbol(Workspace& ws, const nlohmann::json& params) {
    std::string uri = params.at("textDocument").at("uri").get<std::string>();
    Document* doc = ws.get(uri);
    if (!doc) return nlohmann::json::array();

    nlohmann::json result = nlohmann::json::array();

    // Top-level: a single Namespace symbol containing rule children.
    nlohmann::json children = nlohmann::json::array();
    nlohmann::json ns_entry = nullptr;
    const auto& pool = doc->pool();

    for (const auto& e : doc->symbol_index().entries()) {
        if (e.kind == SymbolKind::Namespace) {
            ns_entry = {
                {"name", std::string(pool.get(e.name))},
                {"kind", 3},  // SymbolKind.Namespace per LSP
                {"range", span_to_range(e.span)},
                {"selectionRange", span_to_range(e.span)},
            };
        } else if (e.kind == SymbolKind::RuleHead) {
            children.push_back({
                {"name", std::string(pool.get(e.name))},
                {"kind", 12},  // SymbolKind.Function
                {"range", span_to_range(e.span)},
                {"selectionRange", span_to_range(e.span)},
            });
        }
    }

    if (!ns_entry.is_null()) {
        ns_entry["children"] = children;
        result.push_back(ns_entry);
    } else {
        // No namespace: emit rules as top-level.
        for (auto& c : children) result.push_back(c);
    }

    return result;
}

} // namespace

void register_document_symbols_handler(Dispatcher& d) {
    d.on_request("textDocument/documentSymbol", on_document_symbol);
}

} // namespace mora::lsp
```

- [ ] **Step 3: Wire and test**

```bash
xmake build -P . -y && xmake run -P . test_document_symbols
```

- [ ] **Step 4: Commit**

```bash
git add src/lsp/handlers/document_symbols.cpp tests/lsp/test_document_symbols.cpp \
        src/lsp/server.cpp
git commit -m "feat(lsp): textDocument/documentSymbol — namespace+rules outline"
```

---

### Task 10: workspace/symbol

**Goal:** `Ctrl+T` finds rules across all open files. Substring fuzzy match against `<namespace>.<rule>`.

**Files:**
- Create: `src/lsp/handlers/workspace_symbols.cpp`
- Create: `tests/lsp/test_workspace_symbols.cpp`
- Modify: `src/lsp/server.cpp`

- [ ] **Step 1: Write the failing test**

`tests/lsp/test_workspace_symbols.cpp`:

```cpp
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {
void register_workspace_symbols_handler(Dispatcher&);
}

using namespace mora::lsp;
using nlohmann::json;

namespace {
json req(int id, std::string_view method, json params) {
    return {{"jsonrpc","2.0"},{"id",id},{"method",std::string(method)},{"params",params}};
}
} // namespace

TEST(LspWorkspaceSymbols, MatchesAcrossDocuments) {
    Workspace ws;
    Dispatcher d; register_workspace_symbols_handler(d);
    ws.open("file:///a.mora",
        "namespace test.a\n"
        "bandit(NPC):\n    form/npc(NPC)\n", 1);
    ws.open("file:///b.mora",
        "namespace test.b\n"
        "iron_weapons(W):\n    form/weapon(W)\n", 1);

    auto reply = d.dispatch(ws, req(1, "workspace/symbol", {
        {"query", "bandit"},
    }));
    ASSERT_TRUE(reply.has_value());
    auto& result = (*reply)["result"];
    ASSERT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "test.a.bandit");
    EXPECT_EQ(result[0]["kind"], 12);
    EXPECT_EQ(result[0]["location"]["uri"], "file:///a.mora");
}

TEST(LspWorkspaceSymbols, EmptyQueryReturnsAllRules) {
    Workspace ws;
    Dispatcher d; register_workspace_symbols_handler(d);
    ws.open("file:///a.mora",
        "namespace test.a\nbandit(NPC):\n    form/npc(NPC)\n", 1);

    auto reply = d.dispatch(ws, req(1, "workspace/symbol", {{"query", ""}}));
    auto& result = (*reply)["result"];
    EXPECT_GE(result.size(), 1u);
}
```

- [ ] **Step 2: Implement**

`src/lsp/handlers/workspace_symbols.cpp`:

```cpp
#include <algorithm>
#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/lsp/symbol_index.h"

namespace mora::lsp {

namespace {

nlohmann::json span_to_range(const mora::SourceSpan& span) {
    auto zb = [](uint32_t one) -> int { return one == 0 ? 0 : static_cast<int>(one - 1); };
    return {
        {"start", {{"line", zb(span.start_line)}, {"character", zb(span.start_col)}}},
        {"end",   {{"line", zb(span.end_line)},   {"character", zb(span.end_col)}}},
    };
}

bool fuzzy_contains(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    auto it = std::search(haystack.begin(), haystack.end(),
                          needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    return it != haystack.end();
}

Result on_workspace_symbol(Workspace& ws, const nlohmann::json& params) {
    std::string query = params.at("query").get<std::string>();
    nlohmann::json results = nlohmann::json::array();
    for (Document* doc : ws.all_documents()) {
        const auto& pool = doc->pool();
        // Find this doc's namespace name (if any).
        std::string ns_name;
        for (const auto& e : doc->symbol_index().entries()) {
            if (e.kind == SymbolKind::Namespace) {
                ns_name = std::string(pool.get(e.name));
                break;
            }
        }
        for (const auto& e : doc->symbol_index().entries()) {
            if (e.kind != SymbolKind::RuleHead) continue;
            std::string rule_name = std::string(pool.get(e.name));
            std::string qualified = ns_name.empty() ? rule_name : (ns_name + "." + rule_name);
            if (!fuzzy_contains(qualified, query)) continue;
            results.push_back({
                {"name", qualified},
                {"kind", 12},  // Function
                {"location", {
                    {"uri", doc->uri()},
                    {"range", span_to_range(e.span)},
                }},
            });
        }
    }
    return results;
}

} // namespace

void register_workspace_symbols_handler(Dispatcher& d) {
    d.on_request("workspace/symbol", on_workspace_symbol);
}

} // namespace mora::lsp
```

- [ ] **Step 3: Wire and test**

```bash
xmake build -P . -y && xmake run -P . test_workspace_symbols
```

- [ ] **Step 4: Commit**

```bash
git add src/lsp/handlers/workspace_symbols.cpp tests/lsp/test_workspace_symbols.cpp \
        src/lsp/server.cpp
git commit -m "feat(lsp): workspace/symbol — fuzzy rule search across files"
```

---

### Task 11: textDocument/semanticTokens (full)

**Goal:** Editor paints undefined relations/atoms differently from defined ones, plus distinct shades for variables (bound vs use).

**Files:**
- Create: `src/lsp/handlers/semantic_tokens.cpp`
- Create: `tests/lsp/test_semantic_tokens.cpp`
- Modify: `src/lsp/server.cpp`
- Modify: `src/lsp/handlers/lifecycle.cpp` (advertise legend)

- [ ] **Step 1: Define the legend**

The LSP semantic-tokens legend is fixed at startup. We use a small, stable set:

| Index | Token type            | Modifier semantics |
| ---   | ---                   | --- |
| 0     | function              | (defined rule head or call) |
| 1     | function (deprecated mod) | (undefined rule call) |
| 2     | parameter             | (variable binding or use, bound) |
| 3     | parameter (deprecated mod)| (variable referenced but unbound) |
| 4     | enumMember            | (resolved atom) |
| 5     | enumMember (deprecated mod) | (unresolved atom) |
| 6     | namespace             | (built-in namespace `form`/`ref`/...) |

Modifier indexes:
- 0: `defaultLibrary` (built-in relation)
- 1: `deprecated` (undefined / unresolved — used to make red squigglies even before diagnostics)

LSP wire format: tokens are encoded as 5-tuples (deltaLine, deltaStart, length, tokenType, tokenModifiers) bit-packed.

- [ ] **Step 2: Write the failing test**

`tests/lsp/test_semantic_tokens.cpp`:

```cpp
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {
void register_semantic_tokens_handler(Dispatcher&);
}

using namespace mora::lsp;
using nlohmann::json;

namespace {
json req(int id, std::string_view method, json params) {
    return {{"jsonrpc","2.0"},{"id",id},{"method",std::string(method)},{"params",params}};
}
} // namespace

TEST(LspSemanticTokens, EmitsTokensForRulesAndVariables) {
    Workspace ws;
    Dispatcher d; register_semantic_tokens_handler(d);
    ws.open("file:///x.mora",
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n", 1);

    auto reply = d.dispatch(ws, req(1, "textDocument/semanticTokens/full", {
        {"textDocument", {{"uri", "file:///x.mora"}}},
    }));
    ASSERT_TRUE(reply.has_value());
    auto& result = (*reply)["result"];
    ASSERT_TRUE(result.contains("data"));
    auto& data = result["data"];
    ASSERT_TRUE(data.is_array());
    // Each token = 5 ints. With at least: namespace, bandit, NPC, form, npc, NPC.
    EXPECT_EQ(data.size() % 5, 0u);
    EXPECT_GE(data.size(), 5u * 5u);  // at least 5 tokens
}

TEST(LspSemanticTokens, EmptyDocumentReturnsEmptyData) {
    Workspace ws;
    Dispatcher d; register_semantic_tokens_handler(d);
    ws.open("file:///x.mora", "", 1);

    auto reply = d.dispatch(ws, req(1, "textDocument/semanticTokens/full", {
        {"textDocument", {{"uri", "file:///x.mora"}}},
    }));
    ASSERT_TRUE(reply.has_value());
    auto& data = (*reply)["result"]["data"];
    EXPECT_TRUE(data.is_array());
    EXPECT_EQ(data.size(), 0u);
}
```

- [ ] **Step 3: Implement**

`src/lsp/handlers/semantic_tokens.cpp`:

```cpp
#include <algorithm>
#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/lsp/symbol_index.h"

namespace mora::lsp {

namespace {

constexpr uint32_t TT_FUNCTION   = 0;
constexpr uint32_t TT_FUNCTION_DEP = 1;
constexpr uint32_t TT_PARAMETER  = 2;
constexpr uint32_t TT_PARAMETER_DEP = 3;
constexpr uint32_t TT_ENUM_MEMBER = 4;
constexpr uint32_t TT_ENUM_MEMBER_DEP = 5;
constexpr uint32_t TT_NAMESPACE  = 6;

constexpr uint32_t TM_DEFAULT_LIB = 1u << 0;
constexpr uint32_t TM_DEPRECATED  = 1u << 1;

uint32_t span_length(const mora::SourceSpan& span) {
    if (span.start_line != span.end_line) return 0;  // multi-line: encode as 0 length
    return span.end_col > span.start_col ? span.end_col - span.start_col : 1;
}

Result on_semantic_tokens_full(Workspace& ws, const nlohmann::json& params) {
    std::string uri = params.at("textDocument").at("uri").get<std::string>();
    Document* doc = ws.get(uri);
    nlohmann::json data = nlohmann::json::array();
    if (!doc) return nlohmann::json{{"data", data}};

    // Sort entries by (line, col) for delta encoding.
    auto entries = doc->symbol_index().entries();
    std::sort(entries.begin(), entries.end(),
              [](const SymbolEntry& a, const SymbolEntry& b) {
                  if (a.span.start_line != b.span.start_line)
                      return a.span.start_line < b.span.start_line;
                  return a.span.start_col < b.span.start_col;
              });

    uint32_t prev_line = 0, prev_col = 0;
    for (const auto& e : entries) {
        uint32_t type = 0, modifiers = 0;
        switch (e.kind) {
            case SymbolKind::RuleHead:
            case SymbolKind::RuleCall:        type = TT_FUNCTION; break;
            case SymbolKind::Relation:        type = TT_FUNCTION; modifiers |= TM_DEFAULT_LIB; break;
            case SymbolKind::VariableBinding:
            case SymbolKind::VariableUse:     type = TT_PARAMETER; break;
            case SymbolKind::Atom:            type = TT_ENUM_MEMBER; break;
            case SymbolKind::Namespace:       type = TT_NAMESPACE; break;
        }
        // 0-based positions for the LSP wire format.
        uint32_t line = e.span.start_line == 0 ? 0 : e.span.start_line - 1;
        uint32_t col  = e.span.start_col  == 0 ? 0 : e.span.start_col  - 1;
        uint32_t len  = span_length(e.span);
        uint32_t dl   = line - prev_line;
        uint32_t dc   = (dl == 0) ? (col - prev_col) : col;
        data.push_back(dl); data.push_back(dc); data.push_back(len);
        data.push_back(type); data.push_back(modifiers);
        prev_line = line; prev_col = col;
    }

    return nlohmann::json{{"data", data}};
}

} // namespace

void register_semantic_tokens_handler(Dispatcher& d) {
    d.on_request("textDocument/semanticTokens/full", on_semantic_tokens_full);
}

} // namespace mora::lsp
```

- [ ] **Step 4: Update lifecycle to advertise the legend**

In `src/lsp/handlers/lifecycle.cpp::server_capabilities`:

```cpp
nlohmann::json server_capabilities() {
    return {
        {"textDocumentSync", 1},
        {"hoverProvider", true},
        {"definitionProvider", true},
        {"referencesProvider", true},
        {"documentSymbolProvider", true},
        {"workspaceSymbolProvider", true},
        {"semanticTokensProvider", {
            {"legend", {
                {"tokenTypes", {
                    "function", "function", "parameter", "parameter",
                    "enumMember", "enumMember", "namespace",
                }},
                {"tokenModifiers", {"defaultLibrary", "deprecated"}},
            }},
            {"full", true},
        }},
    };
}
```

- [ ] **Step 5: Wire and test**

In `server.cpp`:

```cpp
void register_semantic_tokens_handler(Dispatcher&);
register_semantic_tokens_handler(dispatcher);
```

```bash
xmake build -P . -y && xmake run -P . test_semantic_tokens
```

- [ ] **Step 6: Commit**

```bash
git add src/lsp/handlers/semantic_tokens.cpp tests/lsp/test_semantic_tokens.cpp \
        src/lsp/handlers/lifecycle.cpp src/lsp/server.cpp
git commit -m "feat(lsp): textDocument/semanticTokens/full + capability advertisements"
```

---

### Task 12: VS Code client — pass dataDir + opt into semantic tokens

**Goal:** The extension passes the `mora.dataDir` setting to the server via `initializationOptions` and opts into semantic tokens.

**Files:**
- Modify: `editors/vscode/src/extension.ts`

- [ ] **Step 1: Update extension.ts**

In `editors/vscode/src/extension.ts`, modify `clientOptions`:

```ts
const config = vscode.workspace.getConfiguration('mora');

const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: 'file', language: 'mora' }],
    initializationOptions: {
        dataDir: config.get<string>('dataDir', ''),
        // relationsDir defaults to <workspace>/data/relations on the server side.
    },
    synchronize: {
        fileEvents: vscode.workspace.createFileSystemWatcher('**/data/relations/**/*.yaml'),
    },
    outputChannelName: 'Mora',
};
```

(`vscode-languageclient` automatically requests semantic tokens when the server advertises the capability — no extra wiring needed.)

- [ ] **Step 2: Compile**

```bash
cd editors/vscode && npm run compile
```

Expected: clean compile.

- [ ] **Step 3: Commit**

```bash
git add editors/vscode/src/extension.ts
git commit -m "feat(vscode): pass mora.dataDir to LSP via initializationOptions"
```

---

### Task 13: Bump versions to 0.3.0 + CHANGELOG

**Files:**
- Modify: `xmake.lua`
- Modify: `editors/vscode/package.json`
- Modify: `editors/vscode/CHANGELOG.md`
- Modify: `src/lsp/handlers/lifecycle.cpp` (version string in serverInfo)

- [ ] **Step 1: Bump xmake.lua**

In `xmake.lua` line 2: `set_version("0.2.0")` → `set_version("0.3.0")`.

- [ ] **Step 2: Bump package.json**

In `editors/vscode/package.json`: `"version": "0.2.0"` → `"0.3.0"`.

- [ ] **Step 3: Bump serverInfo version in lifecycle.cpp**

Find the line `{"serverInfo", {{"name", "mora-lsp"}, {"version", "0.2.0"}}}` and change to `0.3.0`. Also update `mora lsp --version` output in `src/lsp/server.cpp` if it embeds the version string (`mora-lsp 0.2.0 (LSP 3.17)` → `0.3.0`).

- [ ] **Step 4: Update CHANGELOG**

Prepend to `editors/vscode/CHANGELOG.md`:

```markdown
## 0.3.0 — 2026-04-15

- New: hover (`mora compile`-grade signatures + YAML docs for built-in
  relations, doc-comments for user rules, FormID lookup for atoms).
- New: goto-definition (rules → head, variables → binding site,
  built-in relations → defining YAML file).
- New: find-references (workspace-wide for rules and built-in relations,
  rule-scoped for variables).
- New: document outline + workspace symbol search (`Ctrl+T`).
- New: semantic tokens — built-in relations, atoms, and variables get
  distinguishable colours overlaying the TextMate grammar.
- New: configurable Skyrim Data folder (`mora.dataDir`) for atom
  resolution. Hover for unresolved atoms gracefully degrades.
- Internal: `Workspace` now owns the shared `SchemaRegistry` (loaded
  once at `initialize`); per-document re-parse is debounced to 150 ms.
```

- [ ] **Step 5: Verify version sync + run all tests**

```bash
xmake_version=$(grep -oP 'set_version\("\K[^"]+' xmake.lua)
pkg_version=$(node -p "require('./editors/vscode/package.json').version")
[ "$xmake_version" = "$pkg_version" ] && [ "$xmake_version" = "0.3.0" ] && echo OK

xmake test -P .
(cd editors/vscode && npm test)
```

Expected: both `OK` and all tests green.

- [ ] **Step 6: Commit**

```bash
git add xmake.lua editors/vscode/package.json editors/vscode/CHANGELOG.md \
        src/lsp/handlers/lifecycle.cpp src/lsp/server.cpp
git commit -m "chore: bump to 0.3.0 — semantic LSP features"
```

---

## Done — Phase 3 deliverable

After all 13 tasks land:

- Hover, goto-def, find-references, document/workspace symbols, and semantic tokens all work in VS Code against a `.mora` file.
- The Workspace owns one shared `SchemaRegistry` loaded from the project's `data/relations/` directory.
- Re-parses on `didChange` are debounced to 150 ms via a `select(2)`-based poll.
- Atom hover degrades cleanly when `mora.dataDir` is unset (the EditorIdRegistry stub returns "not resolved" until a follow-up implements ESP scanning).
- The .vsix bundles version 0.3.0; CHANGELOG describes the feature additions.

## Open follow-ups (track separately)

- **EditorIdRegistry::scan is a stub.** Hover for atoms always says "Atom not resolved". A follow-up uses `src/esp/` to walk the Data folder and populate the registry. Once the stub is replaced, hover automatically improves — no API changes.
- **`textDocument/semanticTokens/delta` is not implemented.** Editors will request `full` every time, which is fine at our document sizes. Add delta later if the editor reports lag.
- **YAML hot-reload.** The extension's `fileEvents` watcher fires `workspace/didChangeWatchedFiles`, but the server has no handler. Currently, editing a relations YAML requires restarting the LSP. A follow-up adds the handler that calls `ws.set_relations_dir(ws.relations_dir())` to reload.
- **Cross-file diagnostic invalidation.** When document A renames a rule that document B uses, B's diagnostics aren't re-published. The simplest fix: on `didChange` in any doc, mark all documents stale. Cheap; deferred.
- **Windows LSP.** The poll loop uses `select(2)`, POSIX-only. The Phase-3 server emits a clear error on Windows. A future task ports the poll loop to `WaitForSingleObject` on stdin.
- **Variable references span the whole file**, but they should be scoped to the containing rule. Phase 3 v1 scopes by binding-span identity, which works correctly within one rule but doesn't generalize if two rules use the same variable name. (No actual cross-rule false-positive, since each rule's `bindings_seen` map is fresh, so bindings have unique spans.) Worth re-examining if real users complain.
- **Hover for `RuleCall` and `RuleHead` show the same content.** No need for separate signatures yet — both paint the rule's signature + doc-comment.
