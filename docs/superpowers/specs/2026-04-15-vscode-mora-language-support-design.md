# VS Code language support for Mora

Status: **Design — approved, awaiting implementation plan**
Date: 2026-04-15
Author: brainstormed with Claude

## Goal

First-class editing of `.mora` files in VS Code: syntax highlighting,
diagnostics, hover docs, goto-definition, find-references, document
symbols, workspace symbols, and a one-command `mora compile` build task.
Achieved via a TextMate grammar plus a Language Server Protocol (LSP)
server hosted **inside** the existing `mora` CLI binary as a `mora lsp`
subcommand, so editor diagnostics match `mora compile` output
byte-for-byte.

## Non-goals

- Code completion (`textDocument/completion`).
- Code actions / quick fixes.
- Rename refactoring (`textDocument/rename`).
- Inlay hints.
- Source formatting (no formatter exists yet).
- Call hierarchy.
- Workspace edits / refactoring.

Each may be revisited as a separate brainstorm later.

## Architectural decisions

1. **LSP lives in the CLI binary**, not a separate `mora-lsp` process,
   not a separate-language (TS/Rust) server. Reuses `mora_lib` (lexer,
   parser, name resolver, type checker, diag bag, schema registry)
   directly. No code duplication; perfect parity between editor and
   build errors.
2. **Doc strings are leading `#` comments** above a rule head
   (Python/Rust style). Requires lexer to keep comments as trivia and
   parser to attach them to the following `RuleDecl`.
3. **Atom (`@Foo`) goto-definition is not provided** — atoms reference
   ESP records, not source text. Hover gives FormID + record details;
   find-references still works inside `.mora` files.
4. **Same-repo extension** (`editors/vscode/`), version-locked to
   `xmake.lua`'s project version.
5. **Binary discovery is config-first, PATH-fallback** via a `mora.path`
   VS Code setting (default `"mora"`).
6. **Workspace = VS Code workspace folder.** All `.mora` files under it
   form one compilation unit, mirroring the existing `mora compile
   <dir>` behaviour.

## Repository layout

```
<repo root>
├── editors/
│   └── vscode/                              # NEW
│       ├── package.json
│       ├── tsconfig.json
│       ├── language-configuration.json
│       ├── syntaxes/
│       │   └── mora.tmLanguage.json
│       ├── src/
│       │   ├── extension.ts
│       │   └── findMora.ts
│       ├── README.md
│       └── CHANGELOG.md
├── src/
│   ├── lsp/                                 # NEW (compiled into mora_lib)
│   │   ├── server.cpp
│   │   ├── workspace.cpp
│   │   ├── document.cpp
│   │   ├── handlers/
│   │   │   ├── lifecycle.cpp
│   │   │   ├── textsync.cpp
│   │   │   ├── diagnostics.cpp
│   │   │   ├── hover.cpp
│   │   │   ├── definition.cpp
│   │   │   ├── references.cpp
│   │   │   ├── document_symbols.cpp
│   │   │   └── workspace_symbols.cpp
│   │   └── README.md
│   ├── lexer/                               # MODIFIED
│   ├── ast/                                 # MODIFIED — RuleDecl gains doc_comment
│   ├── parser/                              # MODIFIED — attach trivia to rule heads
│   └── main.cpp                             # MODIFIED — add `mora lsp` subcommand
├── include/mora/lsp/                        # NEW — public LSP API headers
└── docs/superpowers/specs/2026-04-15-vscode-mora-language-support-design.md
```

## Phase 1 — TextMate grammar + extension shell

### `editors/vscode/syntaxes/mora.tmLanguage.json`

Hand-written, ~150 lines. Token classes mapped to standard TextMate
scopes so themes paint them sensibly:

| Mora construct                                         | Scope                                       |
| ------------------------------------------------------ | ------------------------------------------- |
| Line comments                                          | `comment.line.number-sign.mora`             |
| Doc comments (≥1 `#` immediately above a rule head)    | `comment.line.documentation.mora`           |
| Top-level keywords (`namespace`, `requires`, `use`, `as`) | `keyword.control.mora`                   |
| Rule-body keywords (`not`, `=>`, `add`, `set`, `maintain`, `on`, `using`) | `keyword.operator.mora`  |
| Built-in namespaces (`form`, `ref`, `player`, `world`, `event`) | `entity.name.namespace.builtin.mora` |
| Relation reference (the `weapon` in `form/weapon(W)`)  | `entity.name.function.mora`                 |
| Atom literal (`@Foo`)                                  | `constant.other.atom.mora`                  |
| Variables (capitalised identifiers)                    | `variable.parameter.mora`                   |
| Numbers                                                | `constant.numeric.mora`                     |
| Strings                                                | `string.quoted.double.mora`                 |
| Comparison operators (`>=`, `<=`, `==`, `!=`, `<`, `>`)| `keyword.operator.comparison.mora`          |
| Punctuation (`(`, `)`, `,`, `:`)                       | `punctuation.*.mora`                        |

