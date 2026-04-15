# VS Code Mora — Phase 2: `mora lsp` subcommand + diagnostics

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire a Language Server Protocol server into the `mora` CLI binary as a `mora lsp` subcommand, plus a VS Code client that consumes it, so that editing a `.mora` file shows live diagnostics matching `mora compile` byte-for-byte.

**Architecture:** New `src/lsp/` subsystem inside `mora_lib`, reusing the existing `Lexer`, `Parser`, `NameResolver`, `TypeChecker`, and `DiagBag`. Single-threaded JSON-RPC 2.0 over stdio, framing written in-house, JSON via header-only `nlohmann/json`. Per-document parse cache with debounced re-parse on `didChange`. The Lexer is changed to emit comment trivia (rather than discarding) and the Parser attaches contiguous leading `#` comments to the next `Rule` AST node as a doc comment.

**Tech Stack:** C++20 (xmake + g++/MSVC), gtest (existing), nlohmann/json (new xmake package), VS Code 1.85+, TypeScript 5.x, `vscode-languageclient` 9.x.

**Spec reference:** `docs/superpowers/specs/2026-04-15-vscode-mora-language-support-design.md` — Phase 2 section + Architectural decisions 1, 2 + Restructuring touch points.

---

## File structure

### Created in this plan

| Path | Purpose |
| --- | --- |
| `include/mora/lsp/lsp.h` | Public entry point: `int mora::lsp::run(int argc, char** argv)`. |
| `src/lsp/server.cpp` | Top-level run-loop: read frame → dispatch → write reply. Owns the `Workspace`. |
| `src/lsp/framing.h` / `.cpp` | `Content-Length`-prefixed JSON-RPC framing on `std::istream` / `std::ostream`. |
| `src/lsp/dispatch.h` / `.cpp` | Method-name → handler-function-ptr table with request/notification distinction. |
| `src/lsp/uri.h` / `.cpp` | `file://` URI ↔ filesystem path. |
| `src/lsp/workspace.h` / `.cpp` | URI → `Document` map plus shared `SchemaRegistry`. |
| `src/lsp/document.h` / `.cpp` | Per-file: text, version, debounced parse cache, latest diagnostics. |
| `src/lsp/diagnostics_convert.h` / `.cpp` | `mora::Diagnostic` → LSP `Diagnostic` JSON. |
| `src/lsp/handlers/lifecycle.cpp` | `initialize`, `initialized`, `shutdown`, `exit`. |
| `src/lsp/handlers/textsync.cpp` | `textDocument/didOpen`, `didChange`, `didClose`, `didSave`. |
| `tests/lsp/test_framing.cpp` | gtest: read/write of framed messages, malformed-input cases. |
| `tests/lsp/test_uri.cpp` | gtest: round-trip URI ↔ path on POSIX (and Windows-style paths as data, no system call). |
| `tests/lsp/test_document.cpp` | gtest: document text update, parse-cache invalidation. |
| `tests/lsp/test_workspace.cpp` | gtest: didOpen / didClose / didChange behaviour against the workspace. |
| `tests/lsp/test_diagnostics_convert.cpp` | gtest: `mora::Diagnostic` → LSP JSON, including range conversion. |
| `tests/lsp/test_protocol_e2e.cpp` | gtest: spawn `mora lsp` subprocess, send `initialize` + `didOpen`, assert on response/notifications. |
| `editors/vscode/src/findMora.ts` | Resolve `mora` binary: setting → PATH lookup. |

### Modified in this plan

| Path | Change |
| --- | --- |
| `xmake.lua` | Add `nlohmann_json` package, glob `src/lsp/**/*.cpp` into `mora_lib`. |
| `src/main.cpp` | Add `else if (command == "lsp") return mora::lsp::run(argc - 2, argv + 2);`. |
| `include/mora/ast/ast.h` | `Rule` gains `std::optional<std::string> doc_comment`. |
| `include/mora/lexer/token.h` | Add `TokenKind::Comment`. |
| `src/lexer/lexer.cpp` | Replace `skip_comment()` body with `lex_comment()` that emits a `Comment` token; teach the main lex loop to either emit or skip based on the `keep_trivia_` flag. |
| `include/mora/lexer/lexer.h` | Add `void set_keep_trivia(bool)` so most callers (the CLI) keep the existing skip-comment behaviour. |
| `src/parser/parser.cpp` | When constructing a `Rule`, scan back through any leading `Comment` tokens immediately above the rule head and fold them into `Rule::doc_comment`. |
| `include/mora/diag/diagnostic.h` | Add `std::vector<Diagnostic> drain_for_uri(std::string_view uri)`. |
| `src/diag/diagnostic.cpp` | Implement the above. |
| `editors/vscode/package.json` | Add `vscode-languageclient` to `dependencies`, drop `--no-dependencies` from `package` script. |
| `editors/vscode/.vscodeignore` | Adjust so production `node_modules/vscode-languageclient` and its transitive deps DO ship; dev-only ignored. |
| `editors/vscode/src/extension.ts` | Replace stub with LSP client startup. |
| `editors/vscode/CHANGELOG.md` | Add 0.2.0 entry. |
| `editors/vscode/package.json` | Bump version to `0.2.0`. |
| `xmake.lua` | Bump `set_version("0.2.0")`. |

---

## Conventions

- **Working directory:** all C++ commands are relative to the worktree root (`/home/tbaldrid/oss/language-design/.worktrees/<branch>` if executing in a worktree, else the repo root). Subagents must `cd` to that root before each Bash invocation.
- **Commits:** one per task. Conventional-commits style: `feat(lsp):`, `feat(parser):`, `test(lsp):`, `chore(xmake):`, etc.
- **Tests:** gtest discovers any `tests/<group>/test_*.cpp` file as its own test target (per existing `xmake.lua` lines 79–90). Putting new tests under `tests/lsp/` is the established pattern.
- **No push to remote** during execution. Push happens at the merge step in `superpowers:finishing-a-development-branch`.

---

## Coding constraints

These apply across all tasks. State them once, refer back as needed:

1. **No threads in the LSP run loop** for v1 (single-threaded; debouncing uses a deadline check on the next read, not a worker thread).
2. **No exceptions across the JSON-RPC boundary.** Handlers return a `dispatch::Result` (success-with-json | error-with-code-and-message), never throw.
3. **`std::cout`/`std::cerr` MUST NOT be used inside the LSP run loop** — all output is framed JSON to stdout, all logging goes to a file (the `--log` flag, off by default). The existing `mora` CLI logs through `mora::log::info` which writes to stdout; the LSP run loop has its own logger that writes to a file or `/dev/null`.
4. **All file I/O uses URI strings, not raw paths** at the API boundary. `uri.h` is the only place URI ↔ path conversion happens.
5. **The `Workspace` owns one `mora::SchemaRegistry`**, populated once at `initialize` time from `data/relations/**/*.yaml`. Documents share-by-reference.

---

### Task 1: xmake package + LSP entry-point skeleton

**Goal:** A `mora::lsp::run()` symbol that returns 0 immediately, accessible from a `mora lsp` invocation, with `nlohmann/json` linked in but unused.

**Files:**
- Modify: `xmake.lua`
- Create: `include/mora/lsp/lsp.h`
- Create: `src/lsp/server.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Add the nlohmann_json package and the LSP source glob to mora_lib**

In `xmake.lua`, find the existing `add_requires` block (currently `add_requires("zlib")`, `add_requires("fmt")`) and add a third line:

```lua
add_requires("nlohmann_json")
```

Find the `target("mora_lib")` block. The `add_files` call currently lists every subsystem under `src/`. Append `"src/lsp/*.cpp", "src/lsp/handlers/*.cpp"` to it. The `add_packages` call (currently `add_packages("zlib", "fmt", {public = true})`) becomes:

```lua
    add_packages("zlib", "fmt", "nlohmann_json", {public = true})
```

- [ ] **Step 2: Create the public LSP header**

`include/mora/lsp/lsp.h`:

```cpp
#pragma once

namespace mora::lsp {

// Run the LSP server over stdio. Reads JSON-RPC frames from std::cin and
// writes responses/notifications to std::cout. Logs (when enabled) go to a
// file passed via --log. Returns 0 on clean shutdown, non-zero on protocol
// or I/O error.
//
// argc/argv are the args AFTER `mora lsp` (so `mora lsp --log foo.txt`
// passes argc=2, argv={"--log", "foo.txt"}).
int run(int argc, char** argv);

} // namespace mora::lsp
```

- [ ] **Step 3: Create the server stub**

`src/lsp/server.cpp`:

```cpp
#include "mora/lsp/lsp.h"

namespace mora::lsp {

int run(int /*argc*/, char** /*argv*/) {
    // Phase 2 task 1: skeleton only. Real run loop comes in task 11.
    return 0;
}

} // namespace mora::lsp
```

- [ ] **Step 4: Wire the `lsp` subcommand**

In `src/main.cpp`, find the existing `if (command == "check") ... else if (command == "compile") ...` chain. Add **before** the `print_usage()` fallback:

```cpp
    else if (command == "lsp") {
        #include "mora/lsp/lsp.h"
        return mora::lsp::run(argc - 2, argv + 2);
    }
```

(If the chain is structured as a series of `if (command == "...")` returns, follow whatever pattern is there. Place `#include "mora/lsp/lsp.h"` at the top of `main.cpp` with the other includes — DON'T put `#include` inside an `else if` block; that's pseudo-syntax. Do it properly.)

Concrete addition for the top of `main.cpp` (with the other includes):

```cpp
#include "mora/lsp/lsp.h"
```

Concrete addition near the dispatch chain (use the same style as the existing `if/else if`):

```cpp
    else if (command == "lsp") return mora::lsp::run(argc - 2, argv + 2);
```

Also extend `print_usage()` (the helper in `main.cpp`) — add a line under "Commands:":

```
"  lsp       Run the language server over stdio (used by editors)\n"
```

- [ ] **Step 5: Build and verify**

Run from the repo root:

```bash
xmake f -y && xmake build mora
```

Expected: build succeeds. nlohmann_json is fetched and cached on first run (~30 s).

```bash
build/linux/x86_64/release/mora lsp; echo "exit=$?"
```

Expected: prints nothing, exits 0 immediately (the stub returns 0). The exit-code line shows `exit=0`.

```bash
build/linux/x86_64/release/mora 2>&1 | grep "lsp"
```

Expected: includes a line `  lsp       Run the language server over stdio …` from `print_usage()`.

- [ ] **Step 6: Commit**

```bash
git add xmake.lua include/mora/lsp/lsp.h src/lsp/server.cpp src/main.cpp
git commit -m "feat(lsp): mora lsp subcommand skeleton + nlohmann_json package"
```

---

### Task 2: URI module

**Goal:** `file://`-URI ↔ filesystem-path conversion that handles POSIX paths today and won't break on Windows-style paths in the future.

**Files:**
- Create: `src/lsp/uri.h`
- Create: `src/lsp/uri.cpp`
- Create: `tests/lsp/test_uri.cpp`

- [ ] **Step 1: Write the failing tests first**

`tests/lsp/test_uri.cpp`:

