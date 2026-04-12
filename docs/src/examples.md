# Examples

Annotated real-world `.mora` files. Each example includes the full source, a line-by-line explanation, and the expected compiler output. Read them top to bottom or jump to whichever pattern you need.

---

## 1. Tag Bandits with a Keyword

This example introduces **derived rules** — rules without effects that name a concept for reuse.

```mora
namespace my_mod.bandits

requires mod("Skyrim.esm")

bandit(NPC):
    npc(NPC)
    has_faction(NPC, :BanditFaction)

tag_bandits(NPC):
    bandit(NPC)
    => add_keyword(NPC, :ActorTypeNPC)
```

### What it does

`bandit(NPC)` defines a reusable predicate: any NPC that belongs to the `BanditFaction`. This rule has no `=>` effects, so it patches nothing on its own — it just names a concept.

`tag_bandits(NPC)` uses `bandit(NPC)` as a clause, exactly like a built-in relation, and applies a keyword to every NPC it matches.

### Line by line

`namespace my_mod.bandits`
: Declares the namespace. Every `.mora` file must start with one.

`requires mod("Skyrim.esm")`
: Guards compilation — Mora will refuse to compile if `Skyrim.esm` is not in the Data directory. Both `:BanditFaction` and `:ActorTypeNPC` live in that plugin.

`bandit(NPC):`
: Head of the derived rule. `NPC` is a logic variable that will be bound to each matching form.

`npc(NPC)`
: Constrains `NPC` to base NPC records only.

`has_faction(NPC, :BanditFaction)`
: Further constrains `NPC` to those that belong to the `BanditFaction`. `:BanditFaction` is a form reference — the leading colon means "look this EditorID up in the loaded plugins."

`tag_bandits(NPC):`
: Head of the patching rule.

`bandit(NPC)`
: Calls the derived rule defined above. Any NPC that satisfies all of `bandit`'s clauses satisfies this clause too.

`=> add_keyword(NPC, :ActorTypeNPC)`
: Effect. Adds the `ActorTypeNPC` keyword to every matched NPC. The `=>` separator marks the transition from conditions to actions.

### Expected output

```
✓ Parsing 1 files
✓ Resolving 2 rules
✓ Type checking 2 rules
✓ 2 static, 0 dynamic
✓ 15 plugins, 3 relations → 59522 facts
✓ 84 patches generated
✓ 428461 entries (Address Library)
✓ MoraRuntime.dll (14.2 KB)
✓ Compiled 2 rules in 412ms
```

!!! tip
    Derived rules are free at runtime — they are fully inlined during compilation. Define as many as you like. Small, named predicates make rules read like plain English and are trivial to update if the definition ever changes.

---

## 2. Iron Weapons Damage Boost

A minimal patch: find every iron weapon and set its base damage.

```mora
namespace my_mod.balance

requires mod("Skyrim.esm")

iron_weapons(Weapon):
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialIron)
    => set_damage(Weapon, 99)
```

### What it does

Iterates over every weapon record in the loaded plugins, keeps those tagged with the `WeapMaterialIron` material keyword, and sets each one's base damage to `99`.

### Line by line

`weapon(Weapon)`
: Constrains `Weapon` to weapon records. Without this clause, `has_keyword` would search across all form types.

`has_keyword(Weapon, :WeapMaterialIron)`
: Keeps only weapons that carry the iron material keyword. This is the same keyword the Creation Kit uses to control tempering recipes and display names.

`=> set_damage(Weapon, 99)`
: Sets base damage to `99` on every matched weapon. The value is a plain integer.

### Expected output

```
✓ Parsing 1 files
✓ Resolving 1 rules
✓ Type checking 1 rules
✓ 1 static, 0 dynamic
✓ 15 plugins, 2 relations → 59522 facts
✓ 200 patches generated
✓ 428461 entries (Address Library)
✓ MoraRuntime.dll (16.5 KB)
✓ Compiled 1 rules in 389ms
```

---

## 3. Rename All NPCs

String patching using a string literal effect.

