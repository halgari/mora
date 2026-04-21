# F3 — Weapon trait predicate bisection

**Status:** open, medium-priority. ~50 forms affected in one scenario; likely a single-predicate semantic bug.

## Symptom

Scenario `traits_weapon_all` with rule:

```ini
WeapMaterialIron = Weapon||OneHandSword,D(5 20),W(5.0 15.0),-E
```

matches ~50 more weapons in mora-kid than in real KID. All divergences are "mora has extra WeapMaterialIron where golden does not."

Example forms (all vanilla Skyrim.esm weapons, one-handed swords, value/weight in range, unenchanted per xEdit inspection):

```
form 0x00013135: missing [], extra [FormId(124696)]
form 0x000135b8: missing [], extra [FormId(124696)]
form 0x00013989..0x000139b1: missing [], extra [FormId(124696)]  // ~8 consecutive
form 0x0002c66f, 0x0003aeb9, 0x000557f5, 0x0005bf14, ...        // etc.
```

## Shortest repro

```bash
export MORA_SKYRIM_DATA=/skyrim-base/Data
cargo test --package mora-kid --test golden golden_traits_weapon_all -- --ignored --nocapture
```

## Leading hypothesis

The rule has four predicates: `OneHandSword`, `D(5 20)` (damage 5-20), `W(5.0 15.0)` (weight 5-15), `-E` (unenchanted). One of them has subtly different semantics from KID's:

1. **`-E` (unenchanted)** — mora's `require_enchanted = Some(false)` evaluated by `!weapon.enchantment.is_some()`. KID might classify enchantment differently — perhaps forms with empty-but-present EITM count differently. If any of these ~50 weapons have an EITM of 0x00000000 (null enchantment reference), that's "has EITM subrecord but pointing nowhere" — KID might treat that as "enchanted" while mora treats it as "unenchanted."

2. **`D(5 20)`** — damage range. mora reads DATA.damage (u16). KID may use a DIFFERENT damage field — perhaps base damage × some multiplier, or a field in DNAM.

3. **`W(5.0 15.0)`** — weight range. Skyrim stores weight as f32 in DATA. mora reads this directly. KID probably uses the same field; unlikely to diverge.

4. **`OneHandSword`** — animation type, mora reads DNAM offset 0x00. KID likely reads the same. Unlikely to diverge.

Best bet: **`-E` edge case** OR **`D(min max)` inclusive/exclusive bounds** (mora's Plan 8b implementation uses inclusive-both-ends; KID may use exclusive on one side).

## How to investigate

1. **Pick one form with extra WeapMaterialIron** (e.g., `0x00013135`). In xEdit, verify each predicate field: animation_type, damage, weight, EITM. Determine WHICH predicate KID evaluates as false and mora as true.
2. **Bisect.** Trim the rule one predicate at a time: start with just `OneHandSword`, then add each other predicate, re-capture, see when mora-and-KID diverge. The predicate whose addition makes the divergence vanish (or appear) is the culprit.
3. **Read KID's trait eval source.** po3/Keyword-Item-Distributor `src/Filter.cpp` — look for `WeaponTraits::Matches` or equivalent. Compare to `crates/mora-kid/src/filter.rs::evaluate_weapon_traits`.

## Relevant files

- `crates/mora-kid/src/filter.rs::evaluate_weapon_traits`
- `crates/mora-kid/src/traits_weapon.rs` — trait parser (what KID grammar we implement)
- `crates/mora-esp/src/subrecords/weapon_data.rs`, `weapon_dnam.rs` — WEAP DATA + DNAM parsers
- `crates/mora-esp/src/records/weapon.rs` — WeaponRecord accessor
- `tests/golden-data/kid-inis/traits_weapon_all/traits_weapon_all_KID.ini`

## Acceptance

`cargo test --test golden golden_traits_weapon_all -- --ignored` passes. Unignore the test. Possibly also fixes armor's `traits_armor_all` if the bug is shared (e.g., `-E` semantics — enchantment is parsed the same way for both).