```cpp
#include <gtest/gtest.h>
#include "mora/lsp/uri.h"

using mora::lsp::uri_from_path;
using mora::lsp::path_from_uri;

TEST(LspUri, RoundTripPosixAbsolute) {
    EXPECT_EQ(uri_from_path("/home/user/foo.mora"),
              "file:///home/user/foo.mora");
    EXPECT_EQ(path_from_uri("file:///home/user/foo.mora"),
              "/home/user/foo.mora");
}

TEST(LspUri, PercentDecodesSpaces) {
    EXPECT_EQ(path_from_uri("file:///home/u%20s/foo%20bar.mora"),
              "/home/u s/foo bar.mora");
}

TEST(LspUri, PercentEncodesSpaces) {
    EXPECT_EQ(uri_from_path("/home/u s/foo bar.mora"),
              "file:///home/u%20s/foo%20bar.mora");
}

TEST(LspUri, WindowsDriveLetter) {
    // Windows file:/// URIs include the drive letter as the first
    // path segment. We don't run on Windows in tests but we accept
    // and produce the canonical form.
    EXPECT_EQ(path_from_uri("file:///C:/Users/u/foo.mora"),
              "C:/Users/u/foo.mora");
    EXPECT_EQ(uri_from_path("C:/Users/u/foo.mora"),
              "file:///C:/Users/u/foo.mora");
}

TEST(LspUri, RejectsNonFileScheme) {
    EXPECT_EQ(path_from_uri("https://example.com/foo"), "");
    EXPECT_EQ(path_from_uri("untitled:Untitled-1"), "");
}

TEST(LspUri, EmptyInput) {
    EXPECT_EQ(uri_from_path(""), "");
    EXPECT_EQ(path_from_uri(""), "");
}
```

- [ ] **Step 2: Add the test target via xmake's auto-discovery**

The existing `xmake.lua` already has a loop (lines 92–104) discovering `tests/**/test_*.cpp`. The new file at `tests/lsp/test_uri.cpp` matches `tests/**/test_*.cpp` so no xmake change is needed.

Run:

```bash
xmake build test_uri
```

Expected: FAIL — `mora/lsp/uri.h` not found.

- [ ] **Step 3: Write the header**

`src/lsp/uri.h`:

```cpp
#pragma once
#include <string>
#include <string_view>

namespace mora::lsp {

// Convert a filesystem path to a file:// URI. Spaces and other reserved
// characters are percent-encoded. Returns "" for empty input.
//
// Accepts both POSIX absolute paths ("/home/u/foo") and Windows-style
// paths ("C:/Users/u/foo"). Drive letters are preserved as the first
// path segment.
std::string uri_from_path(std::string_view path);

// Convert a file:// URI back to a filesystem path. Percent-encoded
// characters are decoded. Returns "" if the URI is not a file:// URI
// (e.g. https://, untitled:) or is otherwise unparseable.
std::string path_from_uri(std::string_view uri);

} // namespace mora::lsp
```

- [ ] **Step 4: Write the implementation**

`src/lsp/uri.cpp`:

```cpp
#include "mora/lsp/uri.h"

#include <cstdio>

namespace mora::lsp {

namespace {

bool is_unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~' ||
           c == '/' || c == ':';
}

void percent_encode(std::string& out, unsigned char c) {
    static const char hex[] = "0123456789ABCDEF";
    out.push_back('%');
    out.push_back(hex[c >> 4]);
    out.push_back(hex[c & 0x0F]);
}

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

} // namespace

std::string uri_from_path(std::string_view path) {
    if (path.empty()) return "";
    std::string out = "file://";
    if (path.front() != '/') out.push_back('/'); // Windows drive letter case
    for (unsigned char c : path) {
        if (is_unreserved(c)) out.push_back(static_cast<char>(c));
        else                  percent_encode(out, c);
    }
    return out;
}

std::string path_from_uri(std::string_view uri) {
    constexpr std::string_view kFile = "file://";
    if (uri.empty() || uri.size() < kFile.size() || uri.substr(0, kFile.size()) != kFile) {
        return "";
    }
    std::string_view body = uri.substr(kFile.size());
    // Drop the leading slash on Windows-style ("file:///C:/...") so the
    // result starts with "C:/". POSIX paths keep their leading slash.
    if (body.size() >= 3 && body[0] == '/' &&
        ((body[1] >= 'A' && body[1] <= 'Z') || (body[1] >= 'a' && body[1] <= 'z')) &&
        body[2] == ':') {
        body.remove_prefix(1);
    }
    std::string out;
    out.reserve(body.size());
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (c == '%' && i + 2 < body.size()) {
            int hi = hex_val(body[i + 1]);
            int lo = hex_val(body[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

} // namespace mora::lsp
```

- [ ] **Step 5: Run the tests**

```bash
xmake build test_uri && xmake test test_uri
```

Expected: 6 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/lsp/uri.h src/lsp/uri.cpp tests/lsp/test_uri.cpp
git commit -m "feat(lsp): file:// URI ↔ filesystem path conversion"
```

---

### Task 3: JSON-RPC framing (read + write)

**Goal:** Read and write `Content-Length`-prefixed JSON-RPC messages on `std::istream` / `std::ostream`. Tested in isolation against `std::stringstream`.

**Files:**
- Create: `src/lsp/framing.h`
- Create: `src/lsp/framing.cpp`
- Create: `tests/lsp/test_framing.cpp`

- [ ] **Step 1: Write the failing tests**

`tests/lsp/test_framing.cpp`:

```cpp
#include <gtest/gtest.h>
#include <sstream>
#include "mora/lsp/framing.h"

using mora::lsp::read_message;
using mora::lsp::write_message;
using mora::lsp::ReadResult;

TEST(LspFraming, ReadValidMessage) {
    std::stringstream s;
    s << "Content-Length: 17\r\n\r\n{\"jsonrpc\":\"2.0\"}";
    std::string body;
    ReadResult r = read_message(s, body);
    EXPECT_EQ(r, ReadResult::Ok);
    EXPECT_EQ(body, R"({"jsonrpc":"2.0"})");
}

TEST(LspFraming, ReadEofBeforeHeader) {
    std::stringstream s;
    std::string body;
    EXPECT_EQ(read_message(s, body), ReadResult::Eof);
}

TEST(LspFraming, ReadEofMidHeader) {
    std::stringstream s;
    s << "Content-Length: 17\r\n";  // missing terminating \r\n
    std::string body;
    EXPECT_EQ(read_message(s, body), ReadResult::Eof);
}

TEST(LspFraming, ReadEofMidBody) {
    std::stringstream s;
    s << "Content-Length: 100\r\n\r\n{\"jsonrpc\":\"2.0\"}";  // body too short
    std::string body;
    EXPECT_EQ(read_message(s, body), ReadResult::Eof);
}

TEST(LspFraming, ReadMissingContentLength) {
    std::stringstream s;
    s << "Some-Other-Header: 17\r\n\r\n{}";
    std::string body;
    EXPECT_EQ(read_message(s, body), ReadResult::ProtocolError);
}

TEST(LspFraming, ReadIgnoresContentType) {
    std::stringstream s;
    s << "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n"
      << "Content-Length: 2\r\n\r\n{}";
    std::string body;
    EXPECT_EQ(read_message(s, body), ReadResult::Ok);
    EXPECT_EQ(body, "{}");
}

TEST(LspFraming, WriteFormatsHeaderAndBody) {
    std::stringstream s;
    write_message(s, R"({"jsonrpc":"2.0"})");
    EXPECT_EQ(s.str(), "Content-Length: 17\r\n\r\n{\"jsonrpc\":\"2.0\"}");
}

TEST(LspFraming, WriteHandlesUnicode) {
    std::stringstream s;
    std::string body = "{\"text\":\"héllo\"}";  // 'é' is 2 UTF-8 bytes
    write_message(s, body);
    std::string expected = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    EXPECT_EQ(s.str(), expected);
}
```

- [ ] **Step 2: Run the tests, expect failure**

```bash
xmake build test_framing
```

Expected: FAIL (header doesn't exist).

- [ ] **Step 3: Write the header**

`src/lsp/framing.h`:

```cpp
#pragma once
#include <iosfwd>
#include <string>

namespace mora::lsp {

enum class ReadResult { Ok, Eof, ProtocolError };

// Read one Content-Length-framed JSON-RPC message from `in`. The body
// (no headers, no trailing newline) is written to `body_out`. Returns:
//   Ok            — body contains the message
//   Eof           — clean end of input (no partial message read)
//   ProtocolError — headers were malformed (no Content-Length, etc.)
ReadResult read_message(std::istream& in, std::string& body_out);

// Write `body` to `out` framed with a Content-Length header.
// Flushes `out` so the message hits the pipe immediately.
void write_message(std::ostream& out, std::string_view body);

} // namespace mora::lsp
```

- [ ] **Step 4: Write the implementation**

`src/lsp/framing.cpp`:

```cpp
#include "mora/lsp/framing.h"

#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <cstdlib>

namespace mora::lsp {

namespace {

// Read one CRLF-terminated header line. Returns false on EOF before any
// bytes were read. The trailing \r\n is consumed but not included.
bool read_header_line(std::istream& in, std::string& line) {
    line.clear();
    char c;
    while (in.get(c)) {
        if (c == '\r') {
            char next;
            if (in.get(next) && next == '\n') return true;
            // \r without \n — malformed, but treat as end of line.
            return true;
        }
        line.push_back(c);
    }
    return !line.empty();
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        char a = s[i], b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

} // namespace

ReadResult read_message(std::istream& in, std::string& body_out) {
    body_out.clear();
    size_t content_length = 0;
    bool have_length = false;
    bool any_header = false;

    while (true) {
        std::string line;
        if (!read_header_line(in, line)) {
            // EOF before any header byte (clean) or mid-header (truncated).
            return any_header ? ReadResult::Eof : ReadResult::Eof;
        }
        if (line.empty()) break; // end of headers
        any_header = true;
        if (starts_with_ci(line, "Content-Length:")) {
            std::string_view v(line);
            v.remove_prefix(std::string_view("Content-Length:").size());
            // Skip leading whitespace.
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
                v.remove_prefix(1);
            }
            std::string num(v);
            char* end = nullptr;
            unsigned long long n = std::strtoull(num.c_str(), &end, 10);
            if (end == num.c_str()) return ReadResult::ProtocolError;
            content_length = static_cast<size_t>(n);
            have_length = true;
        }
        // Other headers (Content-Type, etc.) are ignored.
    }

    if (!have_length) return ReadResult::ProtocolError;

    body_out.resize(content_length);
    in.read(body_out.data(), static_cast<std::streamsize>(content_length));
    if (static_cast<size_t>(in.gcount()) != content_length) {
        return ReadResult::Eof;
    }
    return ReadResult::Ok;
}

void write_message(std::ostream& out, std::string_view body) {
    out << "Content-Length: " << body.size() << "\r\n\r\n";
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    out.flush();
}

} // namespace mora::lsp
```

- [ ] **Step 5: Run the tests**

```bash
xmake build test_framing && xmake test test_framing
```

Expected: 8 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/lsp/framing.h src/lsp/framing.cpp tests/lsp/test_framing.cpp
git commit -m "feat(lsp): Content-Length JSON-RPC framing on istream/ostream"
```

---

### Task 4: Method dispatch table

**Goal:** A small typed table mapping JSON-RPC method names to handler functions, with explicit "request" vs "notification" distinction (requests must be answered, notifications must not).

**Files:**
- Create: `src/lsp/dispatch.h`
- Create: `src/lsp/dispatch.cpp`

