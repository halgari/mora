# Mora INI Importers Implementation Plan (Plan 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Import existing SPID and KID INI files and translate them into Mora rules, displayed via `mora import` in clean `.mora` syntax. This is the adoption bridge — mod authors see their existing INI configs rendered as Mora rules, proving the language can express everything they already do.

**Architecture:** Each importer is a standalone parser that reads an INI file and produces a vector of `Rule` AST nodes. The `mora import` command renders these as pretty-printed `.mora` syntax. Phase 1 covers SPID (Keyword, Spell, Perk, Item distributions with string/form filters) and KID (keyword distribution to items). SkyPatcher is deferred to a later plan due to its much larger surface area.

**Tech Stack:** C++20, xmake, Google Test.

---

## Scope

### SPID (_DISTR.ini) — Phase 1 subset:
- Distribution types: `Keyword`, `Spell`, `Perk`, `Item`
- FormID references: `EditorID` and `0xHEX~Plugin.esp`
- String filters: plain, `+` (AND), `-` (NOT), `*` (wildcard)
- Form filters: faction, race, class by EditorID or FormID
- Level filters: `min/max` ranges
- Traits: `M`/`F`/`U` and negations
- Chance field

### KID (_KID.ini):
- Keyword distribution to: Weapon, Armor, Ammo, Potion, Book
- String/form filters with `+`/`-` prefix logic
- Armor traits: `E`/`-E`, `HEAVY`/`LIGHT`/`CLOTHING`

### Not in scope (Phase 1):
- SkyPatcher (complex colon-separated format, 25+ record types)
- SPID: Package, Outfit, DeathItem, SleepOutfit, Skin, Shout, LevSpell
- SPID: Skill-based level filters
- KID: Magic Effect, Location, Scroll, and other esoteric item types

---

## File Structure

```
include/mora/import/
├── spid_parser.h         # SPID _DISTR.ini line parser
├── kid_parser.h          # KID _KID.ini line parser
├── ini_common.h          # shared: FormID ref parsing, filter parsing
└── mora_printer.h        # pretty-print Rule AST as .mora syntax
src/import/
├── spid_parser.cpp
├── kid_parser.cpp
├── ini_common.cpp
└── mora_printer.cpp
tests/
├── spid_parser_test.cpp
├── kid_parser_test.cpp
└── mora_printer_test.cpp
```

---

### Task 1: Shared INI Parsing Utilities

**Files:**
- Create: `include/mora/import/ini_common.h`
- Create: `src/import/ini_common.cpp`

Shared utilities for parsing FormID references, filter strings, and trait flags used by both SPID and KID.

- [ ] **Step 1: Define shared types**

```cpp
// include/mora/import/ini_common.h
#pragma once

#include "mora/core/string_pool.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace mora {

// A reference to a form: either EditorID or Plugin|FormID
struct FormRef {
    std::string editor_id;         // e.g., "ActorTypeNPC"
    std::string plugin;            // e.g., "Skyrim.esm" (empty if editor_id only)
    uint32_t form_id = 0;          // hex value (0 if editor_id only)

    bool is_editor_id() const { return plugin.empty() && form_id == 0; }
    std::string to_mora_symbol() const;  // ":ActorTypeNPC" or ":Skyrim.esm|0x00012345"
};

// A filter entry with optional prefix
struct FilterEntry {
    enum class Mode { Include, And, Exclude, Wildcard };
    Mode mode = Mode::Include;
    FormRef ref;                   // form reference or string pattern
    std::string pattern;           // for wildcard/string matching
};

struct LevelRange {
    int min = 0;
    int max = 0;
    bool has_min = false;
    bool has_max = false;
};

struct TraitFilter {
    std::optional<bool> is_male;      // true=M, false=F, nullopt=any
    bool is_unique = false;
    bool not_unique = false;
    bool is_child = false;
    bool not_child = false;
};

// Parse a SPID/KID FormID reference: "0x12345~Plugin.esp" or "EditorID"
FormRef parse_form_ref(std::string_view text);

// Parse a pipe-separated filter field: "ActorTypeNPC,+Vampire,-Nazeem"
std::vector<FilterEntry> parse_filter_entries(std::string_view text);

// Parse a level range: "25/50" or "25/" or "/50"
LevelRange parse_level_range(std::string_view text);

// Parse traits: "M/U" or "F/-S"
TraitFilter parse_traits(std::string_view text);

// Split a line by pipe character, trimming whitespace
std::vector<std::string> split_pipes(std::string_view line);

// Trim whitespace from both ends
std::string_view trim(std::string_view s);

} // namespace mora
```

