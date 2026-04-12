# Mora Documentation Site Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and deploy a polished MkDocs Material documentation site for Mora mod authors, with custom Dark Scholar theme and `.mora` syntax highlighting.

**Architecture:** MkDocs Material with custom CSS overrides (Dark Scholar theme: dark background, teal accents, MedievalSharp headings), a custom Pygments lexer for `.mora` files, and 7 content pages covering usage, philosophy, and reference. All files live under `docs/`.

**Tech Stack:** MkDocs, Material for MkDocs, Pygments, Python, GitHub Pages

**Spec:** `docs/superpowers/specs/2026-04-11-documentation-site-design.md`

---

## File Map

**New files:**
- `docs/mkdocs.yml` — MkDocs configuration
- `docs/mora_lexer/__init__.py` — Custom Pygments lexer for `.mora` syntax
- `docs/mora_lexer/setup.py` — Package setup for lexer installation
- `docs/src/index.md` — Landing page
- `docs/src/getting-started.md` — Installation and first steps
- `docs/src/how-mora-works.md` — Philosophy, design, comparisons
- `docs/src/language-guide.md` — Syntax tutorial
- `docs/src/language-reference.md` — All relations, effects, types
- `docs/src/cli-reference.md` — CLI commands
- `docs/src/importing.md` — Migration from SPID/KID/SkyPatcher
- `docs/src/examples.md` — Annotated examples
- `docs/src/stylesheets/extra.css` — Dark Scholar theme overrides
- `docs/src/js/extra.js` — MedievalSharp font loading

---

### Task 1: MkDocs Scaffold + Dark Scholar Theme

**Files:**
- Create: `docs/mkdocs.yml`
- Create: `docs/src/stylesheets/extra.css`
- Create: `docs/src/js/extra.js`
- Create: `docs/src/index.md`

- [ ] **Step 1: Create `docs/mkdocs.yml`**

```yaml
site_name: Mora
site_description: Declarative patching language for Skyrim SE
site_url: https://halgari.github.io/mora/
repo_url: https://github.com/halgari/mora
repo_name: halgari/mora

docs_dir: src

theme:
  name: material
  palette:
    scheme: slate
    primary: custom
    accent: custom
  font: false  # We load our own fonts
  features:
    - navigation.sections
    - navigation.expand
    - navigation.top
    - content.code.copy
    - toc.integrate
  icon:
    repo: fontawesome/brands/github

extra_css:
  - stylesheets/extra.css

extra_javascript:
  - js/extra.js

markdown_extensions:
  - admonition
  - pymdownx.details
  - pymdownx.superfences
  - pymdownx.highlight:
      anchor_linenums: true
  - pymdownx.inlinehilite
  - pymdownx.tabbed:
      alternate_style: true
  - attr_list
  - md_in_html
  - toc:
      permalink: true

nav:
  - Home: index.md
  - Getting Started: getting-started.md
  - How Mora Works: how-mora-works.md
  - Language Guide: language-guide.md
  - Language Reference: language-reference.md
  - CLI Reference: cli-reference.md
  - Importing: importing.md
  - Examples: examples.md
```

- [ ] **Step 2: Create `docs/src/stylesheets/extra.css`**

```css
/* Dark Scholar theme for Mora docs */

@import url('https://fonts.googleapis.com/css2?family=MedievalSharp&display=swap');

:root {
  --md-primary-fg-color: #58a691;
  --md-primary-bg-color: #0d1117;
  --md-accent-fg-color: #58a691;
  --md-default-bg-color: #0d1117;
  --md-default-fg-color: #c9d1d9;
}

/* Darker background to match GitHub dark */
[data-md-color-scheme="slate"] {
  --md-default-bg-color: #0d1117;
  --md-default-fg-color--light: #8b949e;
  --md-code-bg-color: #161b22;
  --md-typeset-a-color: #58a691;
}

/* MedievalSharp for headings */
.md-typeset h1,
.md-typeset h2,
.md-typeset h3,
.md-typeset h4 {
  font-family: 'MedievalSharp', serif;
  color: #c9d1d9;
}

.md-typeset h1 {
  color: #58a691;
  letter-spacing: 1px;
}

/* Site title in header */
.md-header__title {
  font-family: 'MedievalSharp', serif;
  letter-spacing: 2px;
}

/* Navigation */
.md-nav__link {
  font-size: 0.82rem;
}

.md-nav__item--active > .md-nav__link {
  color: #58a691;
}

/* Header bar */
.md-header {
  background-color: #0d1117;
  border-bottom: 1px solid #21262d;
}

/* Sidebar */
.md-sidebar {
  background-color: #0d1117;
}

/* Code blocks */
.md-typeset code {
  background-color: #161b22;
  border: 1px solid #21262d;
  color: #e6edf3;
}

.md-typeset pre > code {
  border: none;
}

/* Admonitions */
.md-typeset .admonition {
  border-left-color: #58a691;
}

/* Footer */
.md-footer {
  background-color: #0d1117;
  border-top: 1px solid #21262d;
}

/* Subtle teal glow on the logo area */
.md-header__button.md-logo {
  color: #58a691;
}

/* Tables */
.md-typeset table:not([class]) {
  background-color: #161b22;
}

.md-typeset table:not([class]) th {
  background-color: #21262d;
  color: #c9d1d9;
}
```

