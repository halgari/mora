# Language Guide

This guide walks you through writing `.mora` files, from the basic structure of a file all the way to composing reusable rules and applying effects. Each concept builds on the ones before it.

---

## File Structure

Every `.mora` file starts with two things: a **namespace** declaration and one or more **requires** directives.

```mora
namespace my_mod.weapons

requires mod("Skyrim.esm")
requires mod("Dawnguard.esm")
```

The **namespace** gives your rules a unique identity so they don't collide with rules from other mods. Use a dotted name based on your mod and the file's purpose, for example `my_mod.weapons`, `my_mod.npcs`, or `my_mod.leveled_lists`.

The **requires** directives tell Mora which plugins your rules depend on. Mora will refuse to compile if any required plugin is missing from the load order, which prevents silent failures.

> **Tip:** Use one `.mora` file per topic (weapons, NPCs, leveled lists) and give each its own descriptive namespace. Small, focused files are easier to maintain than one large file.

---

## Rules

A **rule** is the core building block of Mora. A rule says: *"find every form matching these conditions, then apply these effects."*

A rule has two parts:

- The **head**: the rule's name and its primary variable, written on its own line ending with a colon.
- The **body**: indented lines beneath the head, each describing a condition the form must satisfy.

```mora
my_rule(NPC):
    npc(NPC)
    has_keyword(NPC, :Bandit)
```

This rule is named `my_rule`. It matches every NPC that has the `Bandit` keyword.

The indentation is significant, just like Python. Each line of the body must be indented by at least one level relative to the head.

> **Common mistake:** Forgetting the colon at the end of the rule head. Write `my_rule(NPC):` and don't leave off the `:`.

---

## Variables

Names written in **UpperCase** are variables. A variable starts unbound and takes on a value the first time it appears in a clause. Every subsequent use of that same variable in the rule refers to the same value.

```mora
heavy_weapons(Weapon):
    weapon(Weapon)
    weight(Weapon, W)
    W >= 15.0
```

Here, `Weapon` first binds to each weapon record in turn. `W` then binds to that weapon's weight. The comparison `W >= 15.0` keeps only the weapons where the weight is 15 or more.

Variables are local to the rule they appear in. `Weapon` in one rule has nothing to do with `Weapon` in another.

> **Tip:** Use descriptive variable names like `NPC`, `Weapon`, `Level`, and `Faction` to make rules read like plain English.

---

## Form References

A **form reference** written as `:EditorID` refers to a specific record from your plugins by its Editor ID. Mora resolves it to the actual FormID at compile time, so you never deal with raw hex IDs in your rules.

```mora
has_keyword(Weapon, :WeapMaterialIron)
```

This matches weapons that have the `WeapMaterialIron` keyword, the same keyword the Creation Kit uses to tag iron weapons.

Form references can refer to keywords, factions, spells, perks, NPCs, weapons, or any other record type. If you mistype an Editor ID, Mora will report an error at compile time and suggest similar names.

> **Common mistake:** Omitting the leading colon. `:WeapMaterialIron` is a form reference; `WeapMaterialIron` without the colon is an unbound variable (a bug).

---

## Clauses: Querying Facts

The lines inside a rule body are called **clauses**. Each clause is a query against the plugin data. A form must satisfy every clause in the body to be matched by the rule.

Mora provides built-in relations for the most common queries:

| Clause | What it matches |
|---|---|
| `npc(NPC)` | All NPC base records |
| `weapon(W)` | All weapon records |
| `armor(A)` | All armor records |
| `has_keyword(Form, Kw)` | Form has the given keyword |
| `has_faction(NPC, Faction)` | NPC belongs to the given faction |
| `base_level(NPC, Lvl)` | NPC's base level (binds `Lvl` to a number) |
| `weight(Form, W)` | Form's weight (binds `W` to a number) |
| `gold_value(Form, Val)` | Form's gold value (binds `Val` to a number) |

Multiple clauses in the same rule body are implicitly combined with AND. A form must satisfy all of them.

```mora
armed_bandits(NPC):
    npc(NPC)
    has_faction(NPC, :BanditFaction)
    base_level(NPC, Level)
    Level >= 10
```

This matches NPCs that are in the Bandit faction **and** have a base level of at least 10.

---

## Negation

Use `not` before a clause to require that the condition is **not** true.

```mora
non_daedric_weapons(Weapon):
    weapon(Weapon)
    not has_keyword(Weapon, :DaedricArtifact)
```

This matches every weapon that does **not** have the `DaedricArtifact` keyword.

Negation always applies to the single clause that immediately follows it. The form must still match every other clause in the rule.

> **Tip:** Negation is useful for exclusions: "all weapons except Daedric ones", "all NPCs except essential characters", etc.

> **Common mistake:** Putting `not` before a clause that binds a variable for the first time. If `W` has never been bound before `not weight(Weapon, W)`, the rule won't work as expected. Use negation on clauses whose variables are already bound by earlier clauses.

---

## Comparison Operators

Use comparison operators to filter based on numeric values. Mora supports `>=`, `<=`, `>`, `<`, `==`, and `!=`.

The variable on the left must already be bound by an earlier clause. Comparisons don't bind variables; they just filter.

```mora
elite_bandits(NPC):
    npc(NPC)
    has_faction(NPC, :BanditFaction)
    base_level(NPC, Level)
    Level >= 20
```

Breaking this down step by step:

1. `npc(NPC)`: iterate over every NPC.
2. `has_faction(NPC, :BanditFaction)`: keep only those in the Bandit faction.
3. `base_level(NPC, Level)`: bind `Level` to each NPC's base level.
4. `Level >= 20`: keep only NPCs where that level is 20 or more.