The TextMate grammar is the **fallback** highlighter. Once the LSP is
running it is overlaid by `semanticTokens` (phase 3) for resolved-vs-
unresolved distinctions.

### `editors/vscode/language-configuration.json`

- Line comment: `#`
- Brackets: `()`, `[]`
- Auto-closing pairs: `()`, `""`
- `onEnterRules`:
  - Line ends with `:` (rule head) → next line increases indent.
  - Line starts with `#` → next line continues with `# `.
- Word pattern: `[A-Za-z_][A-Za-z0-9_]*` so `form/weapon` selects
  `weapon` not the whole path.

### `editors/vscode/package.json` highlights

```json
{
  "name": "mora",
  "displayName": "Mora",
  "version": "0.1.0",
  "engines": { "vscode": "^1.85.0" },
  "categories": ["Programming Languages"],
  "main": "out/extension.js",
  "activationEvents": ["onLanguage:mora"],
  "contributes": {
    "languages": [{
      "id": "mora",
      "extensions": [".mora"],
      "configuration": "./language-configuration.json"
    }],
    "grammars": [{
      "language": "mora",
      "scopeName": "source.mora",
      "path": "./syntaxes/mora.tmLanguage.json"
    }],
    "configuration": {
      "title": "Mora",
      "properties": {
        "mora.path": {
          "type": "string",
          "default": "mora",
          "description": "Path to the mora binary; falls back to PATH lookup."
        },
        "mora.dataDir": {
          "type": "string",
          "default": "",
          "description": "Skyrim Data/ folder for atom (@Foo) resolution."
        },
        "mora.trace.server": {
          "type": "string",
          "enum": ["off", "messages", "verbose"],
          "default": "off"
        }
      }
    }
  }
}
```

### Phase-1 deliverable

Opening any `.mora` file paints it correctly. `Ctrl+/` toggles
comments. Bracket matching, indent-on-Enter, auto-closing pairs all
work. No language server running yet.

## Phase 2 — `mora lsp` subcommand + diagnostics

### Process model

- VS Code spawns `mora lsp` as a child process (one per workspace
  folder).
- JSON-RPC 2.0 over stdio with `Content-Length` framing.
- Server is single-threaded; debounced re-parse on document change.
- JSON parsing via `nlohmann_json` (new xmake package, header-only).
- JSON-RPC framing written in-house in `src/lsp/server.cpp` (~200 lines).

### CLI surface

```
mora lsp                 # speak LSP over stdio
mora lsp --log FILE      # also write a server log
mora lsp --version       # print server-protocol version + mora_lib version
```

No other flags.

### `Workspace` (in `src/lsp/workspace.cpp`)

- Owns a single `SchemaRegistry` (the YAML model — loaded once at
  startup, shared across all documents).
- Owns `URI → Document`.
- Builds a workspace symbol index on `initialize` by parsing every
  `.mora` file under the workspace root.
- Resolves cross-file references via the index.

### `Document` (in `src/lsp/document.cpp`)

- Holds latest text (full sync via `textDocument/didChange`).
- Holds parse cache: `tokens → AST → resolved AST → diagnostics`.
- Re-runs the pipeline whenever text changes (debounced ~150 ms).
- Exposes a uniform query API:
  - `tokenAt(line, col) → semantic token info`
  - `definitionOf(symbol) → URI + range, or null`
  - `referencesOf(symbol) → list of URI + range`
  - `documentSymbols() → outline tree`
  - `hoverAt(line, col) → markdown string`

### Re-parse strategy

On every `didChange`, schedule a 150 ms-debounced re-parse of the
changed document only. Workspace-symbol updates derive from each
document's symbol table — no separate workspace-wide reparse.
Cross-file diagnostics for *consumers* of a changed namespace are
recomputed for affected documents, also debounced.

### Restructuring touch points

- `mora_lib::Lexer` — emit comments as trivia tokens instead of
  skipping (currently `skip_comment()` discards). Trivia attaches to
  the next non-trivia token.
- `mora_lib::Parser` — when constructing a `RuleDecl`, scan the leading
  trivia of its first token for contiguous `#` comments → fold into
  `doc_comment` field.
- `mora_lib::ast::RuleDecl` — add `std::optional<DocComment> doc_comment`.
- `mora_lib::DiagBag` — add `drain_for_uri(URI) → std::vector<Diagnostic>`
  for `publishDiagnostics`.
- `mora_lib::SchemaRegistry` — expose enumeration of relations and
  their `docs:` strings for hover.
