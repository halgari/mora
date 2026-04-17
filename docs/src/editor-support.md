# Editor support

Mora ships with first-class VS Code support: a TextMate grammar for syntax
highlighting plus a Language Server (`mora lsp`) that gives you live
diagnostics, hover docs, goto-definition, find-references, and an outline
view — all driven by the same lexer/parser/sema as `mora compile`, so the
editor can never disagree with the build.

This page covers what's supported, how to install the extension, how to
configure it, and a brief tour of how the pieces fit together.

---

## What you get

| Feature | What it does | LSP method |
| --- | --- | --- |
| **Syntax highlighting** | TextMate grammar paints comments, keywords, atoms (`@Foo`), variables (`Capitalised`), built-in namespaces (`form`, `ref`, `player`, `world`, `event`), relation calls, numbers, strings, and operators. | (TextMate) |
| **Live diagnostics** | Parse + sema + type-check errors appear as red squigglies as you type. Identical to what `mora compile` would print. Re-parses are debounced ~150 ms after the last keystroke. | `textDocument/publishDiagnostics` |
| **Hover** | Hover any rule, relation, atom, or variable for a markdown popup with its signature, doc-comment, and (for atoms) FormID lookup. | `textDocument/hover` |
| **Goto definition** | `F12` on a rule call jumps to the rule head; on a variable use jumps to its binding site; on a built-in relation jumps to the YAML file under `data/relations/`. | `textDocument/definition` |
| **Find all references** | `Shift+F12` finds every call site / use site of the symbol. Workspace-wide for rules and relations; rule-scoped for variables. | `textDocument/references` |
| **Outline / breadcrumbs** | The Outline pane and `Ctrl+Shift+O` show your file's namespace and rule list. | `textDocument/documentSymbol` |
| **Workspace symbol search** | `Ctrl+T` fuzzy-finds rules across all open `.mora` files by `<namespace>.<rule>`. | `workspace/symbol` |
| **Semantic tokens** | Refines the TextMate colours with information only the compiler knows — defined vs undefined relations, bound vs free variables, resolved vs unresolved atoms. | `textDocument/semanticTokens/full` |
| **Comment toggle / auto-indent** | `Ctrl+/` toggles `#` comments. Pressing Enter after a rule-head colon indents the body. Continuation `# ` on Enter inside a comment block. | (language config) |
| **`mora compile` task** | `Ctrl+Shift+B` runs `mora compile` on the workspace. Errors land in the Problems pane via a problem-matcher even when the LSP isn't running. | (VS Code task) |

### Documentation comments

A run of `#` comments **immediately above** a rule head becomes that rule's
doc-comment (Python/Rust style). Hover on the rule shows it.

```mora
# Tag every NPC in the bandit faction.
# Used by `silver_weapons` for damage scaling.
bandit(NPC):
    form/npc(NPC)
    form/faction(NPC, @BanditFaction)
```

A blank line breaks the block — comments separated from the rule head by a
blank line are treated as ordinary trivia and don't show up on hover.

---

## Install

The extension ships in two places:

### Option A — sideload from the Mora release archive (offline)