- [ ] **Step 3: Create `docs/src/js/extra.js`**

```javascript
// MedievalSharp is loaded via CSS @import, this file is reserved
// for future interactive features.
```

- [ ] **Step 4: Create `docs/src/index.md`**

```markdown
# Mora

**Declarative patching for Skyrim Special Edition.**

Mora replaces dozens of runtime patchers — SPID, KID, SkyPatcher, and more — with a single compiled DLL. Write rules in a clean, Python-inspired language. Mora evaluates them at compile time and bakes the results into native code.

> **6,782 NPC name patches applied in 1.63 milliseconds.**

## Quick Example

```mora
namespace my_mod.balance

requires mod("Skyrim.esm")

# All iron weapons get 99 damage
iron_weapons(Weapon):
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialIron)
    => set_damage(Weapon, 99)
```

```bash
$ mora compile balance.mora --data-dir ~/.steam/.../Skyrim Special Edition/Data
✓ 200 patches baked into native code
✓ MoraRuntime.dll (16.5 KB)
✓ Compiled 1 rules in 389ms
```

Drop the DLL in `Data/SKSE/Plugins/` and launch the game. That's it.

## Why Mora?

- **Fast** — patches apply in under 2ms, not minutes
- **Declarative** — say what you want, not how to do it
- **Compiled** — rules are evaluated once, results baked into a native DLL
- **Unified** — one language replaces SPID, KID, SkyPatcher, and custom SKSE plugins

## Get Started

- [Getting Started](getting-started.md) — install Mora and write your first rule
- [How Mora Works](how-mora-works.md) — understand the design and why it's fast
- [Language Guide](language-guide.md) — learn the syntax
- [Examples](examples.md) — real-world `.mora` files
```

- [ ] **Step 5: Verify the site builds and serves**

Run:
```bash
cd docs
pip install mkdocs-material
mkdocs serve
```
Expected: Site serves at `http://localhost:8000` with Dark Scholar theme, MedievalSharp headings, and the landing page content.

- [ ] **Step 6: Commit**

```bash
git add docs/mkdocs.yml docs/src/index.md docs/src/stylesheets/extra.css docs/src/js/extra.js
git commit -m "feat: MkDocs scaffold with Dark Scholar theme and landing page"
```

---

### Task 2: Custom Pygments Lexer for `.mora`

**Files:**
- Create: `docs/mora_lexer/__init__.py`
- Create: `docs/mora_lexer/setup.py`

- [ ] **Step 1: Create `docs/mora_lexer/__init__.py`**

```python
"""Pygments lexer for the Mora language."""

from pygments.lexer import RegexLexer, bygroups, words
from pygments.token import (
    Comment, Keyword, Name, Number, Operator, Punctuation, String, Text, Token
)


class MoraLexer(RegexLexer):
    name = 'Mora'
    aliases = ['mora']
    filenames = ['*.mora']

    tokens = {
        'root': [
            # Comments
            (r'#.*$', Comment.Single),

            # String literals
            (r'"[^"]*"', String),

            # Keywords
            (words((
                'namespace', 'requires', 'mod', 'not', 'or', 'in', 'dynamic',
            ), suffix=r'\b'), Keyword),

            # Effect separator
            (r'=>', Operator),

            # Comparison operators
            (r'>=|<=|!=|==|>|<', Operator),

            # Form references (:EditorID)
            (r':[A-Za-z_][A-Za-z0-9_]*', Name.Constant),

            # Built-in relations (query facts)
            (words((
                'npc', 'weapon', 'armor', 'spell', 'perk', 'keyword', 'faction',
                'race', 'leveled_list',
                'has_keyword', 'has_faction', 'has_perk', 'has_spell',
                'base_level', 'level', 'race_of', 'name', 'editor_id',
                'gold_value', 'weight', 'damage', 'armor_rating',
                'template_of', 'leveled_entry', 'outfit_has',
                'current_level', 'current_location', 'current_cell',
                'equipped', 'in_inventory', 'quest_stage', 'is_alive',
            ), prefix=r'\b', suffix=r'(?=\s*\()'), Name.Builtin),

            # Effect functions
            (words((
                'add_keyword', 'remove_keyword', 'add_item', 'add_spell',
                'add_perk', 'set_name', 'set_damage', 'set_armor_rating',
                'set_gold_value', 'set_weight', 'distribute_items',
                'set_game_setting',
            ), prefix=r'\b', suffix=r'(?=\s*\()'), Name.Function),

            # Numbers
            (r'\b\d+\.\d+\b', Number.Float),
            (r'\b\d+\b', Number.Integer),

            # Identifiers (variables, rule names)
            (r'[A-Z][A-Za-z0-9_]*', Name.Variable),
            (r'[a-z_][a-z0-9_]*', Name),

            # Punctuation
            (r'[(),:]', Punctuation),

            # Whitespace
            (r'\s+', Text),
        ],
    }
```

