# Language Reference

This page is a comprehensive reference for all built-in types, relations, and effects in Mora. For a guided introduction to writing rules, see the [Language Guide](language-guide.md).

---

## Types

| Type | Description | Example |
|------|-------------|---------|
| `FormID` | Any game record | `NPC`, `Weapon` |
| `WeaponID` | Weapon record | bound from `weapon(W)` |
| `ArmorID` | Armor record | bound from `armor(A)` |
| `SpellID` | Spell record | bound from `spell(S)` |
| `PerkID` | Perk record | bound from `perk(P)` |
| `KeywordID` | Keyword record | `:WeapMaterialIron` |
| `FactionID` | Faction record | `:BanditFaction` |
| `RaceID` | Race record | `:NordRace` |
| `LocationID` | Location record | |
| `CellID` | Cell record | |
| `QuestID` | Quest record | |
| `String` | Text value | `"Nazeem"` |
| `Int` | Integer | `42` |
| `Float` | Decimal | `9.5` |

---

## Form Relations

Form relations enumerate what records exist in the load order. They are the starting point for most rules.

| Relation | Signature | Description |
|----------|-----------|-------------|
| `npc` | `(FormID)` | All NPC base records |
| `weapon` | `(WeaponID)` | All weapon records |
| `armor` | `(ArmorID)` | All armor records |
| `spell` | `(SpellID)` | All spell records |
| `perk` | `(PerkID)` | All perk records |
| `keyword` | `(KeywordID)` | All keyword records |
| `faction` | `(FactionID)` | All faction records |
| `race` | `(RaceID)` | All race records |
| `leveled_list` | `(FormID)` | All leveled list records |

---

## Property Relations

Property relations expose attributes of a form. Use them in rule bodies to filter or bind values.

| Relation | Signature | Description |
|----------|-----------|-------------|
| `has_keyword` | `(FormID, KeywordID)` | Form has keyword |
| `has_faction` | `(FormID, FactionID)` | NPC belongs to faction |
| `has_perk` | `(FormID, PerkID)` | NPC has perk |
| `has_spell` | `(FormID, SpellID)` | NPC has spell |
| `base_level` | `(FormID, Int)` | NPC's base level |
| `level` | `(FormID, Int)` | Alias for `base_level` |
| `race_of` | `(FormID, RaceID)` | NPC's race |
| `name` | `(FormID, String)` | Display name |
| `editor_id` | `(FormID, String)` | Editor ID string |
| `gold_value` | `(FormID, Int)` | Gold value |
| `weight` | `(FormID, Float)` | Carry weight |
| `damage` | `(FormID, Int)` | Weapon base damage |
| `armor_rating` | `(FormID, Int)` | Armor rating |

---

## Relationship Relations

Relationship relations describe structural connections between records.

| Relation | Signature | Description |
|----------|-----------|-------------|
| `template_of` | `(FormID, FormID)` | NPC template relationship |
| `leveled_entry` | `(FormID, FormID, Int)` | Leveled list entry: list, item, minimum level |
| `outfit_has` | `(FormID, FormID)` | Outfit contains item |

---

## Instance Relations

Instance relations reflect the **runtime state** of the game world. They are only available in dynamic rules, which are evaluated at runtime rather than compiled into a static patch.

> **Note:** Using any instance relation makes a rule dynamic. Dynamic rules cannot be frozen at compile time because their truth value depends on live game state (the player's level, location, active quests, etc.) rather than static load order data.

| Relation | Signature | Description |
|----------|-----------|-------------|
| `current_level` | `(FormID, Int)` | Runtime character level |
| `current_location` | `(FormID, LocationID)` | Current location |
| `current_cell` | `(FormID, CellID)` | Current cell |
| `equipped` | `(FormID, FormID)` | Has item equipped |
| `in_inventory` | `(FormID, FormID, Int)` | Item in inventory with count |
| `quest_stage` | `(QuestID, Int)` | Current quest stage |
| `is_alive` | `(FormID)` | Character is alive |

---

## Effects

Effects are the actions Mora applies to matching forms. They appear in the `then` block of a rule.

| Effect | Signature | Description |
|--------|-----------|-------------|
| `add_keyword` | `(FormID, KeywordID)` | Add keyword to form |
| `remove_keyword` | `(FormID, KeywordID)` | Remove keyword from form |
| `add_item` | `(FormID, FormID)` | Add item to container or NPC |
| `add_spell` | `(FormID, SpellID)` | Add spell to NPC |
| `add_perk` | `(FormID, PerkID)` | Add perk to NPC |
| `set_name` | `(FormID, String)` | Set display name |
| `set_damage` | `(FormID, Int)` | Set weapon base damage |
| `set_armor_rating` | `(FormID, Int)` | Set armor rating |
| `set_gold_value` | `(FormID, Int)` | Set gold value |
| `set_weight` | `(FormID, Float)` | Set carry weight |
| `distribute_items` | `(FormID, FormID)` | Distribute items to NPC |
| `set_game_setting` | `(FormID, Float)` | Modify a game setting |

---

## Comparison Operators

Comparison operators can be used in rule bodies to constrain numeric values.

| Operator | Meaning |
|----------|---------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `<=` | Less than or equal |
| `>` | Greater than |
| `>=` | Greater than or equal |

**Example:**

```mora
rule buff_weak_iron_weapons
where
    weapon(W),
    has_keyword(W, :WeapMaterialIron),
    damage(W, D),
    D < 8
then
    set_damage(W, 10)
```