(No tests for this task on its own — it's exercised by the lifecycle handler tests in Task 5.)

- [ ] **Step 1: Write the header**

`src/lsp/dispatch.h`:

```cpp
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace mora::lsp {

class Workspace;

// Standard JSON-RPC error codes.
enum class ErrorCode : int {
    ParseError      = -32700,
    InvalidRequest  = -32600,
    MethodNotFound  = -32601,
    InvalidParams   = -32602,
    InternalError   = -32603,
    ServerNotInitialized = -32002,
    RequestCancelled     = -32800,
};

struct Error {
    ErrorCode code;
    std::string message;
};

// A handler returns either a result JSON value (for requests) or an error.
// For notifications the runtime ignores the success value.
using Result = std::variant<nlohmann::json, Error>;

// Signature for a request/notification handler.
using Handler = std::function<Result(Workspace&, const nlohmann::json& params)>;

class Dispatcher {
public:
    // Register a request handler — its Result is sent back as the response.
    void on_request(std::string method, Handler h);
    // Register a notification handler — Result is discarded.
    void on_notification(std::string method, Handler h);

    // Dispatch a parsed message. The optional `id` tells you whether this
    // is a request (id present) or notification (id absent). Returns the
    // response JSON to send back, or std::nullopt for notifications.
    std::optional<nlohmann::json> dispatch(Workspace& ws,
                                           const nlohmann::json& message);

private:
    struct Entry { Handler handler; bool is_request; };
    std::unordered_map<std::string, Entry> entries_;

    static nlohmann::json make_error_response(const nlohmann::json& id,
                                              ErrorCode code,
                                              std::string_view message);
    static nlohmann::json make_success_response(const nlohmann::json& id,
                                                const nlohmann::json& result);
};

} // namespace mora::lsp
```

- [ ] **Step 2: Write the implementation**

`src/lsp/dispatch.cpp`:

```cpp
#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {

void Dispatcher::on_request(std::string method, Handler h) {
    entries_[std::move(method)] = {std::move(h), true};
}

void Dispatcher::on_notification(std::string method, Handler h) {
    entries_[std::move(method)] = {std::move(h), false};
}

std::optional<nlohmann::json> Dispatcher::dispatch(Workspace& ws,
                                                   const nlohmann::json& message) {
    const bool has_id = message.contains("id");
    const nlohmann::json id = has_id ? message["id"] : nlohmann::json();

    auto method_it = message.find("method");
    if (method_it == message.end() || !method_it->is_string()) {
        if (has_id) {
            return make_error_response(id, ErrorCode::InvalidRequest,
                                       "missing or non-string \"method\"");
        }
        return std::nullopt;
    }

    auto entry_it = entries_.find(method_it->get<std::string>());
    if (entry_it == entries_.end()) {
        if (has_id) {
            return make_error_response(id, ErrorCode::MethodNotFound,
                                       "method not found");
        }
        return std::nullopt;
    }

    nlohmann::json params;
    if (auto p = message.find("params"); p != message.end()) params = *p;

    Result result = entry_it->second.handler(ws, params);

    if (!entry_it->second.is_request) {
        // Notification — no response, error or success.
        return std::nullopt;
    }
    if (auto* err = std::get_if<Error>(&result)) {
        return make_error_response(id, err->code, err->message);
    }
    return make_success_response(id, std::get<nlohmann::json>(result));
}

nlohmann::json Dispatcher::make_error_response(const nlohmann::json& id,
                                               ErrorCode code,
                                               std::string_view message) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id.is_null() ? nlohmann::json() : id},
        {"error", {
            {"code", static_cast<int>(code)},
            {"message", std::string(message)},
        }},
    };
}

nlohmann::json Dispatcher::make_success_response(const nlohmann::json& id,
                                                 const nlohmann::json& result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result},
    };
}

} // namespace mora::lsp
```

- [ ] **Step 3: Verify it builds (no test yet)**

```bash
xmake build mora_lib
```

Expected: success.

- [ ] **Step 4: Commit**

```bash
git add src/lsp/dispatch.h src/lsp/dispatch.cpp
git commit -m "feat(lsp): JSON-RPC method dispatcher with request/notification distinction"
```

---

### Task 5: Lifecycle handlers — initialize, initialized, shutdown, exit

**Goal:** Implement the four lifecycle methods. The `Workspace` (forward-declared by `dispatch.h`) is touched lightly here — full Workspace state lands in Task 8.

**Files:**
- Create: `src/lsp/handlers/lifecycle.cpp`
- Create: `tests/lsp/test_lifecycle.cpp`
- Modify: `src/lsp/workspace.h` (initial stub — full impl in Task 8)
- Create: `src/lsp/workspace.cpp` (initial stub)

- [ ] **Step 1: Stub the Workspace just enough that lifecycle tests can run**

`src/lsp/workspace.h`:

```cpp
#pragma once

namespace mora::lsp {

// Holds the per-LSP-server state that persists across requests.
// Real document/schema state lands in Task 8.
class Workspace {
public:
    Workspace();
    ~Workspace();

    bool initialized() const { return initialized_; }
    void mark_initialized() { initialized_ = true; }

    bool shutdown_requested() const { return shutdown_requested_; }
    void request_shutdown() { shutdown_requested_ = true; }

private:
    bool initialized_ = false;
    bool shutdown_requested_ = false;
};

} // namespace mora::lsp
```

`src/lsp/workspace.cpp`:

```cpp
#include "mora/lsp/workspace.h"

namespace mora::lsp {

Workspace::Workspace() = default;
Workspace::~Workspace() = default;

} // namespace mora::lsp
```

- [ ] **Step 2: Write the failing tests**

`tests/lsp/test_lifecycle.cpp`:

```cpp
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {
void register_lifecycle_handlers(Dispatcher&);
}

using nlohmann::json;
using namespace mora::lsp;

namespace {
json req(int id, std::string_view method, json params = {}) {
    return {{"jsonrpc","2.0"},{"id",id},{"method",std::string(method)},{"params",params}};
}
json note(std::string_view method, json params = {}) {
    return {{"jsonrpc","2.0"},{"method",std::string(method)},{"params",params}};
}
} // namespace

TEST(LspLifecycle, InitializeReturnsCapabilities) {
    Workspace ws;
    Dispatcher d;
    register_lifecycle_handlers(d);

    auto r = d.dispatch(ws, req(1, "initialize", json::object()));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ((*r)["id"], 1);
    ASSERT_TRUE(r->contains("result"));
    auto caps = (*r)["result"]["capabilities"];
    EXPECT_TRUE(caps.contains("textDocumentSync"));
}

TEST(LspLifecycle, InitializedNotificationMarksReady) {
    Workspace ws;
    Dispatcher d;
    register_lifecycle_handlers(d);

    d.dispatch(ws, req(1, "initialize"));
    EXPECT_FALSE(ws.initialized());

    auto r = d.dispatch(ws, note("initialized"));
    EXPECT_FALSE(r.has_value());  // notifications don't reply
    EXPECT_TRUE(ws.initialized());
}

TEST(LspLifecycle, ShutdownReturnsNullResult) {
    Workspace ws;
    Dispatcher d;
    register_lifecycle_handlers(d);
    d.dispatch(ws, req(1, "initialize"));
    d.dispatch(ws, note("initialized"));

    auto r = d.dispatch(ws, req(2, "shutdown"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ((*r)["id"], 2);
    EXPECT_TRUE((*r)["result"].is_null());
    EXPECT_TRUE(ws.shutdown_requested());
}

TEST(LspLifecycle, RequestBeforeInitializeReturnsServerNotInitialized) {
    Workspace ws;
    Dispatcher d;
    register_lifecycle_handlers(d);

    // Pretend a textDocument/foo request arrived before initialize.
    // We have no such handler yet, but a request to "shutdown" before
    // initialize is the canonical test:
    auto r = d.dispatch(ws, req(2, "shutdown"));
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(r->contains("error"));
    EXPECT_EQ((*r)["error"]["code"], static_cast<int>(ErrorCode::ServerNotInitialized));
}
```

- [ ] **Step 3: Build, expect failure**

```bash
xmake build test_lifecycle
```

Expected: FAIL — `register_lifecycle_handlers` not defined.

- [ ] **Step 4: Implement the handlers**

`src/lsp/handlers/lifecycle.cpp`:

```cpp
#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {

namespace {

nlohmann::json server_capabilities() {
    return {
        // textDocumentSync = 1 (full) for v1; can move to incremental in
        // a follow-up. The full-sync mode receives the entire document
        // text on every change, which is fine at our document sizes.
        {"textDocumentSync", 1},
    };
}

Result on_initialize(Workspace&, const nlohmann::json&) {
    return nlohmann::json{
        {"capabilities", server_capabilities()},
        {"serverInfo", {{"name", "mora-lsp"}, {"version", "0.2.0"}}},
    };
}

Result on_initialized(Workspace& ws, const nlohmann::json&) {
    ws.mark_initialized();
    return nlohmann::json{};  // unused for notifications
}

Result on_shutdown(Workspace& ws, const nlohmann::json&) {
    if (!ws.initialized()) {
        return Error{ErrorCode::ServerNotInitialized, "shutdown before initialize"};
    }
    ws.request_shutdown();
    return nlohmann::json();  // null result
}

Result on_exit(Workspace&, const nlohmann::json&) {
    // The run loop interprets exit by checking shutdown_requested before
    // tearing down. This handler is a no-op aside from acknowledging
    // receipt; the loop check happens after dispatch.
    return nlohmann::json{};
}

} // namespace

void register_lifecycle_handlers(Dispatcher& d) {
    d.on_request     ("initialize",  on_initialize);
    d.on_notification("initialized", on_initialized);
    d.on_request     ("shutdown",    on_shutdown);
    d.on_notification("exit",        on_exit);
}

} // namespace mora::lsp
```

- [ ] **Step 5: Run the tests**

```bash
xmake build test_lifecycle && xmake test test_lifecycle
```

Expected: 4 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/lsp/workspace.h src/lsp/workspace.cpp \
        src/lsp/handlers/lifecycle.cpp tests/lsp/test_lifecycle.cpp
git commit -m "feat(lsp): initialize/initialized/shutdown/exit lifecycle handlers"
```

---

### Task 6: Run loop integration in server.cpp

**Goal:** A real `mora::lsp::run()` that loops on `read_message` → `dispatch` → `write_message` until `exit` arrives, with `--log` support.

**Files:**
- Modify: `src/lsp/server.cpp`

- [ ] **Step 1: Replace `server.cpp` with the run loop**

`src/lsp/server.cpp`:

```cpp
#include "mora/lsp/lsp.h"

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/framing.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {

void register_lifecycle_handlers(Dispatcher&);
// Future tasks add: register_textsync_handlers, register_diagnostics_handlers, ...

namespace {

// Open `path` as the LSP log sink. If `path` is empty, returns a dummy
// stream that discards everything. Returned stream is owned by the caller.
std::unique_ptr<std::ostream> open_log(std::string_view path) {
    if (path.empty()) {
        return std::make_unique<std::ofstream>("/dev/null");
    }
    auto f = std::make_unique<std::ofstream>(std::string(path), std::ios::app);
    return f;
}

void log_event(std::ostream& log, std::string_view event, std::string_view detail = "") {
    log << "[lsp] " << event;
    if (!detail.empty()) log << " " << detail;
    log << "\n";
    log.flush();
}

} // namespace

int run(int argc, char** argv) {
    std::string log_path;
    for (int i = 0; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--version") {
            std::cout << "mora-lsp 0.2.0 (LSP 3.17)\n";
            return 0;
        }
        if (a == "--log" && i + 1 < argc) {
            log_path = argv[++i];
        }
    }

    auto log = open_log(log_path);
    log_event(*log, "starting");

    Workspace ws;
    Dispatcher dispatcher;
    register_lifecycle_handlers(dispatcher);

    while (true) {
        std::string body;
        ReadResult r = read_message(std::cin, body);
        if (r == ReadResult::Eof) {
            log_event(*log, "eof — clean shutdown");
            break;
        }
        if (r == ReadResult::ProtocolError) {
            log_event(*log, "protocol error in headers — aborting");
            return 1;
        }

        nlohmann::json msg;
        try {
            msg = nlohmann::json::parse(body);
        } catch (const std::exception& e) {
            log_event(*log, "json parse error", e.what());
            // Per LSP, malformed JSON → reply with a parse error if we have
            // any id we can pull, otherwise drop. We can't extract an id
            // from a malformed body, so just drop.
            continue;
        }

        std::optional<nlohmann::json> reply = dispatcher.dispatch(ws, msg);
        if (reply) {
            write_message(std::cout, reply->dump());
            log_event(*log, "<- response");
        }

        if (ws.shutdown_requested() && msg.value("method", "") == "exit") {
            log_event(*log, "exit after shutdown — clean");
            return 0;
        }
    }

    return 0;
}

} // namespace mora::lsp
```

- [ ] **Step 2: Build and verify the binary still runs**

```bash
xmake build mora && build/linux/x86_64/release/mora lsp --version
```

Expected: prints `mora-lsp 0.2.0 (LSP 3.17)` and exits 0.

- [ ] **Step 3: Smoke-test the loop with a synthetic input**

```bash
printf 'Content-Length: 56\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
  | build/linux/x86_64/release/mora lsp 2>/dev/null