- [ ] **Step 2: Implement parsing functions**

Key implementation:
- `parse_form_ref`: check for `0x` prefix → hex + `~Plugin.esp`. Otherwise it's an EditorID.
- `parse_filter_entries`: split by `,`, check first char for `+`/`-`/`*` prefix, parse the rest as FormRef or pattern.
- `split_pipes`: split by `|`, trim each part. Handle `NONE` and empty strings.
- `to_mora_symbol`: render as `:EditorID` or `:Plugin.esp|0xHEX`

- [ ] **Step 3: Build and commit**

```bash
xmake build mora_lib
git add -A && git commit -m "feat: shared INI parsing utilities for SPID/KID importers"
```

---

### Task 2: Mora Pretty Printer

**Files:**
- Create: `include/mora/import/mora_printer.h`
- Create: `src/import/mora_printer.cpp`
- Create: `tests/mora_printer_test.cpp`

Render Rule AST nodes as clean `.mora` syntax text. This is what `mora import` displays.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/mora_printer_test.cpp
#include <gtest/gtest.h>
#include "mora/import/mora_printer.h"
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"

class MoraPrinterTest : public ::testing::Test {
protected:
    mora::StringPool pool;
};

TEST_F(MoraPrinterTest, SimpleRule) {
    // Build: bandit(NPC): npc(NPC) has_faction(NPC, :BanditFaction) => add_keyword(NPC, :Tag)
    mora::Rule rule;
    rule.name = pool.intern("bandit_tag");

    mora::Expr head_arg;
    head_arg.data = mora::VariableExpr{pool.intern("NPC"), {}, {}};
    rule.head_args.push_back(std::move(head_arg));

    // Body: npc(NPC)
    mora::FactPattern fp1;
    fp1.name = pool.intern("npc");
    mora::Expr arg1;
    arg1.data = mora::VariableExpr{pool.intern("NPC"), {}, {}};
    fp1.args.push_back(std::move(arg1));
    mora::Clause c1;
    c1.data = std::move(fp1);
    rule.body.push_back(std::move(c1));

    // Effect: add_keyword(NPC, :Tag)
    mora::Effect eff;
    eff.action = pool.intern("add_keyword");
    mora::Expr eff_arg1;
    eff_arg1.data = mora::VariableExpr{pool.intern("NPC"), {}, {}};
    eff.args.push_back(std::move(eff_arg1));
    mora::Expr eff_arg2;
    eff_arg2.data = mora::SymbolExpr{pool.intern("Tag"), {}, {}};
    eff.args.push_back(std::move(eff_arg2));
    rule.effects.push_back(std::move(eff));

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rule);

    EXPECT_NE(output.find("bandit_tag(NPC):"), std::string::npos);
    EXPECT_NE(output.find("npc(NPC)"), std::string::npos);
    EXPECT_NE(output.find("=> add_keyword(NPC, :Tag)"), std::string::npos);
}

