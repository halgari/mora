# Mora language support for VS Code

Syntax highlighting and editor integration for the
[Mora](https://github.com/halgari/mora) declarative-Datalog language for
Skyrim Special Edition.

## Phase 1 (this release)

- Syntax highlighting for `.mora` files.
- Comment toggling (`Ctrl+/`) and bracket matching.
- Auto-indent after rule heads.

## Coming next

- A `mora lsp` language server with diagnostics, hover, goto-definition,
  and find-references — see the project spec.

## Configuration

| Setting | Default | Description |
| --- | --- | --- |
| `mora.path` | `mora` | Path to the `mora` binary; falls back to PATH lookup. |
| `mora.dataDir` | `""` | Skyrim Data folder for atom (`@Foo`) resolution. |
| `mora.trace.server` | `off` | LSP protocol logging. |

(Settings are present in this release but only `mora.path` will be
consumed once phase 2 ships.)