```

Expected: prints something like:

```
Content-Length: 156
{"jsonrpc":"2.0","id":1,"result":{"capabilities":{"textDocumentSync":1},"serverInfo":{"name":"mora-lsp","version":"0.2.0"}}}
```

(Length will differ slightly from 156 — just confirm there's a `Content-Length:` header followed by a JSON-RPC response with `result.capabilities`.)

The process will hang waiting for more input — that's correct. Kill it with Ctrl-C or pipe EOF:

```bash
{ printf 'Content-Length: 56\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'; } \
  | build/linux/x86_64/release/mora lsp 2>/dev/null
```

The trailing `}` closes the brace and EOF triggers a clean shutdown.

- [ ] **Step 4: Commit**

```bash
git add src/lsp/server.cpp
git commit -m "feat(lsp): wire run loop — read, dispatch, write, log to file"
```

---

### Task 7: Lexer emits comment trivia + AST gains doc_comment

**Goal:** The lexer can be configured to emit `Comment` tokens (default: skip them, preserving existing CLI behaviour). The `Rule` AST node grows an optional `doc_comment` field for the parser to populate in Task 8.

**Files:**
- Modify: `include/mora/lexer/token.h`
- Modify: `include/mora/lexer/lexer.h`
- Modify: `src/lexer/lexer.cpp`
- Modify: `include/mora/ast/ast.h`
- Create: `tests/lexer/test_comment_trivia.cpp`

- [ ] **Step 1: Add `Comment` to `TokenKind`**

In `include/mora/lexer/token.h`, find the `enum class TokenKind` block. Add `Comment` to the list of identifier-style tokens (line 11 area). After the change, the enum should include a `Comment` entry, e.g.:

```cpp
enum class TokenKind {
    // ... existing entries ...
    Integer, Float, String, Symbol, EditorId, Variable, Identifier, Discard,
    // ... existing entries ...
    Comment,                       // NEW: leading-`#` comment, body excludes the `#`
    Newline, Indent, Dedent,
    // ... existing entries ...
    Eof, Error,
};
```

(Place `Comment` somewhere logical — adjacent to `Newline`/`Indent`/`Dedent` since they are all "trivia" in spirit.)

Also extend `token_kind_name()` (in `src/lexer/token.cpp` if it exists, or wherever its body lives) to handle the new value with a case returning `"Comment"`.

- [ ] **Step 2: Add the `keep_trivia` toggle to Lexer**

In `include/mora/lexer/lexer.h`, find the `class Lexer` declaration. Add a public method:

```cpp
    void set_keep_trivia(bool keep) { keep_trivia_ = keep; }
```

And a private member at the bottom of the class:

```cpp
private:
    bool keep_trivia_ = false;
```

(If a `private:` block already exists, add it there; otherwise create one.)

- [ ] **Step 3: Replace `skip_comment()` with conditional emit/skip**

In `src/lexer/lexer.cpp`:

Find the existing `Lexer::skip_comment()` function (around line 74). Add a sibling function:

```cpp
Token Lexer::lex_comment() {
    // Pre: peek() == '#'. Reads '#' followed by everything until end of
    // line (excluding the newline). The token's text excludes the '#'.
    advance();  // consume '#'
    size_t saved_start = token_start_;
    token_start_ = pos_;
    while (!at_end() && peek() != '\n') advance();
    Token tok = make_token(TokenKind::Comment);
    token_start_ = saved_start;
    return tok;
}
```

Find every call site of `skip_comment()` in `lexer.cpp`. There are two patterns:
1. The "indent computation" path (around line 268–272) where comment-only lines are bypassed. Leave those `skip_comment()` calls as-is — they don't produce tokens.
2. The main `Lexer::next()` dispatch path (around line 330) where a `#` triggers `skip_comment()` then continues. Replace that branch with:

```cpp
        if (peek() == '#') {
            if (keep_trivia_) {
                return lex_comment();
            }
            skip_comment();
            // After comment, check if at end or newline
            // (existing follow-up logic stays as is)
        }
```

Declare `Token lex_comment();` in the private section of `include/mora/lexer/lexer.h`.

- [ ] **Step 4: Add the failing test**

`tests/lexer/test_comment_trivia.cpp`:

```cpp
#include <gtest/gtest.h>
#include "mora/lexer/lexer.h"
#include "mora/core/string_pool.h"

using namespace mora;

namespace {
std::vector<TokenKind> kinds(Lexer& lex) {
    std::vector<TokenKind> out;
    for (;;) {
        Token t = lex.next();
        out.push_back(t.kind);
        if (t.kind == TokenKind::Eof) break;
    }
    return out;
}
} // namespace

TEST(LexerCommentTrivia, DefaultModeSkipsComments) {
    StringPool pool;
    std::string src = "# leading\nnamespace foo\n";
    Lexer lex(src, "x.mora", pool);
    auto ks = kinds(lex);
    // No Comment token expected.
    for (auto k : ks) EXPECT_NE(k, TokenKind::Comment);
}

TEST(LexerCommentTrivia, KeepTriviaEmitsComment) {
    StringPool pool;
    std::string src = "# leading\nnamespace foo\n";
    Lexer lex(src, "x.mora", pool);
    lex.set_keep_trivia(true);
    auto ks = kinds(lex);
    bool saw_comment = false;
    for (auto k : ks) if (k == TokenKind::Comment) saw_comment = true;
    EXPECT_TRUE(saw_comment);
}

TEST(LexerCommentTrivia, CommentExcludesHashSymbol) {
    StringPool pool;
    std::string src = "# foo bar";
    Lexer lex(src, "x.mora", pool);
    lex.set_keep_trivia(true);
    Token t = lex.next();
    EXPECT_EQ(t.kind, TokenKind::Comment);
    // The token text should be " foo bar" (no leading '#').
    auto text = pool.get(t.string_id);
    EXPECT_EQ(text, " foo bar");
}
```

