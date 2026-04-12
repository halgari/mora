# Mora Documentation Site

## Goal

A polished, themed documentation site for mod authors, covering the Mora language, CLI tooling, design philosophy, and migration from existing patchers. Hosted on GitHub Pages via MkDocs Material.

## Audience

Skyrim mod authors who want to write `.mora` files. Not compiler contributors — no internals, no architecture docs. Focus on usage, syntax, and understanding why Mora is fast.

## Stack

- **MkDocs** with **Material for MkDocs** theme
- Custom CSS overrides for "Dark Scholar" visual identity
- Custom Pygments lexer for `.mora` syntax highlighting
- GitHub Pages deployment via `mkdocs gh-deploy`

## Visual Identity: Dark Scholar

- **Base palette:** Material `slate` (dark mode)
- **Background:** GitHub-dark inspired (`#0d1117` primary, `#161b22` surface)
- **Accent:** Teal (`#58a691`) for links, nav highlights, active states
- **Heading font:** MedievalSharp (Google Fonts) — fantasy flavor without sacrificing readability
- **Body font:** System sans-serif stack (Inter / -apple-system)
- **Code blocks:** Material's default dark code theme
- **Logo text:** "MORA" in MedievalSharp with teal color and letter-spacing

## Syntax Highlighting

Custom Pygments lexer (`MoraLexer`) registered as the `mora` language. Follows Python-style color conventions (familiar to mod authors).

Token mapping:
- **Keywords:** `namespace`, `requires`, `mod`, `not`, `or`, `in` → Keyword
- **Built-in relations:** `npc`, `weapon`, `armor`, `has_keyword`, `has_faction`, `base_level` → Name.Builtin
- **Effects:** `set_damage`, `set_name`, `add_keyword`, `remove_keyword`, `set_weight`, etc. → Name.Function
- **Form references:** `:EditorID` → Name.Constant
- **Effect separator:** `=>` → Operator
- **Comments:** `# ...` → Comment
- **Strings:** `"..."` → String
- **Numbers:** integer and float literals → Number
- **Comparison operators:** `>=`, `<=`, `>`, `<`, `==`, `!=` → Operator

## Sections

### 1. Home / Landing Page (`index.md`)
Brief intro: what Mora is, the pitch (6782 patches in 1.63ms), quick links to Getting Started and How Mora Works.

### 2. Getting Started (`getting-started.md`)
- Install the `mora` CLI (prebuilt binary or build from source)
- Write your first `.mora` file
- Compile to a DLL: `mora compile`
- Deploy to Skyrim (SKSE plugin directory)
- Verify it works

### 3. How Mora Works (`how-mora-works.md`)
The philosophy and design section. Covers:

**The problem:** Skyrim modding relies on dozens of independent runtime patchers (SPID, KID, SkyPatcher, custom SKSE plugins, Synthesis). Each re-implements form lookup, rule evaluation, and patching. With large mod lists, startup time balloons to 5-10+ minutes.

**Comparison with existing approaches:**
- **SPID/KID** — evaluate rules at runtime against every form. O(forms × rules) at every game launch. Fast for small setups, scales badly.
- **SkyPatcher** — per-form INI evaluation, similar runtime scaling. Hundreds of INI files = hundreds of passes over form data.
- **Synthesis** — ahead-of-time patcher that generates ESP files. Avoids runtime cost but produces massive patch plugins, breaks on load order changes, requires manual re-patching after any mod change.
- **Custom SKSE plugins** — maximum flexibility but requires C++ development per patcher. No standardization. Each plugin re-implements form lookup, memory layout knowledge, and patching logic.
- **Mora** — compile-time Datalog evaluation. Rules are evaluated once on the developer's machine against the actual plugin data. Results are frozen into direct memory writes in a native DLL. Runtime cost is O(patches), not O(forms × rules). A single 16KB DLL replaces dozens of runtime patchers. 6782 NPC name patches apply in 1.63ms.

**How it works:**
- Datalog as the rule language (declarative, composable, analyzable)
- Phase classification: static rules (known at compile time) vs dynamic rules (need runtime state)
- Static rules are fully evaluated against ESP data → produce a patch set
- Patch set is compiled to LLVM IR → optimized → native x86-64 DLL
- At runtime: DLL loads via SKSE, applies pre-computed patches at DataLoaded
- Dynamic rules (future): bytecode VM for rules that depend on game state

### 4. Language Guide (`language-guide.md`)
Tutorial-style walkthrough:
- File structure: `namespace`, `requires`
- Rules: head, clauses, effects
- Clauses: relation queries, negation (`not`), disjunction (`or`), comparison operators
- Effects: the `=>` separator, built-in effect functions
- Form references: `:EditorID` syntax
- String literals, numbers
- Comments

### 5. Language Reference (`language-reference.md`)
Comprehensive reference:
- All built-in relations with signatures and descriptions
- All effect functions with signatures and descriptions
- Type system: FormID, KeywordID, SpellID, PerkID, String, Int, Float
- Operator reference

### 6. CLI Reference (`cli-reference.md`)
Every command:
- `mora compile` — flags, output, Address Library, data directory
- `mora check` — type checking and linting
- `mora inspect` — display patch set
- `mora info` — project overview
- `mora import` — scan for SPID/KID/SkyPatcher INIs

### 7. Importing from Other Patchers (`importing.md`)
- How `mora import` works
- SPID `_DISTR.ini` → Mora rules
- KID `_KID.ini` → Mora rules
- SkyPatcher INI → Mora rules
- Migration workflow: import, review, edit, compile

### 8. Examples (`examples.md`)
Annotated real-world examples:
- Tag all bandits with a keyword
- Set damage for all iron weapons
- Rename NPCs by faction
- Complex: multi-clause rules with negation and level checks
- Importing and customizing a SkyPatcher config

## File Structure

```
docs/
  mkdocs.yml
  mora_lexer/
    __init__.py         # Custom Pygments lexer
    setup.py            # Installable package
  src/
    index.md
    getting-started.md
    how-mora-works.md
    language-guide.md
    language-reference.md
    cli-reference.md
    importing.md
    examples.md
    stylesheets/
      extra.css         # Dark Scholar theme overrides
    js/
      extra.js          # MedievalSharp font loading
```

## Build & Deploy

```bash
cd docs
pip install mkdocs-material
pip install -e mora_lexer/
mkdocs serve          # local preview
mkdocs gh-deploy      # push to GitHub Pages
```

## GitHub Pages

Configure via `mkdocs gh-deploy` which pushes to the `gh-pages` branch. Repository settings point GitHub Pages at that branch.