TEST_F(MoraPrinterTest, NegatedFact) {
    mora::Rule rule;
    rule.name = pool.intern("test");
    mora::Expr head;
    head.data = mora::VariableExpr{pool.intern("X"), {}, {}};
    rule.head_args.push_back(std::move(head));

    mora::FactPattern fp;
    fp.name = pool.intern("has_keyword");
    fp.negated = true;
    mora::Expr a1, a2;
    a1.data = mora::VariableExpr{pool.intern("X"), {}, {}};
    a2.data = mora::SymbolExpr{pool.intern("Foo"), {}, {}};
    fp.args.push_back(std::move(a1));
    fp.args.push_back(std::move(a2));
    mora::Clause c;
    c.data = std::move(fp);
    rule.body.push_back(std::move(c));

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rule);
    EXPECT_NE(output.find("not has_keyword(X, :Foo)"), std::string::npos);
}

TEST_F(MoraPrinterTest, PrintComment) {
    mora::MoraPrinter printer(pool);
    std::string output = printer.print_comment("Imported from MyMod_DISTR.ini");
    EXPECT_EQ(output, "# Imported from MyMod_DISTR.ini\n");
}
```

- [ ] **Step 2: Implement MoraPrinter**

```cpp
// include/mora/import/mora_printer.h
#pragma once

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include <string>

namespace mora {

class MoraPrinter {
public:
    explicit MoraPrinter(StringPool& pool) : pool_(pool) {}

    std::string print_rule(const Rule& rule) const;
    std::string print_module(const Module& mod) const;
    std::string print_comment(const std::string& text) const;

private:
    std::string print_expr(const Expr& expr) const;
    std::string print_clause(const Clause& clause) const;
    std::string print_effect(const Effect& effect) const;

    StringPool& pool_;
};

} // namespace mora
```

Implementation: Walk the AST and emit properly indented `.mora` syntax.
- Rule: `name(args):\n    clause1\n    clause2\n    => effect\n`
- FactPattern: `fact_name(arg1, arg2)` or `not fact_name(arg1, arg2)`
- VariableExpr: print the name directly
- SymbolExpr: print with `:` prefix
- IntLiteral/FloatLiteral/StringLiteral: print value
- Effect: `=> action(arg1, arg2)`

- [ ] **Step 3: Build, test, commit**

```bash
xmake build mora_printer_test && xmake run mora_printer_test
git add -A && git commit -m "feat: pretty printer renders Rule AST as .mora syntax"
```

---

### Task 3: SPID Parser

**Files:**
- Create: `include/mora/import/spid_parser.h`
- Create: `src/import/spid_parser.cpp`
- Create: `tests/spid_parser_test.cpp`

Parse SPID `_DISTR.ini` lines and produce Rule AST nodes.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/spid_parser_test.cpp
#include <gtest/gtest.h>
#include "mora/import/spid_parser.h"
#include "mora/import/mora_printer.h"
#include "mora/core/string_pool.h"

class SpidParserTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;
};

TEST_F(SpidParserTest, SimpleKeywordDistribution) {
    // Keyword = ActorTypePoor|Brenuin
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line("Keyword = ActorTypePoor|Brenuin", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rules[0]);
    // Should produce a rule that adds keyword ActorTypePoor to NPCs matching "Brenuin"
    EXPECT_NE(output.find("add_keyword"), std::string::npos);
    EXPECT_NE(output.find(":ActorTypePoor"), std::string::npos);
}

TEST_F(SpidParserTest, SpellWithFactionFilter) {
    // Spell = 0x12FCD~Skyrim.esm|NONE|CrimeFactionWhiterun
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Spell = 0x12FCD~Skyrim.esm|NONE|CrimeFactionWhiterun", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("add_spell"), std::string::npos);
    EXPECT_NE(output.find("has_faction"), std::string::npos);
}

TEST_F(SpidParserTest, ItemWithCount) {
    // Item = 0xF~Skyrim.esm|ActorTypeNPC|NONE|NONE|NONE|3000
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Item = 0xF~Skyrim.esm|ActorTypeNPC|NONE|NONE|NONE|3000", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("add_item"), std::string::npos);
}

TEST_F(SpidParserTest, KeywordWithExclusion) {
    // Keyword = ActorTypeWarrior|ActorTypeNPC,-Nazeem
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Keyword = ActorTypeWarrior|ActorTypeNPC,-Nazeem", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("not"), std::string::npos);
}

TEST_F(SpidParserTest, WithLevelFilter) {
    // Spell = FireBolt|NONE|NONE|25/50|F
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Spell = FireBolt|NONE|NONE|25/50|F", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("base_level"), std::string::npos);
}

TEST_F(SpidParserTest, ParseFile) {
    // Write a temp INI file
    std::string content =
        "; Comment line\n"
        "Keyword = ActorTypePoor|Brenuin\n"
        "\n"
        "Spell = FireBolt|ActorTypeNPC\n";

    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_string(content, "test_DISTR.ini");
    ASSERT_EQ(rules.size(), 2u);
}

TEST_F(SpidParserTest, SkipsComments) {
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line("; This is a comment", "test.ini", 1);
    EXPECT_TRUE(rules.empty());
}

TEST_F(SpidParserTest, PerkDistribution) {
    // Perk = LightFoot|NONE|NONE|NONE|NONE|NONE|50
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Perk = LightFoot|NONE|NONE|NONE|NONE|NONE|50", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("add_perk"), std::string::npos);
}
```