- [ ] **Step 2: Create `docs/mora_lexer/setup.py`**

```python
from setuptools import setup, find_packages

setup(
    name='mora-lexer',
    version='0.1.0',
    packages=find_packages(),
    entry_points={
        'pygments.lexers': [
            'mora = mora_lexer:MoraLexer',
        ],
    },
    install_requires=['pygments'],
)
```

- [ ] **Step 3: Install and verify**

Run:
```bash
cd docs
pip install -e mora_lexer/
python -c "from pygments.lexers import get_lexer_by_name; print(get_lexer_by_name('mora'))"
```
Expected: `<pygments.lexers.MoraLexer>`

- [ ] **Step 4: Verify highlighting in the site**

Update `docs/src/index.md` code blocks to use ` ```mora ` fencing, then `mkdocs serve` and check that syntax is highlighted.

- [ ] **Step 5: Commit**

```bash
git add docs/mora_lexer/
git commit -m "feat: custom Pygments lexer for .mora syntax highlighting"
```

---

### Task 3: How Mora Works

**Files:**
- Create: `docs/src/how-mora-works.md`

- [ ] **Step 1: Write `docs/src/how-mora-works.md`**

This is the philosophy and design page. It should cover:

1. **The Problem** — Skyrim modding relies on dozens of independent runtime patchers. Each re-implements form lookup and rule evaluation. With large mod lists, startup time is 5-10+ minutes.

2. **Existing Approaches** — comparison table and detailed analysis of:
   - **SPID/KID** — runtime rule evaluation, O(forms x rules) per launch
   - **SkyPatcher** — per-form INI evaluation, same scaling problem
   - **Synthesis** — ahead-of-time ESP generation, avoids runtime cost but creates huge patch plugins, breaks on load order changes
   - **Custom SKSE plugins** — maximum flexibility but requires C++ per patcher, no standardization
   - **Mora** — compile-time Datalog, O(patches) runtime, single DLL

3. **How Mora Works** — the pipeline:
   - Declarative rules in Datalog-inspired syntax
   - Compiler loads ESPs, evaluates rules against real plugin data
   - Phase classification: static (compile-time) vs dynamic (runtime)
   - Static rules produce a patch set → LLVM IR → optimized native DLL
   - At runtime: single `apply_all_patches()` call at DataLoaded
   - Real benchmark: 6782 patches in 1.63ms

4. **Why Datalog?** — composable rules, stratified negation, guaranteed termination, partial evaluation

Content should be engaging, not dry — this is the page that sells mod authors on trying Mora. Use concrete numbers and comparisons.

- [ ] **Step 2: Verify renders**

Run: `cd docs && mkdocs serve`
Expected: Page renders with proper headings, code blocks, and structure.

- [ ] **Step 3: Commit**

```bash
git add docs/src/how-mora-works.md
git commit -m "docs: How Mora Works — philosophy, design, and comparisons"
```

---

### Task 4: Getting Started

**Files:**
- Create: `docs/src/getting-started.md`

- [ ] **Step 1: Write `docs/src/getting-started.md`**

Covers:
1. Prerequisites (Skyrim SE, SKSE, Address Library)
2. Installing Mora (build from source with xmake, or prebuilt binary)
3. Writing your first `.mora` file (simple weapon damage rule)
4. Compiling: `mora compile` with `--data-dir`
5. Deploying: copy `MoraRuntime.dll` to `Data/SKSE/Plugins/`
6. Verifying: check `MoraRuntime.log` for patch count and timing
7. Next steps: links to Language Guide and Examples

Include the full workflow with actual terminal output.

- [ ] **Step 2: Commit**

```bash
git add docs/src/getting-started.md
git commit -m "docs: Getting Started guide"
```

---

### Task 5: Language Guide

**Files:**
- Create: `docs/src/language-guide.md`

- [ ] **Step 1: Write `docs/src/language-guide.md`**

Tutorial-style walkthrough covering:

1. **File structure** — `namespace`, `requires mod("...")`
2. **Rules** — `rule_name(Var):` head, indented clauses
3. **Clauses** — querying facts: `npc(NPC)`, `has_keyword(NPC, :Keyword)`
4. **Variables** — uppercase names bind values, shared across clauses
5. **Form references** — `:EditorID` resolves to FormIDs at compile time
6. **Negation** — `not has_keyword(Weapon, :Daedric)`
7. **Comparison** — `Level >= 20`, `Weight < 5.0`
8. **Disjunction** — `or:` blocks
9. **Effects** — `=> effect(Var, value)`, the separator between conditions and actions
10. **Derived rules** — rules without effects that create reusable predicates
11. **String literals** — `"Name"` for `set_name`
12. **Comments** — `# single line`