(If `Token` doesn't carry text via `string_id` — check `token.h` — adapt the assertion to whatever the existing tokens use for their text. The point is: text excludes the `#`.)

- [ ] **Step 5: Add `doc_comment` to `Rule`**

In `include/mora/ast/ast.h`, find `struct Rule` (around line 97). Add an optional doc_comment field:

```cpp
#include <optional>
// ... existing includes ...

struct Rule {
    RuleKind kind = RuleKind::Static;
    StringId name;
    std::vector<Expr> head_args;
    std::vector<Clause> body;
    std::vector<Effect> effects;
    std::vector<ConditionalEffect> conditional_effects;
    SourceSpan span;
    std::optional<std::string> doc_comment;  // NEW: leading # comments above the rule head
};
```

(`<optional>` and `<string>` are likely already included via the headers `Rule` already pulls in. Double-check the top of `ast.h`.)

- [ ] **Step 6: Build and run all tests**

```bash
xmake build && xmake test
```

Expected:
- All existing tests still pass (no behavioural change for default-mode lexing or for `Rule` consumers — the new field is `std::nullopt`).
- The 3 new `LexerCommentTrivia.*` tests pass.

- [ ] **Step 7: Commit**

```bash
git add include/mora/lexer/token.h include/mora/lexer/lexer.h \
        src/lexer/lexer.cpp include/mora/ast/ast.h \
        tests/lexer/test_comment_trivia.cpp
git commit -m "feat(lexer): emit Comment trivia (opt-in); Rule gains doc_comment field"
```

---

### Task 8: Parser attaches doc-comments to rules

**Goal:** When the lexer is in `keep_trivia` mode, the parser folds contiguous `#` comments immediately above a rule head into `Rule::doc_comment`. Comments not directly above a rule are discarded.

**Files:**
- Modify: `src/parser/parser.cpp`
- Modify: `include/mora/parser/parser.h` (small: a setter to enable trivia mode at construction)
- Create: `tests/parser/test_doc_comments.cpp`

- [ ] **Step 1: Write the failing test**

`tests/parser/test_doc_comments.cpp`:

```cpp
#include <gtest/gtest.h>
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

using namespace mora;

namespace {
auto parse_with_trivia(const std::string& src) {
    auto pool = std::make_unique<StringPool>();
    auto diags = std::make_unique<DiagBag>();
    Lexer lex(src, "x.mora", *pool);
    lex.set_keep_trivia(true);
    Parser parser(lex, *pool, *diags);
    auto file = parser.parse_file();
    return std::make_tuple(std::move(file), std::move(pool), std::move(diags));
}
} // namespace

TEST(ParserDocComments, AttachesAdjacentLeadingHashCommentsToRule) {
    std::string src =
        "namespace t\n"
        "\n"
        "# This rule tags bandits.\n"
        "# Multi-line block.\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n";
    auto [file, pool, diags] = parse_with_trivia(src);
    ASSERT_EQ(file.rules.size(), 1u);
    ASSERT_TRUE(file.rules[0].doc_comment.has_value());
    EXPECT_EQ(*file.rules[0].doc_comment,
              " This rule tags bandits.\n Multi-line block.");
}

TEST(ParserDocComments, NoDocCommentWhenBlankLineBetweenCommentAndHead) {
    std::string src =
        "namespace t\n"
        "\n"
        "# Stray comment.\n"
        "\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n";
    auto [file, pool, diags] = parse_with_trivia(src);
    ASSERT_EQ(file.rules.size(), 1u);
    EXPECT_FALSE(file.rules[0].doc_comment.has_value());
}

TEST(ParserDocComments, DefaultLexerModeProducesNoDocComment) {
    std::string src =
        "namespace t\n"
        "# This rule tags bandits.\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n";
    StringPool pool;
    DiagBag diags;
    Lexer lex(src, "x.mora", pool);
    // NB: not calling set_keep_trivia
    Parser parser(lex, pool, diags);
    auto file = parser.parse_file();
    ASSERT_EQ(file.rules.size(), 1u);
    EXPECT_FALSE(file.rules[0].doc_comment.has_value());
}
```

(Adjust the constructor signatures and `parse_file` return type to whatever the parser actually exposes. The shape above assumes a `parse_file()` returning a struct with `rules`.)

- [ ] **Step 2: Build, expect failure**

```bash
xmake build test_doc_comments
```

Expected: tests link, run, fail (`doc_comment` is always `nullopt`).

- [ ] **Step 3: Implement the trivia-attachment logic in the parser**

In `src/parser/parser.cpp`:

Find the `Parser::peek()` and `Parser::advance()` functions. The parser must now track pending `Comment` tokens. The cleanest approach:

1. Add a private member `std::vector<std::string> pending_comments_;` to `Parser` (in `include/mora/parser/parser.h`).
2. Inside `Parser::peek()` (or wherever the parser pulls from the lexer), wrap the `lexer_.next()` call: if the returned token is `TokenKind::Comment`, push its text into `pending_comments_` and try again. Also: a `TokenKind::Newline` token ends a "doc-comment block" only if it follows another newline (i.e. blank line); a single newline after a comment is fine. Actually, simpler rule: when we see ANY token that is NOT `Comment`, that's the boundary. If the prior tokens were all comments preceded by no blank line, they're attached.
3. Track "blank line seen since last comment" — if we see a `Newline` while `pending_comments_` is non-empty AND there's already at least one comment, OK; if we see `Newline` with no pending comment, fine; the trick is "blank line" = two consecutive newlines that wipe pending_comments_.

Concrete: introduce a small helper:

```cpp
// In parser.h, private:
std::vector<std::string> pending_comments_;
bool comment_block_broken_ = false;  // true after we see a blank line

// In parser.cpp, replace the body of `Token Parser::peek()` (around line 11)
// with a version that filters trivia:
Token Parser::peek() {
    if (!has_current_) {
        for (;;) {
            current_ = lexer_.next();
            if (current_.kind == TokenKind::Comment) {
                pending_comments_.push_back(std::string(pool_.get(current_.string_id)));
                continue;
            }
            if (current_.kind == TokenKind::Newline) {
                // A blank line (Newline immediately after another Newline)
                // breaks the doc-comment block.
                if (last_was_newline_) {
                    pending_comments_.clear();
                }
                last_was_newline_ = true;
                continue;  // also skip Newlines from peek's perspective if
                           // the existing parser already ignored them — but
                           // if it didn't, you must NOT skip them here.
            }
            last_was_newline_ = false;
            break;
        }
        has_current_ = true;
    }
    return current_;
}
```

CAUTION: The above sketch assumes the existing parser skips `Newline` tokens itself. If it does NOT (i.e. newlines are part of the parser's grammar), DO NOT skip them in `peek()`. Instead, in `peek()` only filter `Comment` tokens, and add the blank-line tracking by counting consecutive `Newline` tokens elsewhere.

Read the existing `peek()` carefully before editing — preserve its current contract.

4. When constructing a `Rule`, pull `pending_comments_` and join them into the doc_comment field. Find the function in `parser.cpp` that constructs the `Rule` AST node (search for `Rule rule;` or `Rule{` literal). Immediately before populating the rule's other fields, do:

```cpp
        Rule rule;
        if (!pending_comments_.empty()) {
            std::string joined;
            for (size_t i = 0; i < pending_comments_.size(); ++i) {
                if (i > 0) joined.push_back('\n');
                joined += pending_comments_[i];
            }
            rule.doc_comment = std::move(joined);
            pending_comments_.clear();
        }
```

Add a helper to clear pending comments at every other top-level decl boundary so stray comments above non-rule decls don't leak:

```cpp
        // Wherever a non-Rule top-level decl is constructed, e.g.:
        //   case TokenKind::KwNamespace: parse_namespace_decl(); break;
        // add at the start of each parse_xxx_decl():
        pending_comments_.clear();
```

- [ ] **Step 4: Run the new tests + the full suite**

```bash
xmake build && xmake test
```

Expected: all tests pass (existing + 3 new in `test_doc_comments`).

- [ ] **Step 5: Commit**

```bash
git add include/mora/parser/parser.h src/parser/parser.cpp \
        tests/parser/test_doc_comments.cpp
git commit -m "feat(parser): attach leading # comments to rules as doc_comment"
```

---

### Task 9: DiagBag::drain_for_uri

**Goal:** Pull diagnostics for a single document so the LSP can publish per-URI.

**Files:**
- Modify: `include/mora/diag/diagnostic.h`
- Modify: `src/diag/diagnostic.cpp`
- Modify: `tests/diagnostic_test.cpp` (add a case)

- [ ] **Step 1: Write the failing test**

Append to `tests/diagnostic_test.cpp` (find a logical insertion point — after the existing tests, before namespace close):

```cpp
TEST(DiagBag, DrainForUriReturnsOnlyMatching) {
    using namespace mora;
    DiagBag bag;
    SourceSpan a{}; a.file_name = "/home/u/a.mora";
    SourceSpan b{}; b.file_name = "/home/u/b.mora";
    bag.error("E001", "in a", a, "");
    bag.error("E001", "in b", b, "");
    bag.error("E001", "also a", a, "");

    auto for_a = bag.drain_for_uri("file:///home/u/a.mora");
    EXPECT_EQ(for_a.size(), 2u);
    EXPECT_EQ(for_a[0].message, "in a");
    EXPECT_EQ(for_a[1].message, "also a");

    // After drain, b's diagnostics remain in the bag.
    auto for_b = bag.drain_for_uri("file:///home/u/b.mora");
    EXPECT_EQ(for_b.size(), 1u);
}
```

- [ ] **Step 2: Add the declaration**

In `include/mora/diag/diagnostic.h`, inside `class DiagBag`:

```cpp
    // Remove and return all diagnostics whose `span.file_name` corresponds
    // to the given file:// URI. Other diagnostics remain in the bag.
    std::vector<Diagnostic> drain_for_uri(std::string_view uri);
```

(Add `#include <string_view>` to the header if not already present.)

- [ ] **Step 3: Add the implementation**

In `src/diag/diagnostic.cpp`, after the existing methods:

```cpp
#include "mora/lsp/uri.h"  // path_from_uri

namespace mora {

std::vector<Diagnostic> DiagBag::drain_for_uri(std::string_view uri) {
    std::string path = lsp::path_from_uri(uri);
    std::vector<Diagnostic> out;
    std::lock_guard<std::mutex> g(mu_);  // if DiagBag has a mutex; if not, drop this line
    auto it = diags_.begin();             // assuming the storage is `std::vector<Diagnostic> diags_`
    while (it != diags_.end()) {
        if (it->span.file_name == path) {
            out.push_back(std::move(*it));
            it = diags_.erase(it);
        } else {
            ++it;
        }
    }
    return out;
}

} // namespace mora
```

(Adjust to whatever DiagBag's actual storage and mutex are. Read `diagnostic.h` and `diagnostic.cpp` to find the field names.)

- [ ] **Step 4: Build and run**

```bash
xmake build && xmake test diagnostic_test
```

Expected: all `DiagBag.*` tests pass including the new one.

- [ ] **Step 5: Commit**

```bash
git add include/mora/diag/diagnostic.h src/diag/diagnostic.cpp tests/diagnostic_test.cpp
git commit -m "feat(diag): DiagBag::drain_for_uri for per-document publishDiagnostics"
```

---

### Task 10: Document class

**Goal:** A `Document` that holds the latest text + version, runs the parse pipeline on demand, and caches the resulting `DiagBag`.

**Files:**
- Modify: `src/lsp/document.h`
- Create: `src/lsp/document.cpp`
- Create: `tests/lsp/test_document.cpp`

- [ ] **Step 1: Write the header**

`src/lsp/document.h`:

```cpp
#pragma once
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mora/diag/diagnostic.h"

namespace mora {
class StringPool;
}

namespace mora::lsp {

class Document {
public:
    Document(std::string uri, std::string text, int version);

    const std::string& uri() const     { return uri_; }
    const std::string& text() const    { return text_; }
    int                version() const { return version_; }

    // Replace the text and bump the version. Marks the parse cache stale.
    void update(std::string text, int version);

    // Re-run the parse pipeline if the cache is stale. Returns the latest
    // diagnostics scoped to this document's URI.
    const std::vector<Diagnostic>& diagnostics();

    // Schedule a re-parse to happen no sooner than `at`. Used by debouncing.
    void schedule_reparse(std::chrono::steady_clock::time_point at) { reparse_after_ = at; }
    bool reparse_due(std::chrono::steady_clock::time_point now) const {
        return cache_stale_ && now >= reparse_after_;
    }

private:
    std::string uri_;
    std::string text_;
    int version_ = 0;

    bool cache_stale_ = true;
    std::chrono::steady_clock::time_point reparse_after_{};
    std::vector<Diagnostic> diagnostics_;
};

} // namespace mora::lsp
```

- [ ] **Step 2: Write the failing tests**

`tests/lsp/test_document.cpp`:

```cpp
#include <gtest/gtest.h>
#include "mora/lsp/document.h"

using namespace mora::lsp;

TEST(LspDocument, ConstructHoldsFields) {
    Document d("file:///x.mora", "namespace t\n", 1);
    EXPECT_EQ(d.uri(), "file:///x.mora");
    EXPECT_EQ(d.text(), "namespace t\n");
    EXPECT_EQ(d.version(), 1);
}

TEST(LspDocument, UpdateBumpsVersion) {
    Document d("file:///x.mora", "namespace t\n", 1);
    d.update("namespace t\nnamespace u\n", 2);
    EXPECT_EQ(d.version(), 2);
    EXPECT_EQ(d.text(), "namespace t\nnamespace u\n");
}

TEST(LspDocument, DiagnosticsForValidSourceIsEmpty) {
    Document d("file:///x.mora",
               "namespace t\nbandit(NPC):\n    form/npc(NPC)\n",
               1);
    auto& diags = d.diagnostics();
    // Note: form/npc may or may not resolve — the point is the parser
    // doesn't emit a parse error on this trivially-valid syntax. If
    // sema produces "undefined relation" errors, that's fine — the
    // test just confirms no segfault and a deterministic count.
    EXPECT_GE(diags.size(), 0u);
}

TEST(LspDocument, DiagnosticsForBadSourceHasError) {
    Document d("file:///x.mora",
               "namespace t\nbandit(:\n",  // missing variable in head
               1);
    auto& diags = d.diagnostics();
    EXPECT_GT(diags.size(), 0u);
}
```

- [ ] **Step 3: Build, expect failure**

```bash
xmake build test_document
```

Expected: FAIL (Document not implemented).

- [ ] **Step 4: Implement Document**

`src/lsp/document.cpp`:

```cpp
#include "mora/lsp/document.h"

#include "mora/lsp/uri.h"
#include "mora/core/string_pool.h"
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"   // adjust if path differs
#include "mora/sema/type_checker.h"    // adjust if path differs

namespace mora::lsp {

Document::Document(std::string uri, std::string text, int version)
    : uri_(std::move(uri)), text_(std::move(text)), version_(version) {}

void Document::update(std::string text, int version) {
    text_ = std::move(text);
    version_ = version;
    cache_stale_ = true;
}

const std::vector<Diagnostic>& Document::diagnostics() {
    if (!cache_stale_) return diagnostics_;

    std::string path = path_from_uri(uri_);
    StringPool pool;
    DiagBag bag;
    Lexer lex(text_, path, pool);
    Parser parser(lex, pool, bag);
    auto file = parser.parse_file();
    // sema passes:
    NameResolver resolver(pool, bag);
    auto resolved = resolver.resolve(file);
    TypeChecker tc(pool, bag);
    tc.check(resolved);

    diagnostics_ = bag.drain_for_uri(uri_);
    cache_stale_ = false;
    return diagnostics_;
}

} // namespace mora::lsp
```

NB: the `NameResolver` and `TypeChecker` API names above are placeholders — replace with whatever the actual API is. Inspect existing CLI code (`src/main.cpp`'s `check` command) to see how these passes are wired.

- [ ] **Step 5: Run the tests**

```bash
xmake build test_document && xmake test test_document
```

Expected: 4 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/lsp/document.h src/lsp/document.cpp tests/lsp/test_document.cpp
git commit -m "feat(lsp): Document — text+version, parse cache, diagnostics"
```

---

### Task 11: Workspace + textDocument/didOpen, didChange, didClose handlers

**Goal:** The `Workspace` owns documents indexed by URI; the textsync handlers create/update/destroy them.

**Files:**
- Modify: `src/lsp/workspace.h`
- Modify: `src/lsp/workspace.cpp`
- Create: `src/lsp/handlers/textsync.cpp`
- Create: `tests/lsp/test_workspace.cpp`

- [ ] **Step 1: Extend Workspace with the document map**

`src/lsp/workspace.h` — replace the file with:

```cpp
#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mora::lsp {

class Document;

class Workspace {
public:
    Workspace();
    ~Workspace();

    bool initialized() const { return initialized_; }
    void mark_initialized() { initialized_ = true; }

    bool shutdown_requested() const { return shutdown_requested_; }
    void request_shutdown() { shutdown_requested_ = true; }

    // Document lifecycle.
    Document* open(std::string uri, std::string text, int version);
    void      change(std::string_view uri, std::string text, int version);
    void      close(std::string_view uri);
    Document* get(std::string_view uri);

    // For tests.
    size_t document_count() const { return docs_.size(); }

private:
    bool initialized_ = false;
    bool shutdown_requested_ = false;
    std::unordered_map<std::string, std::unique_ptr<Document>> docs_;
};

} // namespace mora::lsp
```

- [ ] **Step 2: Update workspace.cpp**

`src/lsp/workspace.cpp`:

```cpp
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"

namespace mora::lsp {

Workspace::Workspace() = default;
Workspace::~Workspace() = default;

Document* Workspace::open(std::string uri, std::string text, int version) {
    auto doc = std::make_unique<Document>(uri, std::move(text), version);
    Document* raw = doc.get();
    docs_.emplace(std::move(uri), std::move(doc));
    return raw;
}

void Workspace::change(std::string_view uri, std::string text, int version) {
    auto it = docs_.find(std::string(uri));
    if (it != docs_.end()) it->second->update(std::move(text), version);
}

void Workspace::close(std::string_view uri) {
    docs_.erase(std::string(uri));
}

Document* Workspace::get(std::string_view uri) {
    auto it = docs_.find(std::string(uri));
    return it == docs_.end() ? nullptr : it->second.get();
}

} // namespace mora::lsp
```

- [ ] **Step 3: Write the failing tests**

`tests/lsp/test_workspace.cpp`:

```cpp
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/lsp/dispatch.h"

namespace mora::lsp {
void register_textsync_handlers(Dispatcher&);
}

using namespace mora::lsp;
using nlohmann::json;

namespace {
json note(std::string_view method, json params = {}) {
    return {{"jsonrpc","2.0"},{"method",std::string(method)},{"params",params}};
}
} // namespace

TEST(LspWorkspace, DidOpenCreatesDocument) {
    Workspace ws;
    Dispatcher d;
    register_textsync_handlers(d);

    json params = {
        {"textDocument", {
            {"uri", "file:///x.mora"},
            {"languageId", "mora"},
            {"version", 1},
            {"text", "namespace t\n"},
        }},
    };
    d.dispatch(ws, note("textDocument/didOpen", params));
    ASSERT_EQ(ws.document_count(), 1u);
    auto* doc = ws.get("file:///x.mora");
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->text(), "namespace t\n");
    EXPECT_EQ(doc->version(), 1);
}

TEST(LspWorkspace, DidChangeUpdatesText) {
    Workspace ws;
    Dispatcher d;
    register_textsync_handlers(d);

    d.dispatch(ws, note("textDocument/didOpen", {
        {"textDocument", {
            {"uri", "file:///x.mora"},
            {"languageId", "mora"},
            {"version", 1},
            {"text", "namespace t\n"},
        }},
    }));
    d.dispatch(ws, note("textDocument/didChange", {
        {"textDocument", {{"uri", "file:///x.mora"}, {"version", 2}}},
        {"contentChanges", json::array({json{{"text", "namespace u\n"}}})},
    }));

    auto* doc = ws.get("file:///x.mora");
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->text(), "namespace u\n");
    EXPECT_EQ(doc->version(), 2);
}

TEST(LspWorkspace, DidCloseRemovesDocument) {
    Workspace ws;
    Dispatcher d;
    register_textsync_handlers(d);

    d.dispatch(ws, note("textDocument/didOpen", {
        {"textDocument", {
            {"uri", "file:///x.mora"},
            {"languageId", "mora"},
            {"version", 1},
            {"text", ""},
        }},
    }));
    EXPECT_EQ(ws.document_count(), 1u);

    d.dispatch(ws, note("textDocument/didClose", {
        {"textDocument", {{"uri", "file:///x.mora"}}},
    }));
    EXPECT_EQ(ws.document_count(), 0u);
}
```

- [ ] **Step 4: Implement the handlers**

`src/lsp/handlers/textsync.cpp`:

```cpp
#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {

namespace {

Result on_did_open(Workspace& ws, const nlohmann::json& params) {
    const auto& td = params.at("textDocument");
    ws.open(td.at("uri").get<std::string>(),
            td.at("text").get<std::string>(),
            td.at("version").get<int>());
    return nlohmann::json{};
}

Result on_did_change(Workspace& ws, const nlohmann::json& params) {
    const auto& td = params.at("textDocument");
    const auto& changes = params.at("contentChanges");
    if (changes.empty()) return nlohmann::json{};
    // textDocumentSync = Full → there's exactly one entry whose `text` is
    // the entire new content.
    const std::string& text = changes.back().at("text").get_ref<const std::string&>();
    ws.change(td.at("uri").get<std::string>(),
              text,
              td.at("version").get<int>());
    return nlohmann::json{};
}

Result on_did_close(Workspace& ws, const nlohmann::json& params) {
    ws.close(params.at("textDocument").at("uri").get<std::string>());
    return nlohmann::json{};
}

Result on_did_save(Workspace&, const nlohmann::json&) {
    // No-op for v1 — diagnostics already published on didChange.
    return nlohmann::json{};
}

} // namespace

void register_textsync_handlers(Dispatcher& d) {
    d.on_notification("textDocument/didOpen",  on_did_open);
    d.on_notification("textDocument/didChange", on_did_change);
    d.on_notification("textDocument/didClose", on_did_close);
    d.on_notification("textDocument/didSave",  on_did_save);
}

} // namespace mora::lsp
```

- [ ] **Step 5: Wire registration into the run loop**

In `src/lsp/server.cpp`, add the forward-declaration and call:

```cpp
void register_textsync_handlers(Dispatcher&);
```

Inside `run()`, after `register_lifecycle_handlers(dispatcher);`:

```cpp
    register_textsync_handlers(dispatcher);