- [ ] **Step 2: Implement SpidParser**

```cpp
// include/mora/import/spid_parser.h
#pragma once

#include "mora/ast/ast.h"
#include "mora/import/ini_common.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <string>
#include <vector>

namespace mora {

class SpidParser {
public:
    SpidParser(StringPool& pool, DiagBag& diags);

    // Parse a single SPID line, return 0 or 1 rules
    std::vector<Rule> parse_line(const std::string& line,
                                  const std::string& filename, int line_num);

    // Parse an entire INI string (multiple lines)
    std::vector<Rule> parse_string(const std::string& content,
                                    const std::string& filename);

    // Parse a file from disk
    std::vector<Rule> parse_file(const std::string& path);

private:
    // Build a Rule from parsed SPID fields
    Rule build_rule(const std::string& dist_type,
                    const FormRef& target,
                    const std::vector<FilterEntry>& string_filters,
                    const std::vector<FilterEntry>& form_filters,
                    const LevelRange& levels,
                    const TraitFilter& traits,
                    const std::string& filename, int line_num);

    // Map SPID type to Mora action name
    std::string type_to_action(const std::string& dist_type) const;

    // Generate a unique rule name from context
    std::string make_rule_name(const std::string& dist_type,
                                const FormRef& target, int line_num);

    StringPool& pool_;
    DiagBag& diags_;
    int rule_counter_ = 0;
};

} // namespace mora
```

Implementation:
1. `parse_line()`: Skip comments (`;`) and blank lines. Match `Type = rest` at start. Split `rest` by `|` into fields. Parse each field: target FormRef, string filters, form filters, level range, traits, count, chance.
2. `build_rule()`: Construct a `Rule` AST node:
   - Rule name: generated from type + target + line number (e.g., `spid_keyword_ActorTypePoor_1`)
   - Head arg: `NPC` variable
   - Body: `npc(NPC)` always. Then add clauses from filters:
     - String filter include → `has_keyword(NPC, :Keyword)` or `editor_id(NPC, "Pattern")`
     - String filter AND → additional `has_keyword` clause
     - String filter NOT → `not has_keyword(NPC, :Keyword)`
     - Form filter → `has_faction(NPC, :Faction)` or `race_of(NPC, :Race)`
     - Level range → `base_level(NPC, Level)` + guard `Level >= min` / `Level <= max`
     - Trait M/F → guard or filter on gender
   - Effect: `=> add_keyword(NPC, :Target)` or `=> add_spell(NPC, :Target)` etc.
3. `type_to_action()`: Keyword→add_keyword, Spell→add_spell, Perk→add_perk, Item→add_item