---

## Disjunction (or)

By default, every clause in a rule body must match (AND). The `or:` keyword lets you express alternatives. The form must match one of the alternatives, but not necessarily all of them.

```mora
valuable_items(Item):
    or:
        weapon(Item)
        armor(Item)
    gold_value(Item, Val)
    Val >= 1000
```

This matches any item that is either a weapon or armor, and has a gold value of at least 1000.

The `or:` block can contain any number of alternative clauses, one per line, each indented beneath it. Clauses outside the `or:` block apply to all alternatives as usual.

> **Tip:** Think of `or:` like a fork. The rule tries each branch and accepts a form if any branch succeeds. Everything before and after the `or:` block still applies unconditionally.

---

## Effects

So far, rules have only described **what to find**. To actually change something in the game data, add effects using the `=>` separator.

```mora
iron_boost(Weapon):
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialIron)
    => set_damage(Weapon, 99)
    => add_keyword(Weapon, :WeapMaterialDaedric)
```

Each `=>` line is an **effect**, an action applied to every form that matched the rule's conditions. A rule can have multiple effects; they all apply to the same matched form.

Available effects:

| Effect | What it does |
|---|---|
| `set_damage(W, Value)` | Set a weapon's base damage |
| `set_name(Form, "Name")` | Set a form's display name |
| `set_gold_value(Form, Value)` | Set a form's gold value |
| `set_weight(Form, Value)` | Set a form's weight |
| `set_armor_rating(A, Value)` | Set an armor's rating |
| `add_keyword(Form, :Kw)` | Add a keyword to a form |
| `remove_keyword(Form, :Kw)` | Remove a keyword from a form |
| `add_spell(NPC, :Spell)` | Give a spell to an NPC |
| `add_perk(NPC, :Perk)` | Give a perk to an NPC |
| `add_item(NPC, :Item)` | Add an item to an NPC's inventory |
| `distribute_items(...)` | Distribute items to matched forms |

> **Tip:** Effects are applied at compile time for rules targeting base records. Mora evaluates the rule against your plugin data, computes the full list of patches, and bakes them into the output DLL. No scanning happens at game startup.

---

## Derived Rules

A rule without any `=>` effects doesn't patch anything. Instead, it creates a **derived rule** (sometimes called a predicate). Derived rules let you name a concept once and reuse it in other rules.

```mora
bandit(NPC):
    npc(NPC)
    has_faction(NPC, :BanditFaction)
```

Now `bandit(NPC)` can be used as a clause in any other rule, just like a built-in relation:

```mora
arm_bandits(NPC):
    bandit(NPC)
    => add_spell(NPC, :FlameCloak)
```

This is one of Mora's most powerful features. Instead of repeating the faction check everywhere, define it once as `bandit` and reference it by name. If your definition of "bandit" ever needs to change, you update it in one place.

```mora
# You can stack derived rules
high_level_bandit(NPC):
    bandit(NPC)
    base_level(NPC, Level)
    Level >= 30

# And use them together
arm_elite_bandits(NPC):
    high_level_bandit(NPC)
    => add_perk(NPC, :HeavyArmorPerks50)
    => add_spell(NPC, :FlameCloak)
```

> **Tip:** Derived rules are the Mora equivalent of functions or macros in other languages. Use them to avoid repeating yourself and to give meaningful names to conditions you use frequently.

---

## String Literals

Strings are written in double quotes and used with effects that set text values, like `set_name`.

```mora
everyone_is_nazeem(NPC):
    npc(NPC)
    => set_name(NPC, "Nazeem")
```

Strings can contain spaces and most printable characters. They're UTF-8, so special characters are fine.

---

## Comments

Comments start with `#` and run to the end of the line. They can appear on their own line or at the end of any line.

```mora
# This is a comment explaining the whole rule
iron_weapons(Weapon):  # inline comment on the rule head
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialIron)  # only iron
    => set_damage(Weapon, 99)
```

Use comments to explain the intent of your rules, especially when the conditions are non-obvious. Future you (and other mod authors building on your work) will thank present you.

---

## Putting It All Together

Here is a complete `.mora` file that demonstrates most of the concepts above:

```mora
namespace my_mod.combat

requires mod("Skyrim.esm")

# Derived rule: anything we consider a "bandit"
bandit(NPC):
    npc(NPC)
    has_faction(NPC, :BanditFaction)

# Derived rule: high-level bandits specifically
elite_bandit(NPC):
    bandit(NPC)
    base_level(NPC, Level)
    Level >= 20

# Give all bandits a flame cloak spell
arm_all_bandits(NPC):
    bandit(NPC)
    => add_spell(NPC, :FlameCloak)

# Give elite bandits additional perks and a silver sword
arm_elite_bandits(NPC):
    elite_bandit(NPC)
    => add_perk(NPC, :HeavyArmorPerks50)
    => add_item(NPC, :SilverSword)

# Buff iron weapons but exclude greatswords
iron_boost(Weapon):
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialIron)
    not has_keyword(Weapon, :WeapTypeGreatsword)
    => set_damage(Weapon, 14)

# Rename any NPC in the Companions faction
companions_title(NPC):
    npc(NPC)
    has_faction(NPC, :CompanionsFaction)
    => set_name(NPC, "Companion")
```

This file is self-contained and ready to compile. It introduces no external dependencies beyond `Skyrim.esm`, reuses `bandit` and `elite_bandit` across multiple rules, and covers querying, negation, comparison, and effects.