- `mora_lib::NameResolver` / `TypeChecker` — record a side-table
  `(file, line, col) → resolved-symbol-id` during the pass, so the LSP
  can answer goto-def / find-references / hover queries without
  re-running resolution.

### Phase-2 deliverable

Save a `.mora` file → red squigglies appear at the same locations
`mora compile` reports. Type into a `.mora` file → squigglies update
within ~150 ms.

## Phase 3 — LSP semantic features

### `textDocument/hover`

| Token kind                            | Hover content |
| ------------------------------------- | ------------- |
| Built-in relation (`form/weapon`)     | Markdown signature, blank line, YAML `docs:` string, blank line, "Source: static · ESP record `WEAP`". |
| User-defined rule head or call        | Markdown signature reconstructed from sema, blank line, the doc-comment block above the rule's definition (verbatim, `#` stripped), blank line, "Defined in `test/example.mora:7`". |
| Atom (`@BanditFaction`)               | Markdown atom name, blank line, "Editor ID `BanditFaction` → FormID `0x0001BCB1` from `Skyrim.esm` (record FACT)". If unresolved: "⚠ Atom does not resolve to any loaded ESP record." |
| Variable (`NPC`, `Level`)             | Markdown `NPC: FormRef` (or whatever sema inferred), "Bound by `bandit(NPC)` on line 7." |
| Keyword                               | No hover. |

Atom resolution uses the workspace's `mora.dataDir` setting. If unset,
atoms hover with a FormID-unknown placeholder rather than failing.

### `textDocument/definition`

| Symbol kind             | Goto-def behaviour |
| ----------------------- | ------------------ |
| User-defined rule call  | Jumps to the `RuleDecl`'s head token. |
| Built-in relation       | Jumps to its YAML definition line in `data/relations/<ns>/<name>.yaml`. |
| Atom                    | No jump (returns empty result). |
| Variable                | Jumps to its binding site (the predicate call where it first appears in the rule body). |
| Namespace declaration   | Jumps to the canonical declaring file if multiple files share a namespace. |

### `textDocument/references`

| Symbol kind             | Find-refs behaviour |
| ----------------------- | ------------------- |
| User-defined rule       | All call sites across the workspace, including the head itself. |
| Built-in relation       | All call sites across the workspace (not the YAML — YAML is the definition). |
| Atom                    | All `.mora` files mentioning `@<name>`. |
| Variable                | All occurrences within the rule body where it's bound. |

Implementation: linear scan of cached ASTs, no separate inverted
index (workspaces are small enough in v1).

### `textDocument/documentSymbol`

Returns a tree:
```
namespace test.example                     [Namespace]
├── bandit(NPC)                            [Function]
├── tag_bandits(NPC)                       [Function]
└── …
```

Powers the Outline pane and `Ctrl+Shift+O`.

### `workspace/symbol`

`Ctrl+T` query. Searches across all `.mora` files in the workspace.
Matches against `<namespace>.<rule>` (e.g. `test.example.bandit`) with
substring fuzzy match.

### `textDocument/semanticTokens` (full + delta)

Overlays the TextMate grammar with semantic distinctions:

- **Defined** rule → `function`
- **Built-in** relation → `function.defaultLibrary`
- **Undefined** rule call → `function.deprecated`
- **Bound** variable → `parameter`
- **Free** variable (sema error: not bound) → `parameter.deprecated`
- **Resolved** atom → `enum.member`
- **Unresolved** atom → `enum.member.deprecated`

Makes "is my rule actually defined" visible without reading the
diagnostics column.

### Phase-3 deliverable

`F12` goes to the rule's definition. `Shift+F12` finds all callers.
Hover shows docs from the YAML model and from `#`-comment doc
strings. Outline pane works. `Ctrl+T` finds rules across the workspace.
Unresolved relations and atoms are visibly marked even before
diagnostics arrive.

## Phase 4 — Build integration

### Tasks

`editors/vscode/package.json` contributes a default task type:

```json
"contributes": {
  "taskDefinitions": [{
    "type": "mora",
    "required": ["command"],
    "properties": {
      "command":  { "type": "string", "enum": ["check", "compile", "inspect", "info"] },
      "dataDir":  { "type": "string" },
      "output":   { "type": "string" }
    }
  }]
}
```

Provides task templates so `Ctrl+Shift+P` → "Tasks: Run Task" → "Mora:
compile" works out of the box.

### Problem matcher

Mora's diagnostic output format is stable enough for a regex matcher:

```
"problemMatchers": [{
  "name": "mora",
  "owner": "mora",
  "fileLocation": ["relative", "${workspaceFolder}"],
  "pattern": {
    "regexp": "^(.*\\.mora):(\\d+):(\\d+):\\s+(error|warning|note):\\s+(.*)$",
    "file": 1, "line": 2, "column": 3, "severity": 4, "message": 5
  }
}]
```