- [ ] **Step 3: Build, test, commit**

```bash
xmake build spid_parser_test && xmake run spid_parser_test
git add -A && git commit -m "feat: SPID INI importer translates _DISTR.ini to Mora rules"
```

---

### Task 4: KID Parser

**Files:**
- Create: `include/mora/import/kid_parser.h`
- Create: `src/import/kid_parser.cpp`
- Create: `tests/kid_parser_test.cpp`

Parse KID `_KID.ini` lines. Similar structure to SPID but targets items instead of NPCs.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/kid_parser_test.cpp
#include <gtest/gtest.h>
#include "mora/import/kid_parser.h"
#include "mora/import/mora_printer.h"
#include "mora/core/string_pool.h"

class KidParserTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;
};

TEST_F(KidParserTest, SimpleKeywordToWeapon) {
    // Keyword = MyWeaponTag|Weapon|IronSword
    mora::KidParser parser(pool, diags);
    auto rules = parser.parse_line("Keyword = MyWeaponTag|Weapon|IronSword", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("weapon"), std::string::npos);
    EXPECT_NE(output.find("add_keyword"), std::string::npos);
    EXPECT_NE(output.find(":MyWeaponTag"), std::string::npos);
}

TEST_F(KidParserTest, KeywordToArmorByKeyword) {
    // Keyword = HeavyGauntlets|Armor|ArmorHeavy+ArmorGauntlets
    mora::KidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Keyword = HeavyGauntlets|Armor|ArmorHeavy+ArmorGauntlets", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("armor"), std::string::npos);
    EXPECT_NE(output.find("has_keyword"), std::string::npos);
}

TEST_F(KidParserTest, KeywordWithExclusion) {
    // Keyword = NonEnchanted|Armor|ArmorHeavy,-ArmorEnchanted
    mora::KidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Keyword = NonEnchanted|Armor|ArmorHeavy,-ArmorEnchanted", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("not has_keyword"), std::string::npos);
}

TEST_F(KidParserTest, KeywordFromPlugin) {
    // Keyword = 0x12345~MyMod.esp|Weapon|MyMod.esp
    mora::KidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Keyword = 0x12345~MyMod.esp|Weapon|MyMod.esp", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
}

TEST_F(KidParserTest, ParseFile) {
    std::string content =
        "; KID config\n"
        "Keyword = TagA|Weapon|IronSword\n"
        "Keyword = TagB|Armor|ArmorHeavy\n";

    mora::KidParser parser(pool, diags);
    auto rules = parser.parse_string(content, "test_KID.ini");
    ASSERT_EQ(rules.size(), 2u);
}
```

- [ ] **Step 2: Implement KidParser**

```cpp
// include/mora/import/kid_parser.h
#pragma once

#include "mora/ast/ast.h"
#include "mora/import/ini_common.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <string>
#include <vector>

namespace mora {

class KidParser {
public:
    KidParser(StringPool& pool, DiagBag& diags);

    std::vector<Rule> parse_line(const std::string& line,
                                  const std::string& filename, int line_num);
    std::vector<Rule> parse_string(const std::string& content,
                                    const std::string& filename);
    std::vector<Rule> parse_file(const std::string& path);

private:
    Rule build_rule(const FormRef& keyword,
                    const std::string& item_type,
                    const std::vector<FilterEntry>& filters,
                    const std::string& filename, int line_num);

    // Map KID item type string to Mora relation name
    std::string item_type_to_relation(const std::string& type) const;
    std::string make_rule_name(const FormRef& keyword, int line_num);