```mora
namespace test.nazeem

requires mod("Skyrim.esm")

everyone_is_nazeem(NPC):
    npc(NPC)
    => set_name(NPC, "Nazeem")
```

### What it does

Finds every NPC base record and sets its display name to the string `"Nazeem"`. The string is stored as a `BSFixedString` in the output DLL and written to the record's name field at game load.

### Line by line

`npc(NPC)`
: Matches all NPC base records — no keyword or faction filter, so every NPC in the load order qualifies.

`=> set_name(NPC, "Nazeem")`
: Sets the display name. The value is a quoted string literal. Mora stores it as a `BSFixedString` in the compiled DLL; no heap allocation occurs at runtime.

### Expected output

```
✓ Parsing 1 files
✓ Resolving 1 rules
✓ Type checking 1 rules
✓ 1 static, 0 dynamic
✓ 15 plugins, 1 relation → 59522 facts
✓ 4218 patches generated
✓ 428461 entries (Address Library)
✓ MoraRuntime.dll (22.1 KB)
✓ Compiled 1 rules in 401ms
```

!!! tip
    The DLL grows slightly with each unique string stored. For mods that set hundreds of distinct names, this is still negligible — the compiler deduplicates identical strings automatically.

---

## 4. Silver Weapons (Not Greatswords)

Using `not` to exclude a subset of matching forms.

```mora
namespace my_mod.silver

requires mod("Skyrim.esm")

silver_weapons(Weapon):
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialSilver)
    not has_keyword(Weapon, :WeapTypeGreatsword)
    => add_keyword(Weapon, :WeapMaterialDaedric)
```

### What it does

Finds all silver weapons that are **not** greatswords, then adds the `WeapMaterialDaedric` keyword to each one. The greatsword exclusion lets you treat one-handed and two-handed silver weapons differently without duplicating the rest of the rule.

### Line by line

`has_keyword(Weapon, :WeapMaterialSilver)`
: Keeps weapons with the silver material keyword.

`not has_keyword(Weapon, :WeapTypeGreatsword)`
: Excludes weapons that also have the greatsword type keyword. The `not` applies to the single clause that immediately follows it. `Weapon` is already bound by the earlier clauses, so negation is safe here.

`=> add_keyword(Weapon, :WeapMaterialDaedric)`
: Adds the Daedric material keyword to each matched weapon — without removing the silver keyword. A weapon can carry multiple material keywords simultaneously.

### Expected output

```
✓ Parsing 1 files
✓ Resolving 1 rules
✓ Type checking 1 rules
✓ 1 static, 0 dynamic
✓ 15 plugins, 3 relations → 59522 facts
✓ 17 patches generated
✓ 428461 entries (Address Library)
✓ MoraRuntime.dll (13.8 KB)
✓ Compiled 1 rules in 391ms
```

!!! warning
    Only negate clauses whose variables are already bound by earlier clauses. Writing `not has_keyword(Weapon, W)` when `W` has not yet been bound produces undefined behavior. Bind first, then negate.

---

## 5. Elite Bandits (Level 20+)

Combining a derived rule with a numeric comparison.

```mora
namespace my_mod.elite

requires mod("Skyrim.esm")

bandit(NPC):
    npc(NPC)
    has_faction(NPC, :BanditFaction)

elite_bandits(NPC):
    bandit(NPC)
    base_level(NPC, Level)
    Level >= 20
    => add_keyword(NPC, :ActorTypeNPC)
    => add_spell(NPC, :FlameCloak)
```

### What it does

Defines bandits as in Example 1, then further filters to those with a base level of 20 or higher. Every elite bandit receives a keyword and a flame cloak spell — two effects applied to the same matched form.

### Line by line

`base_level(NPC, Level)`
: Binds the variable `Level` to each NPC's base level. The clause does not filter by itself — it only makes the value available to the next clause.

`Level >= 20`
: Filters: only NPCs where the bound level is 20 or more pass. Comparison operators (`>=`, `<=`, `>`, `<`, `==`, `!=`) never bind variables; they only accept or reject already-bound values.

