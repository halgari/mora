# Language Reference

This reference is auto-generated from the Mora form model.
For a guided introduction, see the [Language Guide](language-guide.md).

---

## Types

| Type | Description |
|------|-------------|
| `FormID` | Any game record |
| `WeaponID` | Weapon record (from `weapon(W)`) |
| `ArmorID` | Armor record (from `armor(A)`) |
| `NpcID` | NPC record (from `npc(N)`) |
| `SpellID` | Spell record |
| `PerkID` | Perk record |
| `KeywordID` | Keyword record |
| `FactionID` | Faction record |
| `RaceID` | Race record |
| `String` | Text value |
| `Int` | Integer |
| `Float` | Decimal |

---

## Form Relations

Form relations enumerate what records exist in the load order.

| Relation | Signature |
|----------|-----------|
| `weapon` | `(WeaponID)` |
| `armor` | `(ArmorID)` |
| `npc` | `(NpcID)` |
| `leveled_list` | `(FormID)` |
| `leveled_char` | `(FormID)` |
| `ammo` | `(FormID)` |
| `potion` | `(FormID)` |
| `ingredient` | `(FormID)` |
| `book` | `(FormID)` |
| `scroll` | `(FormID)` |
| `enchantment` | `(FormID)` |
| `magic_effect` | `(FormID)` |
| `misc_item` | `(FormID)` |
| `soul_gem` | `(FormID)` |
| `spell` | `(SpellID)` |
| `perk` | `(PerkID)` |
| `keyword` | `(KeywordID)` |
| `faction` | `(FactionID)` |
| `race` | `(RaceID)` |

---

## Property Relations

Property relations expose attributes of a form for filtering and binding.

| Relation | Signature | Applies to |
|----------|-----------|------------|
| `gold_value` | `(FormID, Int)` | weapon, armor |
| `weight` | `(FormID, Float)` | weapon, armor |
| `damage` | `(FormID, Int)` | weapon |
| `name` | `(FormID, String)` | weapon, armor, npc |
| `speed` | `(FormID, Float)` | weapon |
| `reach` | `(FormID, Float)` | weapon |
| `range_min` | `(FormID, Float)` | weapon |
| `range_max` | `(FormID, Float)` | weapon |
| `stagger` | `(FormID, Float)` | weapon |
| `crit_damage` | `(FormID, Int)` | weapon |
| `armor_rating` | `(FormID, Int)` | armor |
| `base_level` | `(FormID, Int)` | npc |
| `calc_level_min` | `(FormID, Int)` | npc |
| `calc_level_max` | `(FormID, Int)` | npc |
| `speed_mult` | `(FormID, Int)` | npc |
| `race_of` | `(FormID, FormID)` | npc |
| `has_keyword` | `(FormID, KeywordID)` | weapon, armor, npc |
| `has_spell` | `(FormID, SpellID)` | npc |
| `has_perk` | `(FormID, PerkID)` | npc |
| `has_faction` | `(FormID, FactionID)` | npc |

---

## Effects

Effects are the actions Mora applies to matching forms.

### Scalar Setters

| Effect | Signature | Applies to |
|--------|-----------|------------|
| `set_gold_value` | `(FormID, Int)` | weapon, armor |
| `set_weight` | `(FormID, Float)` | weapon, armor |
| `set_damage` | `(WeaponID, Int)` | weapon |
| `set_name` | `(FormID, String)` | weapon, armor, npc |
| `set_speed` | `(WeaponID, Float)` | weapon |
| `set_reach` | `(WeaponID, Float)` | weapon |
| `set_range_min` | `(WeaponID, Float)` | weapon |
| `set_range_max` | `(WeaponID, Float)` | weapon |
| `set_stagger` | `(WeaponID, Float)` | weapon |
| `set_crit_damage` | `(WeaponID, Int)` | weapon |
| `set_enchantment` | `(FormID, FormID)` | weapon, armor |
| `set_armor_rating` | `(ArmorID, Int)` | armor |
| `set_level` | `(NpcID, Int)` | npc |
| `set_calc_level_min` | `(NpcID, Int)` | npc |
| `set_calc_level_max` | `(NpcID, Int)` | npc |
| `set_speed_mult` | `(NpcID, Int)` | npc |
| `set_race` | `(NpcID, FormID)` | npc |
| `set_class` | `(NpcID, FormID)` | npc |
| `set_voice_type` | `(NpcID, FormID)` | npc |
| `set_skin` | `(NpcID, FormID)` | npc |
| `set_outfit` | `(NpcID, FormID)` | npc |
| `set_chance_none` | `(FormID, Int)` | leveled_list, leveled_char |

### Collection Operations

| Effect | Signature | Applies to |
|--------|-----------|------------|
| `add_keyword` | `(FormID, KeywordID)` | weapon, armor, npc |
| `remove_keyword` | `(FormID, KeywordID)` | weapon, armor, npc |
| `add_spell` | `(NpcID, SpellID)` | npc |
| `remove_spell` | `(NpcID, SpellID)` | npc |
| `add_perk` | `(NpcID, PerkID)` | npc |
| `add_faction` | `(NpcID, FactionID)` | npc |
| `remove_faction` | `(NpcID, FactionID)` | npc |
| `add_shout` | `(NpcID, FormID)` | npc |
| `remove_shout` | `(NpcID, FormID)` | npc |

### Boolean Flags

| Effect | Signature | Applies to |
|--------|-----------|------------|
| `set_essential` | `(NpcID, Int)` | npc |
| `set_protected` | `(NpcID, Int)` | npc |
| `set_auto_calc_stats` | `(NpcID, Int)` | npc |

---

## Comparison Operators

| Operator | Meaning |
|----------|---------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `<=` | Less than or equal |
| `>` | Greater than |
| `>=` | Greater than or equal |

## Arithmetic Operators

Arithmetic operators require numeric types (Int or Float).

| Operator | Meaning |
|----------|---------|
| `+` | Addition |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Division |