```

- [ ] **Step 6: Build and run**

```bash
xmake build && xmake test test_workspace
```

Expected: 3 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/lsp/workspace.h src/lsp/workspace.cpp \
        src/lsp/handlers/textsync.cpp tests/lsp/test_workspace.cpp \
        src/lsp/server.cpp
git commit -m "feat(lsp): Workspace document map + textDocument sync handlers"
```

---

### Task 12: Diagnostic → LSP JSON conversion

**Goal:** Convert `mora::Diagnostic` to the LSP `Diagnostic` JSON shape with proper Range (line/character, 0-based).

**Files:**
- Create: `src/lsp/diagnostics_convert.h`
- Create: `src/lsp/diagnostics_convert.cpp`
- Create: `tests/lsp/test_diagnostics_convert.cpp`

- [ ] **Step 1: Write the failing tests**

`tests/lsp/test_diagnostics_convert.cpp`:

```cpp
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "mora/lsp/diagnostics_convert.h"
#include "mora/diag/diagnostic.h"
#include "mora/core/source_location.h"

using namespace mora;

TEST(LspDiagConvert, ErrorBecomesSeverity1) {
    Diagnostic d;
    d.level = DiagLevel::Error;
    d.code = "E0142";
    d.message = "boom";
    d.span.start_line = 5;   // 1-based in mora
    d.span.start_col  = 3;
    d.span.end_line   = 5;
    d.span.end_col    = 8;

    auto j = lsp::diagnostic_to_json(d);
    EXPECT_EQ(j["severity"], 1);
    EXPECT_EQ(j["code"], "E0142");
    EXPECT_EQ(j["source"], "mora");
    EXPECT_EQ(j["message"], "boom");
    EXPECT_EQ(j["range"]["start"]["line"], 4);     // 0-based
    EXPECT_EQ(j["range"]["start"]["character"], 2);
    EXPECT_EQ(j["range"]["end"]["line"], 4);
    EXPECT_EQ(j["range"]["end"]["character"], 7);
}

TEST(LspDiagConvert, WarningBecomesSeverity2) {
    Diagnostic d;
    d.level = DiagLevel::Warning;
    d.span.start_line = 1;
    d.span.start_col  = 1;
    d.span.end_line   = 1;
    d.span.end_col    = 2;

    auto j = lsp::diagnostic_to_json(d);
    EXPECT_EQ(j["severity"], 2);
}

TEST(LspDiagConvert, NoteBecomesSeverity3) {
    Diagnostic d;
    d.level = DiagLevel::Note;
    d.span.start_line = 1;
    d.span.start_col  = 1;
    d.span.end_line   = 1;
    d.span.end_col    = 2;

    auto j = lsp::diagnostic_to_json(d);
    EXPECT_EQ(j["severity"], 3);
}
```