The Windows release archive (`Mora-<version>-windows.zip` from the
[GitHub Release](https://github.com/halgari/mora/releases) or the
[Nexus mod page](https://www.nexusmods.com/skyrimspecialedition/mods/177372))
includes the packaged extension at:

```
Data/tools/Mora/mora-vscode-<version>.vsix
```

Install it via:

- **VS Code UI:** Extensions panel → `⋯` menu → **Install from VSIX…** →
  pick the `.vsix` file.
- **Command line:** `code --install-extension Data/tools/Mora/mora-vscode-<version>.vsix`.

This is the offline path — no marketplace account, no internet required
once you have the archive.

### Option B — VS Code Marketplace / Open VSX

Search for **"Mora"** in the Extensions panel (publisher `halgari`).
Marketplace publishing is on the roadmap; until it lands, use Option A.

### Make sure `mora.exe` is on PATH

The extension needs to spawn `mora lsp` (a subcommand of the same binary
that compiles patches). Put `mora.exe` (Windows) or `mora` (Linux) on your
PATH — the easiest way is to add `Data/tools/Mora/` from the release archive
to your PATH environment variable.

If you can't (or don't want to) modify PATH, set the **`mora.path`** VS
Code setting to the absolute path of the binary. See
[Configuration](#configuration) below.

---

## Configuration

All settings live under the `mora.*` prefix in VS Code's `settings.json`.

| Setting | Default | What it does |
| --- | --- | --- |
| `mora.path` | `mora` | Binary to spawn for `mora lsp`. Bare name → looked up on PATH; absolute path → used as-is. |
| `mora.dataDir` | `""` | Skyrim `Data/` folder. Used for atom (`@Foo`) resolution in hover. Optional — when unset, atom hover degrades to "not resolved". |
| `mora.trace.server` | `off` | LSP protocol logging. `messages` logs methods only; `verbose` logs full payloads. Output appears in the **Mora** output channel. |

Example `settings.json`:

```json
{
  "mora.path": "C:\\Mods\\Mora\\mora.exe",
  "mora.dataDir": "C:\\Steam\\steamapps\\common\\Skyrim Special Edition\\Data",
  "mora.trace.server": "off"
}
```

A `.vscode/settings.json` in your mora project will override the user-level
setting — useful if you have multiple projects pointing at different
Skyrim installs.

---

## How it works

Knowing the architecture isn't required to use the extension, but it
explains the failure modes and the deliberate trade-offs.

### Same compiler, two front-ends

```
                       ┌──────────────────────────────────┐
                       │           mora_lib (C++)         │
                       │  Lexer → Parser → NameResolver   │
                       │  → TypeChecker → DiagBag         │
                       │  → SchemaRegistry                │
                       └────────────┬─────────────────────┘
                                    │
                ┌───────────────────┴────────────────────┐
                │                                        │
        mora compile / check                       mora lsp
        (build patches)                       (LSP over stdio)
                │                                        │
                ▼                                        ▼
         mora_patches.bin                       VS Code editor
```

The `mora` CLI is one binary with multiple subcommands. `mora compile`
produces patch files; `mora check` does parse + sema and prints
diagnostics; `mora lsp` runs the same lexer/parser/sema pipeline behind a
JSON-RPC 2.0 server that VS Code talks to over stdio.

The benefit: **anything `mora compile` rejects, the editor will too, and
vice versa, at exactly the same source position.** No drift between
editor and build.

### Per-document parse cache + debouncing

`mora lsp` is single-threaded. It maintains one `Document` per open
`.mora` file, each with a parse cache. On every `didChange` notification
from the editor, the document's cache is marked stale and a re-parse is
scheduled for 150 ms in the future; the run loop polls stdin with a
50 ms timeout and processes deferred re-parses between polls. Rapid
keystrokes coalesce — only the final state of a 1-second-long edit
triggers a re-parse.

### SymbolIndex — the heart of semantic features

After every parse, the LSP walks the AST and builds a `SymbolIndex` — a
flat list of `(SourceSpan, SymbolKind, name, ...)` entries covering every
named token in the document. Hover, definition, references,
documentSymbol, and semanticTokens are all simple position-lookup or
linear-scan queries against this index.

Symbol kinds tracked:

| Kind | Example | Used by |
| --- | --- | --- |
| `RuleHead` | `bandit(NPC):` | hover, goto-def, references, documentSymbol, workspaceSymbol |
| `RuleCall` | `bandit(X)` inside another rule's body | hover, goto-def, references, semanticTokens |
| `Relation` | `weapon` in `form/weapon(W)` | hover, goto-def (→ YAML), references, semanticTokens |
| `Atom` | `@BanditFaction` | hover, references, semanticTokens |
| `VariableBinding` | first occurrence of `NPC` in a rule | hover, goto-def, references, semanticTokens |
| `VariableUse` | subsequent occurrences | hover, goto-def, references, semanticTokens |
| `Namespace` | `test.example` in `namespace test.example` | documentSymbol |

### Workspace state

The `Workspace` owns a single shared `SchemaRegistry` (the YAML relation
model), loaded at LSP `initialize` time. Every document parses against
the same registry, so adding a built-in relation in `data/relations/...`
lights up across every open file (after restarting the LSP).

The Workspace also owns an `EditorIdRegistry` for atom resolution — see
the next section.

### Atom resolution (best-effort)

Hovering on `@BanditFaction` ideally shows:

> Editor ID `BanditFaction` → FormID `0x0001BCB1` from `Skyrim.esm`
> (record `FACT`)

This requires the LSP to have read your ESPs. It's gated on the
`mora.dataDir` setting:

- **Set, valid:** the LSP scans your `Data/` folder (currently a stub —
  see [Limitations](#limitations) below) and resolves atoms it knows.
- **Unset or invalid:** atom hover degrades to "Atom not resolved (data
  dir not loaded)". Everything else still works — no error popups, no
  broken navigation.

### Diagnostics flow

Each Document caches the diagnostics it produced on its last parse. When
a re-parse completes, the LSP sends a `textDocument/publishDiagnostics`
notification scoped to that document's URI. Closing a document clears
its diagnostics on the editor side.

---

## VS Code task: `mora compile`

The extension contributes a task definition so you can wire up build
keystrokes. Add to your project's `.vscode/tasks.json`:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "mora compile",
      "type": "mora",
      "command": "compile",
      "dataDir": "${config:mora.dataDir}",
      "problemMatcher": "$mora",
      "group": { "kind": "build", "isDefault": true }
    }
  ]
}
```

`Ctrl+Shift+B` now runs `mora compile`. Errors land in the Problems pane
via the bundled `$mora` problem matcher (a regex that recognises Mora's
`file.mora:line:col: error: …` output).

---

## Troubleshooting

### "Mora language server not found"

The extension couldn't find a `mora` binary. Either:

- Add `tools/Mora/` to your PATH and restart VS Code, **or**
- Set `mora.path` in `settings.json` to the absolute path of `mora.exe`.

### "Atom not resolved" on every hover

`mora.dataDir` is unset or points at a folder without ESP files. This is
expected today — the EditorIdRegistry is currently a stub (see
[Limitations](#limitations)). Hover for everything else still works.

### Hover/definition not working at all

Open the **Mora** output channel (View → Output → "Mora") and set
`mora.trace.server: "verbose"` in settings. You should see `initialize`
and `didOpen` round-trips. If those are missing, the LSP didn't start —
check the binary path and that you can run `mora lsp --version` from
your shell.

### "this is too hard for me" — the LSP crashed

Run `mora lsp --version` from a terminal — it should print
`mora-lsp 0.3.0 (LSP 3.17)`. If the LSP genuinely crashes mid-session,
the editor will reconnect automatically; check the output channel for
the last message before the disconnect, file an issue with that.

## Limitations

These are real today; they're tracked as follow-ups in the project
notes:

- **Atom hover is a stub.** The plumbing is wired (`mora.dataDir`,
  `EditorIdRegistry`, hover degradation) but the actual ESP scan that
  populates the registry isn't implemented yet. Hover always reports
  "not resolved".
- **`semanticTokens/delta` not implemented.** The editor re-fetches the
  full token stream on every change. Fine at our document sizes; will
  matter if a single `.mora` file grows past ~5 000 lines.
- **Cross-file diagnostic invalidation isn't implemented.** Renaming a
  rule in `a.mora` won't re-publish stale diagnostics in `b.mora` until
  `b.mora` itself is touched.
- **Windows LSP isn't supported yet.** The poll loop uses POSIX
  `select(2)`. `mora lsp` on Windows prints an error and exits. The CLI
  (`mora compile`, `mora check`) works on Windows fine — it's only the
  language-server subcommand that's POSIX-only for now.
- **Qualified relation positions.** Hover on the `form` half of
  `form/weapon` works; on the `weapon` half it currently misses (the
  parser doesn't yet emit a separate name span for the relation). Hover
  on the rule head or on a variable reference works as expected.
- **Cross-document rule resolution.** A rule defined in `a.mora` and
  called from `b.mora` resolves correctly for goto-def and references,
  but `b.mora`'s diagnostics may flag the call as "undefined" because
  the per-document resolver doesn't see across files. This is being
  reworked.
- **`mora.path` change requires restart.** Changing the binary path
  doesn't restart the running LSP — use "Developer: Reload Window".

---

## Source layout for the curious

The extension code:

```
editors/vscode/
├── package.json              # extension manifest
├── language-configuration.json
├── syntaxes/mora.tmLanguage.json   # TextMate grammar
└── src/
    ├── extension.ts          # spawns LanguageClient against `mora lsp`
    └── findMora.ts           # binary discovery
```

The C++ language server:

```
include/mora/lsp/             # public API headers
src/lsp/
├── server.cpp                # JSON-RPC run loop, debounce-aware
├── framing.cpp               # Content-Length read/write
├── dispatch.cpp              # method-name → handler dispatcher
├── workspace.cpp             # documents + shared SchemaRegistry
├── document.cpp              # per-file parse cache + SymbolIndex
├── symbol_index.cpp          # AST walker + position lookup
├── diagnostics_convert.cpp   # mora::Diagnostic → LSP JSON
├── editor_id_registry.cpp    # atom resolution (stub)
├── uri.cpp                   # file:// ↔ path
└── handlers/
    ├── lifecycle.cpp         # initialize / shutdown
    ├── textsync.cpp          # didOpen / didChange / didClose
    ├── hover.cpp
    ├── definition.cpp
    ├── references.cpp
    ├── document_symbols.cpp
    ├── workspace_symbols.cpp
    └── semantic_tokens.cpp
```

The whole LSP is compiled into `mora_lib` and exposed through the `mora
lsp` subcommand — there's no separate binary to ship.