    StringPool& pool_;
    DiagBag& diags_;
    int rule_counter_ = 0;
};

} // namespace mora
```

Implementation:
1. Parse `Keyword = <keyword>|<item_type>|<filters>|<traits>|<chance>` format
2. `item_type_to_relation()`: "Weapon"→"weapon", "Armor"→"armor", "Ammo"→"ammo", "Potion"→"potion", "Book"→"book"
3. Build rule: body starts with `weapon(Item)` (or armor, etc.), filters add `has_keyword` clauses, effect is `add_keyword(Item, :Keyword)`

- [ ] **Step 3: Build, test, commit**

```bash
xmake build kid_parser_test && xmake run kid_parser_test
git add -A && git commit -m "feat: KID INI importer translates _KID.ini to Mora rules"
```

---

### Task 5: `mora import` CLI Command

**Files:**
- Modify: `src/main.cpp`

Implement the import command that finds INI files, parses them, and displays the generated Mora rules.

- [ ] **Step 1: Implement cmd_import**

The import command:
1. Scan target directory for `*_DISTR.ini` and `*_KID.ini` files
2. Parse each with the appropriate parser
3. Pretty-print the generated rules using MoraPrinter
4. Show summary: N SPID files → M rules, N KID files → M rules

Output format:
```
  mora v0.1.0

  Scanning for INI files...

  ── MyMod_DISTR.ini (3 rules) ──

  # Imported from MyMod_DISTR.ini:2
  spid_keyword_ActorTypePoor_2(NPC):
      npc(NPC)
      editor_id(NPC, "Brenuin")
      => add_keyword(NPC, :ActorTypePoor)

  # Imported from MyMod_DISTR.ini:4
  spid_spell_FireBolt_4(NPC):
      npc(NPC)
      has_keyword(NPC, :ActorTypeNPC)
      => add_spell(NPC, :FireBolt)

  ...

  ── MyArmor_KID.ini (2 rules) ──

  # Imported from MyArmor_KID.ini:1
  kid_keyword_HeavyGauntlets_1(Item):
      armor(Item)
      has_keyword(Item, :ArmorHeavy)
      has_keyword(Item, :ArmorGauntlets)
      => add_keyword(Item, :HeavyGauntlets)

  ── Summary ──
  2 INI files → 5 Mora rules
  SPID: 1 file, 3 rules
  KID:  1 file, 2 rules
```

With `--write` flag: write the rules to `.mora` files in the target directory instead of printing.

- [ ] **Step 2: Add test data INI files**

Create `test_data/TestMod_DISTR.ini`:
```ini
; Test SPID distribution
Keyword = ActorTypePoor|Brenuin
Spell = FireBolt|ActorTypeNPC
Perk = LightFoot|NONE|NONE|NONE|M
```

Create `test_data/TestArmor_KID.ini`:
```ini
; Test KID distribution
Keyword = HeavyGauntlets|Armor|ArmorHeavy+ArmorGauntlets
Keyword = LightBoots|Armor|ArmorLight+ArmorBoots,-ArmorEnchanted
```

- [ ] **Step 3: Test**

```bash
xmake build mora
xmake run mora import test_data/
```

Expected: Pretty-printed Mora rules for all INI files found.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "feat: mora import command displays INI files as Mora rules"
```

---

### Task 6: Update xmake.lua + Integration

**Files:**
- Modify: `xmake.lua`

Add `src/import/*.cpp` to the mora_lib target.

- [ ] **Step 1: Update xmake.lua**

Add `"src/import/*.cpp"` to mora_lib's add_files.

Create `mkdir -p src/import` and placeholder if needed.

- [ ] **Step 2: Full build and test**

```bash
xmake build && xmake test
```

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "chore: add import/ to xmake build"
```

---

## Summary

This plan delivers:
- **SPID importer**: Parses `_DISTR.ini` files (Keyword, Spell, Perk, Item distributions with filters)
- **KID importer**: Parses `_KID.ini` files (keyword distribution to items)
- **Pretty printer**: Renders Rule AST as clean `.mora` syntax
- **`mora import` command**: Finds INI files, translates them, displays the result
- **Shared INI utilities**: FormID parsing, filter parsing, trait parsing

Users can see exactly what their existing INI configs become in Mora syntax — proving the language is a superset of what they already do.
