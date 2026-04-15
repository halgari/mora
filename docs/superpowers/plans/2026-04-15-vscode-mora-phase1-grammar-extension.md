# VS Code Mora — Phase 1: TextMate grammar + extension shell

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A VS Code extension that registers the `mora` language, paints `.mora` files with a TextMate grammar, and provides comment toggling + auto-indent. No language server yet — that's phase 2.

**Architecture:** Self-contained TypeScript extension under `editors/vscode/`. Shipped as a `.vsix` via a new `extension` CI job. Bundled into the Windows release archive at `tools/Mora/mora-vscode-<version>.vsix`. The `extension.ts` is a no-op stub in this phase; activation handlers come in phase 2.

**Tech Stack:** TypeScript 5.x, Node 20, `@vscode/vsce`, `vscode-tmgrammar-test`, GitHub Actions.

**Spec reference:** `docs/superpowers/specs/2026-04-15-vscode-mora-language-support-design.md` — Section 1 (repo layout), Phase 1 section (grammar + language config).

---

## File structure

Created in this plan:

| Path | Purpose |
| --- | --- |
| `editors/vscode/.gitignore` | Ignore `node_modules/`, `out/`, `*.vsix` |
| `editors/vscode/.vscodeignore` | Files to exclude from the packaged `.vsix` |
| `editors/vscode/package.json` | Extension manifest + npm scripts + contributions |
| `editors/vscode/tsconfig.json` | TypeScript compiler config |
| `editors/vscode/README.md` | Marketplace listing copy |
| `editors/vscode/CHANGELOG.md` | Version history (starts with 0.1.0) |
| `editors/vscode/language-configuration.json` | Comments, brackets, autoclose, onEnter |
| `editors/vscode/syntaxes/mora.tmLanguage.json` | TextMate grammar |
| `editors/vscode/src/extension.ts` | Stub `activate`/`deactivate` |
| `editors/vscode/tests/grammar/comments.test.mora` | Grammar test for comments |
| `editors/vscode/tests/grammar/keywords.test.mora` | Grammar test for keywords |
| `editors/vscode/tests/grammar/identifiers.test.mora` | Grammar test for atoms / variables / relations |
| `editors/vscode/tests/grammar/literals.test.mora` | Grammar test for numbers / strings |
| `editors/vscode/tests/grammar/operators.test.mora` | Grammar test for operators / punctuation |
| `editors/vscode/tests/grammar/example.test.mora` | Integration test against `test_data/example.mora` |

Modified in this plan:

| Path | Change |
| --- | --- |
| `.github/workflows/ci.yml` | New `extension` job; Windows packaging step pulls the `.vsix` |
| `README.md` (root) | Short paragraph linking to the extension dir |

---

## Conventions