So `mora compile` errors land in the Problems pane even without the
LSP running (e.g. CI logs pasted into the editor).

### Phase-4 deliverable

`Ctrl+Shift+B` invokes `mora compile` on the workspace. Errors light
up in the Problems pane. Output channel shows the run.

## Build pipeline

### `xmake.lua` changes

Add the LSP source files to `mora_lib`:

```lua
add_files(..., "src/lsp/*.cpp", "src/lsp/handlers/*.cpp")
```

`mora` (the binary) gains nothing new — `main.cpp` adds:

```cpp
else if (cmd == "lsp") return mora::lsp::run(argc - 2, argv + 2);
```

New xmake package: `add_requires("nlohmann_json")`, marked
`{public = true}` on `mora_lib` so headers propagate. Header-only,
~400 KB compiled into `mora.exe`. No new build target.

### VS Code extension build (Node toolchain)

`editors/vscode/package.json` scripts:

```json
"scripts": {
  "compile":       "tsc -p .",
  "package":       "vsce package -o ../../dist/mora-vscode-${VERSION}.vsix",
  "publish:vsce":  "vsce publish",
  "publish:ovsx":  "ovsx publish"
}
```

Runtime deps: `vscode-languageclient`.
Dev deps: `typescript`, `@types/vscode`, `@vscode/vsce`, `ovsx`,
`eslint`.

### CI changes (`.github/workflows/ci.yml`)

**New job `extension`** (parallel with `linux` and `windows`,
`ubuntu-latest`):

1. Checkout, setup Node 20.
2. `cd editors/vscode && npm ci`
3. `npm run compile` (typecheck)
4. `npm run package` → `dist/mora-vscode-<version>.vsix`
5. Upload `.vsix` as workflow artifact.

**Windows job — extend the packaging step** to depend on `extension`
job (`needs: extension`), download the `.vsix` artifact, place it in
the release archive at `tools/Mora/mora-vscode-<version>.vsix`.

**Marketplace publishing on `v*` tags** (after Nexus upload):

```yaml
- name: Publish to VS Code Marketplace
  if: startsWith(github.ref, 'refs/tags/v') && env.VSCE_PAT != ''
  run: cd editors/vscode && npx vsce publish --packagePath ../../dist/mora-vscode-*.vsix
  env:
    VSCE_PAT: ${{ secrets.VSCE_PAT }}

- name: Publish to Open VSX
  if: startsWith(github.ref, 'refs/tags/v') && env.OVSX_PAT != ''
  run: cd editors/vscode && npx ovsx publish ../../dist/mora-vscode-*.vsix
  env:
    OVSX_PAT: ${{ secrets.OVSX_PAT }}
```

Both publish-PATs are new optional secrets — if absent, those steps
skip silently. The Nexus archive still ships with the bundled `.vsix`
so users always have a sideload path.

### Versioning

The extension's `package.json` `version` is the source of truth for
marketplace publishing. CI fails the `extension` job if
`editors/vscode/package.json` `version` ≠ `xmake.lua`'s
`set_version("…")`. Bump both by hand in v1; automate via a
`tools/sync_version.py` later if needed.

### Cold-build cost

- Extension build: ~30 s on a clean GitHub runner (npm install + tsc).
- LSP code in `mora_lib`: adds ~3 s to the C++ compile.
- Net new wall-clock: ~30 s (parallel job).

## Phasing summary

| Phase | Scope | Ships when |
| ----- | ----- | ---------- |
| 1     | TextMate grammar + extension shell + language config. No LSP. | First. Standalone. |
| 2     | `mora lsp` subcommand. Lexer/parser changes for trivia/doc-comments. Diagnostics flow into the editor. | Depends on phase 1's extension shell to host the LSP client. |
| 3     | Hover, goto-def, find-references, document symbols, workspace symbols, semantic tokens. | Builds on phase 2's parse cache + symbol resolution. |
| 4     | Task contributions + problem matcher. | Depends on phase 1 (task contributions live in `package.json`). |

Phase 4 can ship in parallel with phase 1 (no LSP dependency); the
table orders them by user-perceived value.

## Open questions deferred to implementation plan

- Exact debouncing strategy for cross-file diagnostic invalidation.
- Whether to vendor an LSP framing helper or write framing in-house
  (current lean: in-house, ~200 lines).
- Atom hover's ESP loading cost — first hover may be slow if the user
  has a 250-plugin load order. Likely needs a background warmup.
- Test strategy: gtest cases for `Workspace`/`Document` query API are
  straightforward; protocol-level tests likely need a tiny harness
  that pipes JSON-RPC through `mora lsp`.

## Out of scope (explicit)

Code completion, code actions, rename, inlay hints, formatting, call
hierarchy, workspace edits, refactoring. Each can be its own
brainstorm later.