`=> add_keyword(NPC, :ActorTypeNPC)`
: First effect. Adds the keyword to each matched NPC.

`=> add_spell(NPC, :FlameCloak)`
: Second effect. Gives each matched NPC the Flame Cloak spell. Multiple `=>` lines all apply to the same `NPC` — they are not alternatives.

### Expected output

```
✓ Parsing 1 files
✓ Resolving 2 rules
✓ Type checking 2 rules
✓ 2 static, 0 dynamic
✓ 15 plugins, 4 relations → 59522 facts
✓ 62 patches generated
✓ 428461 entries (Address Library)
✓ MoraRuntime.dll (14.6 KB)
✓ Compiled 2 rules in 418ms
```

!!! tip
    Patch count reflects the number of individual field writes, not the number of matched forms. Each matched NPC receives two patches (keyword + spell), so 31 elite bandits produce 62 patches.

---

## 6. Complete Mod — Weapon Rebalance

A realistic multi-rule file combining derived rules, multiple effects, and negation. This is the kind of `.mora` file you might ship as a balance mod.

```mora
namespace rebalance.weapons

requires mod("Skyrim.esm")

# Derived: all non-unique weapons
common_weapon(W):
    weapon(W)
    not has_keyword(W, :WeapTypeStaff)
    not has_keyword(W, :DaedricArtifact)

# Iron weapons: boost damage, reduce value
iron_rebalance(W):
    common_weapon(W)
    has_keyword(W, :WeapMaterialIron)
    => set_damage(W, 12)
    => set_gold_value(W, 15)

# Steel weapons
steel_rebalance(W):
    common_weapon(W)
    has_keyword(W, :WeapMaterialSteel)
    => set_damage(W, 18)
    => set_gold_value(W, 45)
```

### What it does

Defines a base predicate `common_weapon` that excludes staves and Daedric artifacts — weapons that should not be touched by a material balance pass. The two patching rules then each select a material tier and apply both a damage value and a gold value.

### Line by line

`common_weapon(W):`
: Derived rule. The short variable name `W` is fine for a predicate this focused. It will be expanded inline when referenced by the patching rules.

`not has_keyword(W, :WeapTypeStaff)`
: Excludes staves. Staves use a completely different damage model and should not receive weapon damage patches.

`not has_keyword(W, :DaedricArtifact)`
: Excludes unique Daedric artifacts. Daedric artifacts are handcrafted and should not be flattened by a balance pass.

`iron_rebalance(W):`
: Patching rule for iron-tier weapons. Calling `common_weapon(W)` here expands to all three clauses from the derived rule — the exclusions are inherited automatically.

`=> set_damage(W, 12)` and `=> set_gold_value(W, 15)`
: Two effects applied in order to the same matched weapon. Both patch the same form; neither depends on the other.

`steel_rebalance(W):`
: Identical structure to `iron_rebalance` but targets a different material tier and sets higher values. Adding a new tier — say, Orcish — means writing one more rule of the same shape.

### Expected output

```
✓ Parsing 1 files
✓ Resolving 3 rules
✓ Type checking 3 rules
✓ 3 static, 0 dynamic
✓ 15 plugins, 4 relations → 59522 facts
✓ 874 patches generated
✓ 428461 entries (Address Library)
✓ MoraRuntime.dll (18.3 KB)
✓ Compiled 3 rules in 443ms
```

### Patterns to note

**Single definition, multiple consumers.** `common_weapon` is written once. Both `iron_rebalance` and `steel_rebalance` benefit from its exclusions without repeating them. If you later decide that named weapons should also be excluded, you add one clause to `common_weapon` and all downstream rules update automatically.

**Rules do not interact.** `iron_rebalance` and `steel_rebalance` run independently. A weapon with both `WeapMaterialIron` and `WeapMaterialSteel` keywords (unusual but possible from a mod) would receive patches from both rules. If you want mutual exclusion, add a `not has_keyword` guard.

**Multiple effects per rule.** Listing several `=>` lines in one rule is more efficient than writing separate single-effect rules for the same condition — Mora evaluates the conditions once and applies all effects to each matched form.
