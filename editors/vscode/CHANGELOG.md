# Changelog

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

## 0.2.0 — 2026-04-15

- New: language server (`mora lsp`) provides live diagnostics that match
  `mora compile` byte-for-byte.
- New: `mora.path` setting controls the binary the extension spawns.
- New: `# `-comments immediately above a rule head are recognised as
  doc-comments by the parser (no UI surface yet — phase 3 will use them
  for hover).

## 0.1.0 — 2026-04-15

Initial release.

- TextMate grammar for `.mora` files.
- Language configuration: comments, brackets, auto-indent.