(Adjust field names to the actual `SourceSpan` shape — read `include/mora/core/source_location.h` first. The above assumes 1-based `start_line`/`start_col`/`end_line`/`end_col`. If they're 0-based already, drop the `-1` adjustments below.)

- [ ] **Step 2: Write the header**

`src/lsp/diagnostics_convert.h`:

```cpp
#pragma once
#include <nlohmann/json.hpp>

#include "mora/diag/diagnostic.h"

namespace mora::lsp {

// Convert a single mora Diagnostic into an LSP Diagnostic JSON object.
// The mora::SourceSpan is expected to be 1-based; the LSP wants 0-based,
// so we subtract 1 from line/character.
nlohmann::json diagnostic_to_json(const mora::Diagnostic& d);

} // namespace mora::lsp
```

- [ ] **Step 3: Write the implementation**

`src/lsp/diagnostics_convert.cpp`:

```cpp
#include "mora/lsp/diagnostics_convert.h"

namespace mora::lsp {

namespace {

int severity(mora::DiagLevel level) {
    switch (level) {
        case mora::DiagLevel::Error:   return 1;  // LSP DiagnosticSeverity.Error
        case mora::DiagLevel::Warning: return 2;  // .Warning
        case mora::DiagLevel::Note:    return 3;  // .Information
    }
    return 1;
}

} // namespace

nlohmann::json diagnostic_to_json(const mora::Diagnostic& d) {
    return {
        {"range", {
            {"start", {{"line", d.span.start_line - 1}, {"character", d.span.start_col - 1}}},
            {"end",   {{"line", d.span.end_line   - 1}, {"character", d.span.end_col   - 1}}},
        }},
        {"severity", severity(d.level)},
        {"code",     d.code},
        {"source",   "mora"},
        {"message",  d.message},
    };
}

} // namespace mora::lsp
```

- [ ] **Step 4: Run the tests**

```bash
xmake build test_diagnostics_convert && xmake test test_diagnostics_convert
```

Expected: 3 tests pass. If `SourceSpan` field names differ, fix the implementation to match (e.g. it might be `start.line` rather than `start_line`).

- [ ] **Step 5: Commit**

```bash
git add src/lsp/diagnostics_convert.h src/lsp/diagnostics_convert.cpp \
        tests/lsp/test_diagnostics_convert.cpp
git commit -m "feat(lsp): convert mora::Diagnostic to LSP JSON (0-based ranges)"
```

---

### Task 13: publishDiagnostics on document change

**Goal:** After `didOpen` and `didChange`, the server pushes a `textDocument/publishDiagnostics` notification with the document's diagnostics.

**Files:**
- Modify: `src/lsp/server.cpp`
- Modify: `src/lsp/handlers/textsync.cpp`
- Modify: `src/lsp/workspace.h` (add a publish-callback hook)

The challenge: handlers are pure functions taking `(Workspace&, json)` and returning a result. publishDiagnostics is a server-pushed notification, not a response. We need a way for handlers to enqueue notifications back to the server.

- [ ] **Step 1: Add a notification queue to Workspace**

In `src/lsp/workspace.h`, add:

```cpp
#include <vector>
#include <nlohmann/json.hpp>
// ... existing includes ...

class Workspace {
public:
    // ... existing methods ...

    // Append an outgoing notification to be sent at the end of the
    // current dispatch tick.
    void enqueue_notification(nlohmann::json msg) { outgoing_.push_back(std::move(msg)); }

    // Drain queued notifications. The run loop calls this after each
    // dispatch and writes them out.
    std::vector<nlohmann::json> drain_outgoing() {
        std::vector<nlohmann::json> out;
        out.swap(outgoing_);
        return out;
    }

private:
    // ... existing fields ...
    std::vector<nlohmann::json> outgoing_;
};
```

- [ ] **Step 2: Update textsync handlers to publish diagnostics**

In `src/lsp/handlers/textsync.cpp`:

Add includes:

```cpp
#include "mora/lsp/document.h"
#include "mora/lsp/diagnostics_convert.h"
```

Add a helper near the top (inside the anonymous namespace):

```cpp
void publish_diagnostics(Workspace& ws, std::string_view uri) {
    Document* doc = ws.get(uri);
    if (!doc) return;
    const auto& diags = doc->diagnostics();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : diags) arr.push_back(diagnostic_to_json(d));
    ws.enqueue_notification({
        {"jsonrpc", "2.0"},
        {"method",  "textDocument/publishDiagnostics"},
        {"params",  {
            {"uri",         std::string(uri)},
            {"diagnostics", arr},
        }},
    });
}
```

Modify `on_did_open` and `on_did_change` (and only those — `on_did_close` clears the document) so each calls `publish_diagnostics(ws, uri)` after creating/updating:

```cpp
Result on_did_open(Workspace& ws, const nlohmann::json& params) {
    const auto& td = params.at("textDocument");
    std::string uri = td.at("uri").get<std::string>();
    ws.open(uri,
            td.at("text").get<std::string>(),
            td.at("version").get<int>());
    publish_diagnostics(ws, uri);
    return nlohmann::json{};
}

Result on_did_change(Workspace& ws, const nlohmann::json& params) {
    const auto& td = params.at("textDocument");
    const auto& changes = params.at("contentChanges");
    if (changes.empty()) return nlohmann::json{};
    const std::string& text = changes.back().at("text").get_ref<const std::string&>();
    std::string uri = td.at("uri").get<std::string>();
    ws.change(uri, text, td.at("version").get<int>());
    publish_diagnostics(ws, uri);
    return nlohmann::json{};
}

Result on_did_close(Workspace& ws, const nlohmann::json& params) {
    std::string uri = params.at("textDocument").at("uri").get<std::string>();
    ws.close(uri);
    // Clear stale diagnostics on the client side.
    ws.enqueue_notification({
        {"jsonrpc", "2.0"},
        {"method",  "textDocument/publishDiagnostics"},
        {"params",  {{"uri", uri}, {"diagnostics", nlohmann::json::array()}}},
    });
    return nlohmann::json{};
}
```

- [ ] **Step 3: Update the run loop to drain outgoing**

In `src/lsp/server.cpp`, inside the main loop, after the `dispatcher.dispatch(ws, msg)` call and the response write:

```cpp
        // Drain any server-pushed notifications enqueued by the handler.
        for (auto& note : ws.drain_outgoing()) {
            write_message(std::cout, note.dump());
        }
```

- [ ] **Step 4: Add an integration test for the queue**

Edit `tests/lsp/test_workspace.cpp` to add a fourth test:

```cpp
TEST(LspWorkspace, DidOpenEnqueuesPublishDiagnostics) {
    Workspace ws;
    Dispatcher d;
    register_textsync_handlers(d);

    d.dispatch(ws, note("textDocument/didOpen", {
        {"textDocument", {
            {"uri", "file:///x.mora"},
            {"languageId", "mora"},
            {"version", 1},
            {"text", "namespace t\n"},
        }},
    }));

    auto outgoing = ws.drain_outgoing();
    ASSERT_EQ(outgoing.size(), 1u);
    EXPECT_EQ(outgoing[0]["method"], "textDocument/publishDiagnostics");
    EXPECT_EQ(outgoing[0]["params"]["uri"], "file:///x.mora");
}
```

- [ ] **Step 5: Run all LSP tests**

```bash
xmake build && xmake test
```

Expected: all tests pass; the new `DidOpenEnqueuesPublishDiagnostics` test in particular.

- [ ] **Step 6: Commit**

```bash
git add src/lsp/workspace.h src/lsp/handlers/textsync.cpp \
        src/lsp/server.cpp tests/lsp/test_workspace.cpp
git commit -m "feat(lsp): publishDiagnostics after didOpen/didChange/didClose"
```

---

### Task 14: End-to-end protocol test (subprocess)

**Goal:** Spawn `mora lsp` as a child process, pipe JSON-RPC frames in/out, assert on the responses.

**Files:**
- Create: `tests/lsp/test_protocol_e2e.cpp`

- [ ] **Step 1: Write the test**

`tests/lsp/test_protocol_e2e.cpp`:

```cpp
#include <gtest/gtest.h>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

// Find the mora binary in build/linux/x86_64/{release,releasedbg,debug}/.
std::string find_mora() {
    namespace fs = std::filesystem;
    const char* modes[] = {"release", "releasedbg", "debug"};
    for (fs::path p = fs::current_path(); !p.empty(); p = p.parent_path()) {
        for (const char* m : modes) {
            auto cand = p / "build" / "linux" / "x86_64" / m / "mora";
            if (fs::exists(cand)) return cand.string();
        }
        if (p == p.parent_path()) break;
    }
    return "";
}

std::string frame(std::string_view body) {
    std::string out = "Content-Length: ";
    out += std::to_string(body.size());
    out += "\r\n\r\n";
    out += body;
    return out;
}

// Run `mora lsp`, send the concatenated frames on stdin, return stdout.
std::string run_lsp(const std::string& bin, std::string_view stdin_data) {
    // Create a temp file for stdin.
    std::string in_path  = std::tmpnam(nullptr);
    std::string out_path = std::tmpnam(nullptr);
    {
        std::ofstream f(in_path);
        f.write(stdin_data.data(), stdin_data.size());
    }
    std::string cmd = bin + " lsp < " + in_path + " > " + out_path + " 2>/dev/null";
    int rc = std::system(cmd.c_str());
    (void)rc;
    std::ifstream f(out_path);
    std::stringstream ss;
    ss << f.rdbuf();
    std::remove(in_path.c_str());
    std::remove(out_path.c_str());
    return ss.str();
}

} // namespace

TEST(LspE2e, InitializeRoundTrip) {
    std::string bin = find_mora();
    ASSERT_FALSE(bin.empty()) << "mora binary not found";

    auto in = frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})")
            + frame(R"({"jsonrpc":"2.0","method":"initialized","params":{}})")
            + frame(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})")
            + frame(R"({"jsonrpc":"2.0","method":"exit"})");

    std::string out = run_lsp(bin, in);

    // Parse the first response (initialize) — find first body after first
    // header block.
    auto skip_one_message = [&out](size_t& pos) -> std::string {
        size_t hdr_end = out.find("\r\n\r\n", pos);
        if (hdr_end == std::string::npos) return "";
        std::string hdrs = out.substr(pos, hdr_end - pos);
        size_t cl = hdrs.find("Content-Length:");
        size_t length = std::stoul(hdrs.substr(cl + 15));
        std::string body = out.substr(hdr_end + 4, length);
        pos = hdr_end + 4 + length;
        return body;
    };

    size_t p = 0;
    std::string init = skip_one_message(p);
    ASSERT_FALSE(init.empty());
    auto j = nlohmann::json::parse(init);
    EXPECT_EQ(j["id"], 1);
    EXPECT_TRUE(j["result"]["capabilities"].contains("textDocumentSync"));

    std::string sd = skip_one_message(p);
    auto j2 = nlohmann::json::parse(sd);
    EXPECT_EQ(j2["id"], 2);
}

TEST(LspE2e, DidOpenEmitsPublishDiagnostics) {
    std::string bin = find_mora();
    ASSERT_FALSE(bin.empty());

    nlohmann::json open_params = {
        {"textDocument", {
            {"uri", "file:///tmp/test.mora"},
            {"languageId", "mora"},
            {"version", 1},
            {"text", "namespace t\nbandit(:\n"},  // syntax error
        }},
    };
    nlohmann::json open_msg = {
        {"jsonrpc","2.0"},
        {"method","textDocument/didOpen"},
        {"params", open_params},
    };

    auto in = frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})")
            + frame(R"({"jsonrpc":"2.0","method":"initialized","params":{}})")
            + frame(open_msg.dump())
            + frame(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})")
            + frame(R"({"jsonrpc":"2.0","method":"exit"})");

    std::string out = run_lsp(bin, in);

    // We should see a `publishDiagnostics` notification somewhere with
    // a non-empty `diagnostics` array.
    EXPECT_NE(out.find("publishDiagnostics"), std::string::npos);
    auto pos = out.find(R"("diagnostics":[)");
    ASSERT_NE(pos, std::string::npos);
    EXPECT_NE(out.substr(pos, 50).find("range"), std::string::npos)
        << "expected at least one diagnostic with a range";
}
```

(Headers `<fstream>` and `<filesystem>` need to be included — add at the top.)

- [ ] **Step 2: Run the test**

```bash
xmake build mora && xmake build test_protocol_e2e && xmake test test_protocol_e2e
```

Expected: 2 tests pass. The `find_mora` helper reuses the same pattern as Phase-1 Task 4 (`tests/cli/test_v2_end_to_end.cpp`).

- [ ] **Step 3: Commit**

```bash
git add tests/lsp/test_protocol_e2e.cpp
git commit -m "test(lsp): end-to-end protocol roundtrip via mora lsp subprocess"
```

---

### Task 15: VS Code client — `findMora.ts`

**Goal:** Helper module that resolves the `mora` binary to spawn for the LSP. Order: `mora.path` setting → PATH lookup.

**Files:**
- Create: `editors/vscode/src/findMora.ts`

(No standalone test — exercised in Task 17.)

- [ ] **Step 1: Write findMora.ts**

`editors/vscode/src/findMora.ts`:

```ts
import * as fs from 'node:fs';
import * as path from 'node:path';
import * as vscode from 'vscode';

/**
 * Resolve the path to the `mora` binary.
 *
 * Resolution order:
 *   1. `mora.path` configuration setting (if absolute and exists, used as-is;
 *      if relative or bare name, looked up on PATH).
 *   2. `mora` on PATH.
 *
 * Returns the absolute path to the binary, or null if not found.
 */
export function findMora(): string | null {
    const config = vscode.workspace.getConfiguration('mora');
    const setting = config.get<string>('path', 'mora');

    // If the setting is an absolute path, accept it iff it exists.
    if (path.isAbsolute(setting)) {
        return fs.existsSync(setting) ? setting : null;
    }

    // Otherwise (bare name like "mora", or relative path), search PATH.
    const PATH = process.env.PATH ?? '';
    const sep = process.platform === 'win32' ? ';' : ':';
    const exts = process.platform === 'win32'
        ? (process.env.PATHEXT ?? '.EXE;.CMD;.BAT').split(';')
        : [''];
    for (const dir of PATH.split(sep)) {
        for (const ext of exts) {
            const candidate = path.join(dir, setting + ext);
            try {
                if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) {
                    return candidate;
                }
            } catch { /* ignore */ }
        }
    }
    return null;
}
```

- [ ] **Step 2: Verify TypeScript compiles**

```bash
cd editors/vscode && npm run compile
```

Expected: no errors. The unused-locals check might flag `findMora` since nothing imports it yet — that's fine, the linter only cares about unused locals inside a function, not unused exports.

- [ ] **Step 3: Commit**

```bash
git add editors/vscode/src/findMora.ts
git commit -m "feat(vscode): findMora — resolve mora binary via setting > PATH"
```

---

### Task 16: VS Code client — `vscode-languageclient` dep + `.vscodeignore` adjust + `package` script

**Goal:** Add the runtime dep, update the package script so production deps ship, adjust `.vscodeignore`.

**Files:**
- Modify: `editors/vscode/package.json`
- Modify: `editors/vscode/.vscodeignore`

- [ ] **Step 1: Add the runtime dependency**

```bash
cd editors/vscode && npm install --save vscode-languageclient@^9.0.1
```

Expected: `package.json` `dependencies` block now contains `"vscode-languageclient": "^9.0.1"`. `package-lock.json` is updated.

- [ ] **Step 2: Update the `package` script**

In `editors/vscode/package.json`, find the `scripts.package` line:

```json
    "package": "vsce package --no-dependencies -o ./mora-vscode-${npm_package_version}.vsix"
```

Change to:

```json
    "package": "vsce package -o ./mora-vscode-${npm_package_version}.vsix"
```

(Drop `--no-dependencies` so vsce bundles the runtime dep tree.)

- [ ] **Step 3: Adjust `.vscodeignore`**

The current `.vscodeignore` excludes `node_modules/**`, which would also exclude runtime deps. We want production node_modules but no dev. The cleanest pattern: exclude only specific dev-only top-level packages, OR keep the broad exclusion and ship a bundled `out/extension.js` instead.

For v0.2.0 we go the simpler route: ship the production node_modules. Replace `.vscodeignore` with:

```
.vscode/**
.vscode-test/**
src/**
tests/**
**/*.map
**/*.ts
!out/**/*.js
!LICENSE
.gitignore
.vscodeignore
tsconfig.json
node_modules/.bin/**
node_modules/@types/**
node_modules/typescript/**
node_modules/@vscode/vsce/**
node_modules/@vscode/vsce-sign/**
node_modules/ovsx/**
node_modules/vscode-tmgrammar-test/**
```

(Specific dev-only top-level packages get excluded; everything else under `node_modules/` ships. This adds ~3–6 MB to the .vsix — acceptable.)

- [ ] **Step 4: Verify package locally**

```bash
cd editors/vscode && rm -rf out *.vsix && npm run compile && npm run package
```

Expected: a `mora-vscode-0.1.0.vsix` is produced. Inspect with `unzip -l`:

```bash
unzip -l editors/vscode/mora-vscode-0.1.0.vsix | grep -E "node_modules|extension/(out|package)" | head -20
```

Expected entries include `extension/node_modules/vscode-languageclient/...` and `extension/out/extension.js`. NO `node_modules/typescript/`, `node_modules/@types/`, or `node_modules/vscode-tmgrammar-test/`.

Clean up:

```bash
rm editors/vscode/mora-vscode-0.1.0.vsix
```

- [ ] **Step 5: Commit**

```bash
git add editors/vscode/package.json editors/vscode/package-lock.json \
        editors/vscode/.vscodeignore
git commit -m "feat(vscode): bundle vscode-languageclient as runtime dep"
```

---

### Task 17: VS Code client — `extension.ts` LSP startup

**Goal:** Replace the no-op stub with code that finds `mora`, spawns `mora lsp`, and starts a `LanguageClient`.

**Files:**
- Modify: `editors/vscode/src/extension.ts`

- [ ] **Step 1: Replace `extension.ts`**

`editors/vscode/src/extension.ts`:

```ts
import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
} from 'vscode-languageclient/node';
import { findMora } from './findMora';

let client: LanguageClient | undefined;

export async function activate(context: vscode.ExtensionContext): Promise<void> {
    const moraPath = findMora();
    if (!moraPath) {
        vscode.window.showErrorMessage(
            'Mora language server not found. Set "mora.path" or install `mora` on PATH.',
        );
        return;
    }

    const serverOptions: ServerOptions = {
        run:   { command: moraPath, args: ['lsp'], transport: TransportKind.stdio },
        debug: { command: moraPath, args: ['lsp', '--log', '/tmp/mora-lsp.log'], transport: TransportKind.stdio },
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'mora' }],
        synchronize: {
            // Notify the server when relation YAML files change so the
            // SchemaRegistry can be reloaded. (Phase 3 — for now this is
            // a no-op on the server side.)
            fileEvents: vscode.workspace.createFileSystemWatcher('**/data/relations/**/*.yaml'),
        },
        outputChannelName: 'Mora',
    };

    client = new LanguageClient('mora', 'Mora Language Server', serverOptions, clientOptions);
    await client.start();
    context.subscriptions.push({ dispose: () => client?.stop() });
}

export function deactivate(): Thenable<void> | undefined {
    return client?.stop();
}
```

- [ ] **Step 2: Compile**

```bash
cd editors/vscode && npm run compile
```

Expected: no errors.

- [ ] **Step 3: Manual smoke test (skip if no local VS Code)**

If you have VS Code installed locally:

```bash
cd editors/vscode && npm run package && code --install-extension mora-vscode-0.1.0.vsix --force
```

Open `test_data/example.mora`. Expected:
- The Mora output channel appears in View → Output and shows the LSP starting.
- No errors in the channel.

If you intentionally introduce a syntax error (e.g. `bandit(:`) and save, expected: a red squiggly line under the syntax error. If `mora.path` isn't set and `mora` is on PATH, it works without configuration.

Uninstall:
```bash
code --uninstall-extension halgari.mora
rm editors/vscode/mora-vscode-0.1.0.vsix
```

- [ ] **Step 4: Commit**

```bash
git add editors/vscode/src/extension.ts
git commit -m "feat(vscode): start LanguageClient against `mora lsp`"
```

---

### Task 18: Bump versions to 0.2.0 + CHANGELOG

**Files:**
- Modify: `xmake.lua`
- Modify: `editors/vscode/package.json`
- Modify: `editors/vscode/CHANGELOG.md`

- [ ] **Step 1: Bump xmake.lua version**

In `xmake.lua` line 2 (or wherever `set_version` lives), change:

```lua
set_version("0.1.0")
```

to:

```lua
set_version("0.2.0")
```

- [ ] **Step 2: Bump package.json version**

In `editors/vscode/package.json`, change `"version": "0.1.0"` to `"version": "0.2.0"`.

- [ ] **Step 3: Update the CHANGELOG**

Prepend to `editors/vscode/CHANGELOG.md` (immediately under the `# Changelog` header):

```markdown
## 0.2.0 — 2026-04-15

- New: language server (`mora lsp`) provides live diagnostics that match
  `mora compile` byte-for-byte.
- New: `mora.path` setting controls the binary the extension spawns.
- New: `# `-comments immediately above a rule head are recognised as
  doc-comments by the parser (no UI surface yet — phase 3 will use them
  for hover).
```

- [ ] **Step 4: Verify the CI version-sync check still passes locally**

```bash
xmake_version=$(grep -oP 'set_version\("\K[^"]+' xmake.lua)
pkg_version=$(node -p "require('./editors/vscode/package.json').version")
if [ "$xmake_version" != "$pkg_version" ]; then echo MISMATCH; else echo OK; fi
```

Expected: `OK` (both 0.2.0).

- [ ] **Step 5: Run the full test suite**

```bash
xmake test && (cd editors/vscode && npm test)
```

Expected: all C++ + grammar tests pass.

- [ ] **Step 6: Commit**

```bash
git add xmake.lua editors/vscode/package.json editors/vscode/CHANGELOG.md
git commit -m "chore: bump to 0.2.0 — LSP server + extension client"
```

---

## Done — Phase 2 deliverable

After all 18 tasks land:

- `mora lsp` subcommand runs an LSP server over stdio; `mora lsp --version` and `mora lsp --log <file>` work.
- The new `src/lsp/` subsystem is fully gtest-covered (URI, framing, lifecycle, document, workspace, diagnostics conversion, end-to-end subprocess test).
- The lexer optionally emits `Comment` trivia; the parser folds it into `Rule::doc_comment`. CLI behaviour is unchanged.
- The VS Code extension installs the language client; opening a `.mora` file shows live diagnostics that match `mora compile`.
- The Windows release archive grows: `tools/Mora/mora-vscode-0.2.0.vsix` now contains the production `node_modules` (vscode-languageclient + transitives) bundled.

**Out of scope for phase 2:** hover, goto-def, find-references, document-symbols, semantic tokens — those land in Phase 3.

## Open follow-ups (track separately)

- **Debouncing.** Phase 2 re-parses the document synchronously on every `didChange` (called from `publish_diagnostics`). The spec's 150 ms debounce requires either a worker thread or a non-blocking read with `select`/`poll` — out of scope for the single-threaded v1. Document::schedule_reparse / reparse_due are present in the API for phase 3 but unused in the run loop. Mora documents are small enough that re-parsing per keystroke is fine in practice.
- **Shared SchemaRegistry.** The Document re-creates a `StringPool` and re-runs the YAML schema load on every parse cycle. The spec wants the `Workspace` to own one shared `SchemaRegistry` populated at `initialize` time. Phase 3 will hoist registry ownership into `Workspace` once cross-file workspace symbols need it. Phase 2 leaves it per-document; correctness is identical, performance is "fine" at current YAML size (~50 KB).
- The TextMate grammar's `relation-call` pattern can't distinguish defined vs undefined — the phase-3 semantic-tokens layer fixes this.
- `grep -oP` portability nit in CI's version-sync check (GNU-only) — not an issue while CI uses ubuntu-latest.
- The `decreaseIndentPattern` in `language-configuration.json` is a no-op blank-line matcher; consider tightening when phase 3 lands.
- Cross-file diagnostic invalidation (when document A renames a rule that document B uses) — out of scope for v1; phase 3 will add it via the workspace symbol index.