Each concept with a code example and explanation.

- [ ] **Step 2: Commit**

```bash
git add docs/src/language-guide.md
git commit -m "docs: Language Guide tutorial"
```

---

### Task 6: Language Reference

**Files:**
- Create: `docs/src/language-reference.md`

- [ ] **Step 1: Write `docs/src/language-reference.md`**

Comprehensive reference organized as:

**Types:**
- FormID, KeywordID, SpellID, PerkID, FactionID, RaceID, ArmorID, WeaponID, LocationID, CellID, QuestID
- String, Int, Float, Bool

**Form Relations** (query what exists):
| Relation | Signature | Description |
|----------|-----------|-------------|
| `npc(NPC)` | `(FormID)` | All NPC base records |
| `weapon(W)` | `(WeaponID)` | All weapon records |
| ... | ... | ... |

**Property Relations** (query attributes):
| Relation | Signature | Description |
|----------|-----------|-------------|
| `has_keyword(Form, Kw)` | `(FormID, KeywordID)` | Form has keyword |
| `base_level(NPC, Lvl)` | `(FormID, Int)` | NPC's base level |
| ... | ... | ... |

**Instance Relations** (runtime state — dynamic rules only):
Table of `current_level`, `current_location`, etc.

**Effects:**
| Effect | Signature | Description |
|--------|-----------|-------------|
| `set_damage(W, Val)` | `(FormID, Int)` | Set weapon base damage |
| `set_name(Form, Name)` | `(FormID, String)` | Set display name |
| ... | ... | ... |

Full tables for all relations and effects from `name_resolver.cpp`.

- [ ] **Step 2: Commit**

```bash
git add docs/src/language-reference.md
git commit -m "docs: Language Reference — all relations, effects, types"
```

---

### Task 7: CLI Reference

**Files:**
- Create: `docs/src/cli-reference.md`

- [ ] **Step 1: Write `docs/src/cli-reference.md`**

Document each command:

- `mora compile <path>` — flags: `--output`, `--data-dir`, `-v`
- `mora check <path>` — type check only, no compilation
- `mora inspect <path>` — show patch set, `--conflicts` flag
- `mora info` — project overview, `--data-dir`
- `mora import <path>` — scan for SPID/KID/SkyPatcher INIs

Each with usage, flags, example output.

- [ ] **Step 2: Commit**

```bash
git add docs/src/cli-reference.md
git commit -m "docs: CLI Reference"
```

---

### Task 8: Importing from Other Patchers

**Files:**
- Create: `docs/src/importing.md`

- [ ] **Step 1: Write `docs/src/importing.md`**

Covers:
1. Overview: why migrate, what `mora import` does
2. SPID `_DISTR.ini` → Mora rules (with before/after examples)
3. KID `_KID.ini` → Mora rules (with before/after examples)
4. SkyPatcher INI → Mora rules (with before/after examples)
5. Migration workflow: import, review, customize, compile
6. Limitations: what can't be auto-imported

- [ ] **Step 2: Commit**

```bash
git add docs/src/importing.md
git commit -m "docs: Importing from SPID, KID, and SkyPatcher"
```

---

### Task 9: Examples

**Files:**
- Create: `docs/src/examples.md`

- [ ] **Step 1: Write `docs/src/examples.md`**

Annotated examples:
1. **Tag bandits with a keyword** — derived rules + effects
2. **Iron weapons damage boost** — simple scalar patch
3. **Rename NPCs by faction** — string patching
4. **Silver weapons (not greatswords)** — negation
5. **High-level bandits** — comparison operators
6. **Importing a SkyPatcher config** — migration workflow

Each example has the `.mora` source, a brief explanation, and the expected compile output.

- [ ] **Step 2: Commit**

```bash
git add docs/src/examples.md
git commit -m "docs: Annotated examples"
```

---

### Task 10: GitHub Pages Deployment

- [ ] **Step 1: Build and deploy**

Run:
```bash
cd docs
pip install -e mora_lexer/
mkdocs gh-deploy
```

- [ ] **Step 2: Verify the site is live**

Check `https://halgari.github.io/mora/` (or whatever the repo's GitHub Pages URL is).

- [ ] **Step 3: Commit any deployment config changes**

```bash
git add -A
git commit -m "docs: GitHub Pages deployment"
```