- **Commits:** one per task; conventional-commits style (`feat(vscode):`, `chore(ci):`, etc.).
- **Working dir:** all `npm` commands run from `editors/vscode/` unless stated otherwise. The plan writes `cd editors/vscode && …` explicitly each time so steps are self-contained.
- **Node version:** 20.x (matches CI's `actions/setup-node@v4 with: node-version: 20`).

---

### Task 1: Scaffold extension package

**Files:**
- Create: `editors/vscode/.gitignore`
- Create: `editors/vscode/.vscodeignore`
- Create: `editors/vscode/tsconfig.json`
- Create: `editors/vscode/src/extension.ts`
- Create: `editors/vscode/README.md`
- Create: `editors/vscode/CHANGELOG.md`

- [ ] **Step 1: Create the directory and `.gitignore`**

```bash
mkdir -p editors/vscode/src editors/vscode/syntaxes editors/vscode/tests/grammar
```

`editors/vscode/.gitignore`:

```
node_modules/
out/
*.vsix
.vscode-test/
```

- [ ] **Step 2: Create `.vscodeignore`** (files NOT to ship inside the `.vsix`)

`editors/vscode/.vscodeignore`:

```
.vscode/**
.vscode-test/**
src/**
tests/**
**/*.map
**/*.ts
!out/**/*.js
.gitignore
.vscodeignore
tsconfig.json
node_modules/**
```

- [ ] **Step 3: Create `tsconfig.json`**

`editors/vscode/tsconfig.json`:

```json
{
  "compilerOptions": {
    "module": "commonjs",
    "target": "ES2022",
    "outDir": "out",
    "lib": ["ES2022"],
    "sourceMap": true,
    "rootDir": "src",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "noImplicitReturns": true,
    "noFallthroughCasesInSwitch": true,
    "noUnusedLocals": true
  },
  "include": ["src/**/*.ts"],
  "exclude": ["node_modules", "out", "tests"]
}
```

- [ ] **Step 4: Create the stub `extension.ts`**

`editors/vscode/src/extension.ts`:

```ts
import * as vscode from 'vscode';

// Phase 1: no activation work is required. The grammar and language
// configuration are wired up entirely through `contributes` in
// package.json. Phase 2 will add LSP client startup here.
export function activate(_context: vscode.ExtensionContext): void {
  // intentionally empty
}

export function deactivate(): void {
  // intentionally empty
}
```

- [ ] **Step 5: Create `README.md`**

`editors/vscode/README.md`:

```markdown
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
```

- [ ] **Step 6: Create `CHANGELOG.md`**

`editors/vscode/CHANGELOG.md`:

```markdown
# Changelog

## 0.1.0 — 2026-04-15

Initial release.

- TextMate grammar for `.mora` files.
- Language configuration: comments, brackets, auto-indent.
```

- [ ] **Step 7: Commit**

```bash
git add editors/vscode/.gitignore editors/vscode/.vscodeignore \
        editors/vscode/tsconfig.json editors/vscode/src/extension.ts \
        editors/vscode/README.md editors/vscode/CHANGELOG.md
git commit -m "feat(vscode): scaffold extension directory with stub activate()"
```

---

### Task 2: Create `package.json` and install dev dependencies

**Files:**
- Create: `editors/vscode/package.json`
- Create: `editors/vscode/package-lock.json` (generated)

- [ ] **Step 1: Write `package.json`**

`editors/vscode/package.json`:

```json
{
  "name": "mora",
  "displayName": "Mora",
  "description": "Syntax highlighting and language support for the Mora declarative-Datalog language for Skyrim Special Edition.",
  "version": "0.1.0",
  "publisher": "halgari",
  "license": "MPL-2.0",
  "repository": {
    "type": "git",
    "url": "https://github.com/halgari/mora.git",
    "directory": "editors/vscode"
  },
  "engines": {
    "vscode": "^1.85.0"
  },
  "categories": ["Programming Languages"],
  "keywords": ["mora", "skyrim", "skse", "datalog"],
  "main": "out/extension.js",
  "activationEvents": ["onLanguage:mora"],
  "contributes": {
    "languages": [
      {
        "id": "mora",
        "aliases": ["Mora", "mora"],
        "extensions": [".mora"],
        "configuration": "./language-configuration.json"
      }
    ],
    "grammars": [
      {
        "language": "mora",
        "scopeName": "source.mora",
        "path": "./syntaxes/mora.tmLanguage.json"
      }
    ],
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
          "default": "off",
          "description": "LSP protocol logging level."
        }
      }
    }
  },
  "scripts": {
    "compile": "tsc -p .",
    "watch": "tsc -p . --watch",
    "test:grammar": "vscode-tmgrammar-test \"tests/grammar/**/*.test.mora\"",
    "test": "npm run compile && npm run test:grammar",
    "package": "vsce package --no-dependencies -o ./mora-vscode-${npm_package_version}.vsix"
  },
  "devDependencies": {
    "@types/vscode": "^1.85.0",
    "@vscode/vsce": "^3.2.0",
    "ovsx": "^0.10.0",
    "typescript": "^5.6.0",
    "vscode-tmgrammar-test": "^0.1.3"
  }
}
```

- [ ] **Step 2: Install dev dependencies**

```bash
cd editors/vscode && npm install
```

Expected: `package-lock.json` is created, `node_modules/` is populated, no errors.

- [ ] **Step 3: Verify the toolchain**

```bash
cd editors/vscode && npx tsc --version && npx vsce --version && npx vscode-tmgrammar-test --help
```

Expected: prints versions for `tsc`, `vsce`, and the help text for `vscode-tmgrammar-test` (single command).

- [ ] **Step 4: Commit**

```bash
git add editors/vscode/package.json editors/vscode/package-lock.json
git commit -m "feat(vscode): add package.json with grammar + config contributions"
```

---

### Task 3: Verify the empty grammar file is wired up

**Files:**
- Create: `editors/vscode/syntaxes/mora.tmLanguage.json`
- Create: `editors/vscode/language-configuration.json`

This task creates *placeholder* files so `vsce package` doesn't error. Real grammar content lands in tasks 5–9; real language config in task 10.

- [ ] **Step 1: Write the placeholder grammar**

`editors/vscode/syntaxes/mora.tmLanguage.json`:

```json
{
  "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
  "name": "Mora",
  "scopeName": "source.mora",
  "fileTypes": ["mora"],
  "patterns": []
}
```

- [ ] **Step 2: Write the placeholder language configuration**

`editors/vscode/language-configuration.json`:

```json
{
  "comments": {
    "lineComment": "#"
  },
  "brackets": [["(", ")"]],
  "autoClosingPairs": [
    { "open": "(", "close": ")" },
    { "open": "\"", "close": "\"" }
  ]
}
```

- [ ] **Step 3: Compile and confirm `vsce package` runs**

```bash
cd editors/vscode && npm run compile && npx vsce package --no-dependencies -o /tmp/mora-vscode-test.vsix
```

Expected: produces `/tmp/mora-vscode-test.vsix` without errors. `vsce` may print a warning about missing `LICENSE` — fix in step 4.

- [ ] **Step 4: Add a `LICENSE` symlink to the root MPL-2.0 license**

```bash
cd editors/vscode && ln -s ../../LICENSE LICENSE
```

(`vsce` requires a `LICENSE` file inside the extension dir for marketplace publication. Symlink keeps a single source of truth.)

Re-run packaging to confirm clean:

```bash
cd editors/vscode && npx vsce package --no-dependencies -o /tmp/mora-vscode-test.vsix
```

Expected: no LICENSE warnings. Delete the test artifact afterwards: `rm /tmp/mora-vscode-test.vsix`.

- [ ] **Step 5: Add `LICENSE` to the .vscodeignore exception list**

Edit `editors/vscode/.vscodeignore` — add `!LICENSE` near the bottom so the symlinked file *is* shipped in the `.vsix`:

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
node_modules/**
```

- [ ] **Step 6: Commit**

```bash
git add editors/vscode/syntaxes/mora.tmLanguage.json \
        editors/vscode/language-configuration.json \
        editors/vscode/LICENSE editors/vscode/.vscodeignore
git commit -m "feat(vscode): wire up placeholder grammar + language config"
```

---

### Task 4: Set up grammar test harness

**Files:**
- Create: `editors/vscode/tests/grammar/smoke.test.mora`

`vscode-tmgrammar-test` runs assertion-style tests where each line of source is followed by `# ^^^^^ scope.name` lines pointing at characters and asserting their TextMate scope.

- [ ] **Step 1: Write a smoke test that should always pass**

The placeholder grammar paints nothing, so a test that asserts only the universal scope works.

`editors/vscode/tests/grammar/smoke.test.mora`:

```mora
# SYNTAX TEST "source.mora"
namespace test
# <----------- source.mora
```

The `# SYNTAX TEST "source.mora"` directive on the first line tells the runner which scope is the file's root. The `# <-` annotation asserts that the character at the start of `namespace` belongs to the `source.mora` scope — true for *any* token (it's the file scope).

- [ ] **Step 2: Run the test**

```bash
cd editors/vscode && npm run test:grammar
```

Expected output: ends with a green `1 passed` line (or similar — exact wording depends on `vscode-tmgrammar-test` version, but no failures).

- [ ] **Step 3: Confirm a failing test fails**

Edit `smoke.test.mora` and change `source.mora` to `source.bogus`:

```mora
# SYNTAX TEST "source.mora"
namespace test
# <----------- source.bogus
```

Run again:

```bash
cd editors/vscode && npm run test:grammar
```

Expected: 1 failure. The runner names the line and shows the actual vs. expected scope.

Revert the file to assert `source.mora`.

- [ ] **Step 4: Commit**

```bash
git add editors/vscode/tests/grammar/smoke.test.mora
git commit -m "test(vscode): smoke test for grammar harness"
```

---

### Task 5: Comment highlighting (TDD)

**Files:**
- Create: `editors/vscode/tests/grammar/comments.test.mora`
- Modify: `editors/vscode/syntaxes/mora.tmLanguage.json`

- [ ] **Step 1: Write the failing test**

`editors/vscode/tests/grammar/comments.test.mora`:

```mora
# SYNTAX TEST "source.mora"

# this is a comment
# <- comment.line.number-sign.mora
# ^^^^^^^^^^^^^^^^^^ comment.line.number-sign.mora

namespace test  # trailing
#               ^ comment.line.number-sign.mora
```

- [ ] **Step 2: Run the test, confirm it fails**

```bash
cd editors/vscode && npm run test:grammar
```

Expected: failures on the comment lines (placeholder grammar paints nothing).

- [ ] **Step 3: Add the comment pattern to the grammar**

Replace `editors/vscode/syntaxes/mora.tmLanguage.json` with:

```json
{
  "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
  "name": "Mora",
  "scopeName": "source.mora",
  "fileTypes": ["mora"],
  "patterns": [
    { "include": "#comments" }
  ],
  "repository": {
    "comments": {
      "match": "#.*$",
      "name": "comment.line.number-sign.mora"
    }
  }
}
```

- [ ] **Step 4: Run the test, confirm it passes**

```bash
cd editors/vscode && npm run test:grammar
```

Expected: `2 passed` (smoke + comments).

- [ ] **Step 5: Commit**

```bash
git add editors/vscode/tests/grammar/comments.test.mora \
        editors/vscode/syntaxes/mora.tmLanguage.json
git commit -m "feat(vscode): highlight # line comments"
```

---

### Task 6: Keyword highlighting (TDD)

The full keyword list, taken from `src/lexer/lexer.cpp:143–160`:

| Group | Keywords |
| --- | --- |
| Top-level / declarations | `namespace`, `requires`, `mod`, `use`, `as`, `refer`, `only`, `import_spid`, `import_kid` |
| Rule body / quantifiers | `not`, `or`, `in`, `maintain`, `on`, `set`, `add`, `sub`, `remove` |
| Built-in namespaces | `form`, `ref`, `player`, `world`, `event` |

**Files:**
- Create: `editors/vscode/tests/grammar/keywords.test.mora`
- Modify: `editors/vscode/syntaxes/mora.tmLanguage.json`

- [ ] **Step 1: Write the failing test**

`editors/vscode/tests/grammar/keywords.test.mora`:

```mora
# SYNTAX TEST "source.mora"

namespace test.example
# <-------- keyword.control.mora

requires mod("Skyrim.esm")
# <------- keyword.control.mora
#        ^^^ keyword.control.mora

use form as f
# <- keyword.control.mora
#       ^^ keyword.control.mora

bandit(NPC):
    form/npc(NPC)
#   ^^^^ entity.name.namespace.builtin.mora
    not form/keyword(NPC, @Foo)
#   ^^^ keyword.operator.mora
    add form/keyword(NPC, @Bar)
#   ^^^ keyword.operator.mora
```

The `=>` operator is intentionally absent from this test — it's an
operator, not a keyword, and gets its own assertion in task 9.

- [ ] **Step 2: Run the test, confirm it fails**

```bash
cd editors/vscode && npm run test:grammar
```

Expected: failures on every keyword assertion.

- [ ] **Step 3: Add keyword patterns to the grammar**

Replace `editors/vscode/syntaxes/mora.tmLanguage.json`:

```json
{
  "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
  "name": "Mora",
  "scopeName": "source.mora",
  "fileTypes": ["mora"],
  "patterns": [
    { "include": "#comments" },
    { "include": "#keywords-toplevel" },
    { "include": "#keywords-body" },
    { "include": "#namespaces-builtin" }
  ],
  "repository": {
    "comments": {
      "match": "#.*$",
      "name": "comment.line.number-sign.mora"
    },
    "keywords-toplevel": {
      "match": "\\b(namespace|requires|mod|use|as|refer|only|import_spid|import_kid)\\b",
      "name": "keyword.control.mora"
    },
    "keywords-body": {
      "match": "\\b(not|or|in|maintain|on|set|add|sub|remove)\\b",
      "name": "keyword.operator.mora"
    },
    "namespaces-builtin": {
      "match": "\\b(form|ref|player|world|event)(?=/)",
      "name": "entity.name.namespace.builtin.mora"
    }
  }
}
```

The `(?=/)` lookahead in `namespaces-builtin` matches `form` *only* when followed by `/` — so the bare word `form` (e.g. as a variable name) isn't mistakenly painted.

The `=>` operator is *not* included here — it'll be handled in task 9 (operators).

- [ ] **Step 4: Run the test, confirm it passes**

```bash
cd editors/vscode && npm run test:grammar
```

Expected: 3 passed (smoke + comments + keywords).

- [ ] **Step 5: Commit**

```bash
git add editors/vscode/tests/grammar/keywords.test.mora \
        editors/vscode/syntaxes/mora.tmLanguage.json
git commit -m "feat(vscode): highlight keywords and built-in namespaces"
```

---

### Task 7: Atom + variable + relation reference highlighting (TDD)

Mora has three identifier flavours:
- **Atoms**: `@Capitalised` (an editor-ID reference). Scope: `constant.other.atom.mora`.
- **Variables**: `Capitalised` (no leading `@`). Scope: `variable.parameter.mora`.
- **Relation references**: `lowercase_word` after a `/` or as the head of a call. Scope: `entity.name.function.mora`.

Phase-1 caveat (per spec): the relation-reference scope paints *every* lowercase identifier in callable position the same way, defined or not. Semantic tokens (phase 3) refine this.

**Files:**
- Create: `editors/vscode/tests/grammar/identifiers.test.mora`
- Modify: `editors/vscode/syntaxes/mora.tmLanguage.json`

- [ ] **Step 1: Write the failing test**

`editors/vscode/tests/grammar/identifiers.test.mora`:

```mora
# SYNTAX TEST "source.mora"

bandit(NPC):
# <----- entity.name.function.mora
#      ^^^ variable.parameter.mora
    form/weapon(W)
#        ^^^^^^ entity.name.function.mora
#               ^ variable.parameter.mora
    form/keyword(W, @WeapMaterialIron)
#                   ^^^^^^^^^^^^^^^^^ constant.other.atom.mora
```

- [ ] **Step 2: Run the test, confirm it fails**

```bash
cd editors/vscode && npm run test:grammar
```

- [ ] **Step 3: Add identifier patterns to the grammar**

Replace `editors/vscode/syntaxes/mora.tmLanguage.json`:

```json
{
  "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
  "name": "Mora",
  "scopeName": "source.mora",
  "fileTypes": ["mora"],
  "patterns": [
    { "include": "#comments" },
    { "include": "#keywords-toplevel" },
    { "include": "#keywords-body" },
    { "include": "#namespaces-builtin" },
    { "include": "#atoms" },
    { "include": "#variables" },
    { "include": "#relation-call" }
  ],
  "repository": {
    "comments": {
      "match": "#.*$",
      "name": "comment.line.number-sign.mora"
    },
    "keywords-toplevel": {
      "match": "\\b(namespace|requires|mod|use|as|refer|only|import_spid|import_kid)\\b",
      "name": "keyword.control.mora"
    },
    "keywords-body": {
      "match": "\\b(not|or|in|maintain|on|set|add|sub|remove)\\b",
      "name": "keyword.operator.mora"
    },
    "namespaces-builtin": {
      "match": "\\b(form|ref|player|world|event)(?=/)",
      "name": "entity.name.namespace.builtin.mora"
    },
    "atoms": {
      "match": "@[A-Za-z_][A-Za-z0-9_]*",
      "name": "constant.other.atom.mora"
    },
    "variables": {
      "match": "\\b[A-Z][A-Za-z0-9_]*\\b",
      "name": "variable.parameter.mora"
    },
    "relation-call": {
      "match": "\\b([a-z_][a-z0-9_]*)(?=\\s*\\()",
      "name": "entity.name.function.mora"
    }
  }
}
```

Pattern notes:
- `atoms` matches `@` followed by an identifier; the scope spans both.
- `variables` matches an identifier starting with an uppercase letter — but only when it's not part of a keyword (keyword patterns run first because they're listed first in the top-level `patterns` array).
- `relation-call` matches a lowercase identifier *only* when followed by `(`. This catches both `bandit(...)` (head) and `form/weapon(...)` (body call). Bare `weapon` not in callable position is left unscoped.

- [ ] **Step 4: Run the test, confirm it passes**

```bash
cd editors/vscode && npm run test:grammar
```

- [ ] **Step 5: Commit**

```bash
git add editors/vscode/tests/grammar/identifiers.test.mora \
        editors/vscode/syntaxes/mora.tmLanguage.json
git commit -m "feat(vscode): highlight atoms, variables, relation calls"
```

---

### Task 8: Number + string literal highlighting (TDD)

**Files:**
- Create: `editors/vscode/tests/grammar/literals.test.mora`
- Modify: `editors/vscode/syntaxes/mora.tmLanguage.json`

- [ ] **Step 1: Write the failing test**

`editors/vscode/tests/grammar/literals.test.mora`:

```mora
# SYNTAX TEST "source.mora"

requires mod("Skyrim.esm")
#            ^^^^^^^^^^^^ string.quoted.double.mora

elite(NPC):
    form/level(NPC, Level)
    Level >= 20
#            ^^ constant.numeric.mora
    form/speed(W, 0.5)
#                 ^^^ constant.numeric.mora
```

- [ ] **Step 2: Run the test, confirm it fails**

```bash
cd editors/vscode && npm run test:grammar
```

- [ ] **Step 3: Add literal patterns**

Edit `editors/vscode/syntaxes/mora.tmLanguage.json` — add to the top-level `patterns` array (before `#variables` so number literals don't get caught by an identifier pattern):

```json
    { "include": "#numbers" },
    { "include": "#strings" },
```

The full top-level `patterns` array becomes:

```json
"patterns": [
  { "include": "#comments" },
  { "include": "#keywords-toplevel" },
  { "include": "#keywords-body" },
  { "include": "#namespaces-builtin" },
  { "include": "#strings" },
  { "include": "#numbers" },
  { "include": "#atoms" },
  { "include": "#variables" },
  { "include": "#relation-call" }
]
```

And add to `repository`:

```json
    "numbers": {
      "match": "\\b[0-9]+(?:\\.[0-9]+)?\\b",
      "name": "constant.numeric.mora"
    },
    "strings": {
      "name": "string.quoted.double.mora",
      "begin": "\"",
      "end": "\"",
      "patterns": [
        {
          "match": "\\\\.",
          "name": "constant.character.escape.mora"
        }
      ]
    }
```

- [ ] **Step 4: Run the test, confirm it passes**

```bash
cd editors/vscode && npm run test:grammar
```

- [ ] **Step 5: Commit**

```bash
git add editors/vscode/tests/grammar/literals.test.mora \
        editors/vscode/syntaxes/mora.tmLanguage.json
git commit -m "feat(vscode): highlight number and string literals"
```

---

### Task 9: Operator + punctuation highlighting (TDD)

Operator list from `src/lexer/lexer.cpp:392–433`:
- Two-char: `=>`, `==`, `!=`, `<=`, `>=`
- One-char comparison: `<`, `>`
- Arithmetic: `+`, `-`, `*`, `/`, `|`
- Punctuation: `(`, `)`, `[`, `]`, `,`, `.`

**Files:**
- Create: `editors/vscode/tests/grammar/operators.test.mora`
- Modify: `editors/vscode/syntaxes/mora.tmLanguage.json`

- [ ] **Step 1: Write the failing test**

`editors/vscode/tests/grammar/operators.test.mora`:

```mora
# SYNTAX TEST "source.mora"

elite(NPC):
    form/level(NPC, Level)
    Level >= 20
#         ^^ keyword.operator.comparison.mora
    Level == 50
#         ^^ keyword.operator.comparison.mora
    Level != 0
#         ^^ keyword.operator.comparison.mora
    => add form/keyword(NPC, @Foo)
#   ^^ keyword.operator.arrow.mora
```

- [ ] **Step 2: Run the test, confirm it fails**

```bash
cd editors/vscode && npm run test:grammar
```

- [ ] **Step 3: Add operator patterns**

Edit `editors/vscode/syntaxes/mora.tmLanguage.json` — add to the top-level `patterns` array, *before* `#numbers` (so `=>` is matched as an arrow even though `>` is a comparison op):

Full top-level `patterns`:

```json
"patterns": [
  { "include": "#comments" },
  { "include": "#keywords-toplevel" },
  { "include": "#keywords-body" },
  { "include": "#namespaces-builtin" },
  { "include": "#strings" },
  { "include": "#operators-arrow" },
  { "include": "#operators-comparison" },
  { "include": "#numbers" },
  { "include": "#atoms" },
  { "include": "#variables" },
  { "include": "#relation-call" }
]
```

Add to `repository`:

```json
    "operators-arrow": {
      "match": "=>",
      "name": "keyword.operator.arrow.mora"
    },
    "operators-comparison": {
      "match": "==|!=|<=|>=|<|>",
      "name": "keyword.operator.comparison.mora"
    }
```

Punctuation (`(`, `)`, `,`, `.`, `+`, `-`, `*`) is intentionally left unscoped — themes paint them with the default text colour, which is what users expect for delimiters.

- [ ] **Step 4: Run the test, confirm it passes**

```bash
cd editors/vscode && npm run test:grammar
```

- [ ] **Step 5: Commit**

```bash
git add editors/vscode/tests/grammar/operators.test.mora \
        editors/vscode/syntaxes/mora.tmLanguage.json
git commit -m "feat(vscode): highlight comparison and arrow operators"
```

---

### Task 10: Integration test against a real `.mora` file

Validates the full grammar against an existing example, catching interactions between rules.

**Files:**
- Create: `editors/vscode/tests/grammar/example.test.mora`

- [ ] **Step 1: Copy the test fixture**

```bash
cp test_data/example.mora editors/vscode/tests/grammar/example.test.mora
```

- [ ] **Step 2: Add the syntax-test header and a few key assertions**

Prepend to `editors/vscode/tests/grammar/example.test.mora`:

```mora
# SYNTAX TEST "source.mora"
```

Then add assertion lines after a handful of representative source lines (insert after each):

After `namespace test.example`:
```
# <-------- keyword.control.mora
```

After `requires mod("Skyrim.esm")`:
```
# <------- keyword.control.mora
#        ^^^ keyword.control.mora
#            ^^^^^^^^^^^^ string.quoted.double.mora
```

After `bandit(NPC):`:
```
# <----- entity.name.function.mora
#      ^^^ variable.parameter.mora
```

After `    form/keyword(W, @WeapMaterialSilver)`:
```
#       ^^^^ entity.name.namespace.builtin.mora
#            ^^^^^^^ entity.name.function.mora
#                       ^^^^^^^^^^^^^^^^^^^ constant.other.atom.mora
```

After `    Level >= 20`:
```
#         ^^ keyword.operator.comparison.mora
#            ^^ constant.numeric.mora
```

(Each assertion line uses `# ^^^` aligned to the character offsets in the source line *above* it. Count carefully — alignment errors cause test failures that look identical to grammar bugs.)

- [ ] **Step 3: Run the test, confirm it passes**

```bash
cd editors/vscode && npm run test:grammar
```

If any assertions fail, fix the assertion column offsets first (they are nearly always the cause). Only suspect the grammar after re-counting.

- [ ] **Step 4: Commit**

```bash
git add editors/vscode/tests/grammar/example.test.mora
git commit -m "test(vscode): integration test against test_data/example.mora"
```

---

### Task 11: Real language configuration (comments + brackets + autoclose + onEnter)

**Files:**
- Modify: `editors/vscode/language-configuration.json`

- [ ] **Step 1: Replace the placeholder with the full config**

`editors/vscode/language-configuration.json`:

```json
{
  "comments": {
    "lineComment": "#"
  },
  "brackets": [
    ["(", ")"],
    ["[", "]"]
  ],
  "autoClosingPairs": [
    { "open": "(", "close": ")" },
    { "open": "[", "close": "]" },
    { "open": "\"", "close": "\"", "notIn": ["string", "comment"] }
  ],
  "surroundingPairs": [
    ["(", ")"],
    ["[", "]"],
    ["\"", "\""]
  ],
  "wordPattern": "(-?\\d*\\.\\d\\w*)|([A-Za-z_][A-Za-z0-9_]*)",
  "indentationRules": {
    "increaseIndentPattern": "^\\s*[A-Za-z_][A-Za-z0-9_]*\\s*\\([^)]*\\)\\s*:\\s*(#.*)?$",
    "decreaseIndentPattern": "^\\s*$"
  },
  "onEnterRules": [
    {
      "beforeText": "^\\s*#",
      "action": { "indent": "none", "appendText": "# " }
    }
  ]
}
```

Notes:
- `wordPattern` mirrors VS Code's default but ensures `form/weapon` selects `weapon` (not the slash) on double-click. The slash is excluded from the word.
- `increaseIndentPattern` matches a rule head: `name(args):` (optionally followed by a trailing comment).
- `decreaseIndentPattern` is a blank line — the next line returns to the enclosing indent level. Mora is indentation-sensitive but bodies use uniform indentation, so a blank line between rules is the natural dedent signal.
- `onEnterRules`: when the cursor is on a line starting with `#`, pressing Enter inserts `# ` on the next line, continuing the comment block.

- [ ] **Step 2: Manual verification (optional but recommended once)**

If you have VS Code installed locally:

```bash
cd editors/vscode && npm run compile && npx vsce package --no-dependencies -o /tmp/mora-test.vsix
code --install-extension /tmp/mora-test.vsix --force
code test_data/example.mora
```

Then in VS Code:
- Highlight: confirm colours match the test assertions visually.
- `Ctrl+/` toggles a `#` comment on the current line.
- Type a `(` — confirm `)` is inserted.
- After `bandit(NPC):` press Enter — confirm the next line is indented.
- Inside a `# foo` comment, press Enter — confirm `# ` is inserted.

This step is **manual** — no automated test runs an Enter-key behaviour against `language-configuration.json`. Skip cleanly with `code --uninstall-extension halgari.mora` afterwards.

- [ ] **Step 3: Commit**

```bash
git add editors/vscode/language-configuration.json
git commit -m "feat(vscode): language configuration — brackets, autoclose, onEnter"
```

---

### Task 12: Verify production build locally

This task does no editing — it confirms the extension packages cleanly and the `.vsix` contents are correct before wiring CI.

- [ ] **Step 1: Clean build**

```bash
cd editors/vscode && rm -rf out *.vsix && npm run compile
```

Expected: `out/extension.js` is created. No TypeScript errors.

- [ ] **Step 2: Package**

```bash
cd editors/vscode && npm run package
```

Expected: produces `editors/vscode/mora-vscode-0.1.0.vsix`. `vsce` may warn about a missing icon — ignore for v0.1.0.

- [ ] **Step 3: Inspect contents**

```bash
unzip -l editors/vscode/mora-vscode-0.1.0.vsix
```

Expected entries (subset):
```
extension/package.json
extension/language-configuration.json
extension/syntaxes/mora.tmLanguage.json
extension/out/extension.js
extension/LICENSE
extension/README.md
extension/CHANGELOG.md
```

NOT expected (would indicate `.vscodeignore` is wrong):
- `extension/src/**`
- `extension/tests/**`
- `extension/node_modules/**`

- [ ] **Step 4: Run the full test suite**

```bash
cd editors/vscode && npm test
```

Expected: TypeScript compiles, all grammar tests pass.

- [ ] **Step 5: Clean up the local artifact**

```bash
rm editors/vscode/mora-vscode-0.1.0.vsix
```

(The artifact will be rebuilt by CI; tracking it locally is noise.)

No commit for this task — it's pure verification.

---

### Task 13: Add `extension` job to CI

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Add the new job**

Insert the following block in `.github/workflows/ci.yml`, between the `linux:` job and the `windows:` job (inside the `jobs:` mapping):

```yaml
  extension:
    name: VS Code extension (.vsix)
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - uses: actions/setup-node@v4
        with:
          node-version: '20'
          cache: 'npm'
          cache-dependency-path: editors/vscode/package-lock.json

      - name: Install dependencies
        working-directory: editors/vscode
        run: npm ci

      - name: Compile TypeScript
        working-directory: editors/vscode
        run: npm run compile

      - name: Run grammar tests
        working-directory: editors/vscode
        run: npm run test:grammar

      - name: Package .vsix
        working-directory: editors/vscode
        run: npm run package

      - name: Upload .vsix artifact
        uses: actions/upload-artifact@v4
        with:
          name: mora-vscode-vsix
          path: editors/vscode/mora-vscode-*.vsix
          if-no-files-found: error
```

- [ ] **Step 2: Commit and push**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: add extension job — build and test the VS Code .vsix"
git push
```

- [ ] **Step 3: Watch the run**

```bash
sleep 8 && gh run list --limit 1 -w CI
```

Wait for completion (`gh run watch <id>`). Expected: `extension` job green in ~30 s.

- [ ] **Step 4: Download the artifact and confirm contents**

```bash
gh run download <run-id> -n mora-vscode-vsix -D /tmp/mora-vsix
unzip -l /tmp/mora-vsix/mora-vscode-0.1.0.vsix | head -20
```

Expected: same file list as task 12 step 3.

- [ ] **Step 5: Clean up local artifacts**

```bash
rm -rf /tmp/mora-vsix
```

No additional commit — task 2's commit already has CI.

---

### Task 14: Version-sync check

The extension's `package.json` `version` must match `xmake.lua`'s `set_version(...)`. Add a CI guard so they can't drift.

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Add a sync-check step to the `extension` job**

Insert immediately *after* the "Install dependencies" step and *before* "Compile TypeScript":

```yaml
      - name: Verify version matches xmake.lua
        run: |
          xmake_version=$(grep -oP 'set_version\("\K[^"]+' xmake.lua)
          pkg_version=$(node -p "require('./editors/vscode/package.json').version")
          echo "xmake.lua version:    $xmake_version"
          echo "package.json version: $pkg_version"
          if [ "$xmake_version" != "$pkg_version" ]; then
            echo "::error::xmake.lua and editors/vscode/package.json version mismatch"
            exit 1
          fi
```

- [ ] **Step 2: Test the failure path locally**

Edit `editors/vscode/package.json`, change `"version": "0.1.0"` to `"version": "0.1.99"`. Run the equivalent locally:

```bash
xmake_version=$(grep -oP 'set_version\("\K[^"]+' xmake.lua)
pkg_version=$(node -p "require('./editors/vscode/package.json').version")
if [ "$xmake_version" != "$pkg_version" ]; then
  echo "MISMATCH: $xmake_version vs $pkg_version"
fi
```

Expected: prints `MISMATCH: 0.1.0 vs 0.1.99`.

Revert the version to `0.1.0`.

- [ ] **Step 3: Commit and push**

```bash
git add .github/workflows/ci.yml
git commit -m "ci(extension): fail if package.json version drifts from xmake.lua"
git push
```

- [ ] **Step 4: Watch the run**

Confirm the `extension` job is still green and the new step runs.

---

### Task 15: Bundle the `.vsix` into the Windows release archive

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Make the Windows job depend on the extension job**

In `.github/workflows/ci.yml`, find:

```yaml
  windows:
    name: Windows (MSVC — SKSE runtime DLL)
    runs-on: windows-latest
```

Add `needs: extension` immediately below `runs-on:`:

```yaml
  windows:
    name: Windows (MSVC — SKSE runtime DLL)
    runs-on: windows-latest
    needs: extension
```

- [ ] **Step 2: Add a step to download the `.vsix` artifact**

Insert this step in the Windows job *after* the existing "Build mora.exe (CLI compiler)" step and *before* "Package release archive":

```yaml
      - name: Download VS Code extension .vsix
        uses: actions/download-artifact@v4
        with:
          name: mora-vscode-vsix
          path: dist/vsix
```

- [ ] **Step 3: Update the packaging step to copy the `.vsix`**

Find the "Package release archive" step's `Copy-Item` block. After the existing `Copy-Item` for `MoraRuntime.dll`, add:

```powershell
          $vsix = Get-ChildItem -Path "$proj/dist/vsix" -Filter mora-vscode-*.vsix | Select-Object -First 1
          if (-not $vsix) { throw "VS Code .vsix not found in dist/vsix" }
          Copy-Item $vsix.FullName "$stage/tools/Mora/$($vsix.Name)"
          Write-Host "Bundled $($vsix.Name) into release archive"
```

So the Copy block becomes (full context):

```powershell
          Copy-Item "$env:MORA_EXE"                     "$stage/tools/Mora/mora.exe"
          Copy-Item "$proj/build/runtime/MoraRuntime.dll" "$stage/Data/SKSE/Plugins/MoraRuntime.dll"
          $vsix = Get-ChildItem -Path "$proj/dist/vsix" -Filter mora-vscode-*.vsix | Select-Object -First 1
          if (-not $vsix) { throw "VS Code .vsix not found in dist/vsix" }
          Copy-Item $vsix.FullName "$stage/tools/Mora/$($vsix.Name)"
          Write-Host "Bundled $($vsix.Name) into release archive"
```

- [ ] **Step 4: Commit and push**

```bash
git add .github/workflows/ci.yml
git commit -m "ci(windows): bundle mora-vscode .vsix into release archive"
git push
```

- [ ] **Step 5: Watch the run**

Wait for both `extension` and `windows` to finish.

```bash
sleep 8 && gh run list --limit 1 -w CI
```

Expected: both green. Windows job finishes a touch later because of the `needs:` ordering.

- [ ] **Step 6: Inspect the release archive**

```bash
gh run download <run-id> -n Mora-windows -D /tmp/mora-rel
unzip -l /tmp/mora-rel/Mora-*-windows.zip
```

Expected entries:
```
Data/SKSE/Plugins/MoraRuntime.dll
tools/Mora/mora.exe
tools/Mora/mora-vscode-0.1.0.vsix
```

Clean up: `rm -rf /tmp/mora-rel`.

---

### Task 16: Root README mention

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add a one-paragraph blurb under "Building" or as a new section**

Add this section to `README.md` immediately before the existing "## CI" section:

```markdown
## Editor support

Syntax highlighting and a forthcoming language server live in
[`editors/vscode/`](editors/vscode/). The packaged `.vsix` ships in the
Windows release archive at `tools/Mora/mora-vscode-<version>.vsix`;
sideload it into VS Code with **Extensions → ⋯ → Install from VSIX**.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: link to editors/vscode/ from the root README"
```

- [ ] **Step 3: Push**

```bash
git push
```

(No CI watch needed — README-only change.)

---

## Done — Phase 1 deliverable

After all 16 tasks land:

- A `.mora` file opened in VS Code with the extension installed shows correct, themed colours.
- Comment toggling, bracket matching, auto-closing pairs, and indent-on-Enter all work.
- `npm test` in `editors/vscode/` runs the grammar test suite green.
- The Windows release archive contains `tools/Mora/mora-vscode-<version>.vsix` alongside `mora.exe` and `MoraRuntime.dll`.
- CI guards prevent `package.json` and `xmake.lua` versions from drifting.

**No language server is running yet.** That's phase 2. Hover, goto-def, find-references, document symbols, semantic tokens, and red-squiggly diagnostics are all stubbed-out features that will arrive when phase 2's `mora lsp` subcommand ships.
