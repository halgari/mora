# KID Grammar Compatibility Matrix

This page is the public source of truth for which KID INI features
Mora currently supports. Each cell in the table is one of:

- ✅ — fully supported; golden-tested against real KID output
- 🟡 — supported but not yet golden-tested, or with caveats
- ❌ — not yet supported
- — — not applicable to this record type

Cells move left-to-right (❌ → 🟡 → ✅) as milestones land:

- **M3** ships Weapon + Armor × all filter types (first green cells).
- **M6** adds remaining record types (Ammo, Ingredient, …, Ingestible).
- **M7** adds the rarer distribution-mode variants and edge-case traits.

See `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md` for
the overall rollout plan.

## Record types × filter families

| Record type           | Form filter | Keyword filter | String filter | Trait filter | Chance |
|-----------------------|-------------|----------------|---------------|--------------|--------|
| Weapon                | ❌          | ❌             | ❌            | ❌           | ❌     |
| Armor                 | ❌          | ❌             | ❌            | ❌           | ❌     |
| Ammo                  | ❌          | ❌             | ❌            | ❌           | ❌     |
| MagicEffect           | ❌          | ❌             | ❌            | ❌           | ❌     |
| Potion                | ❌          | ❌             | ❌            | ❌           | ❌     |
| Scroll                | ❌          | ❌             | ❌            | ❌           | ❌     |
| Location              | ❌          | ❌             | ❌            | ❌           | ❌     |
| Ingredient            | ❌          | ❌             | ❌            | ❌           | ❌     |
| Book                  | ❌          | ❌             | ❌            | ❌           | ❌     |
| MiscItem              | ❌          | ❌             | ❌            | ❌           | ❌     |
| Key                   | ❌          | ❌             | ❌            | ❌           | ❌     |
| SoulGem               | ❌          | ❌             | ❌            | ❌           | ❌     |
| SpellItem             | ❌          | ❌             | ❌            | ❌           | ❌     |
| Activator             | ❌          | ❌             | ❌            | ❌           | ❌     |
| Flora                 | ❌          | ❌             | ❌            | ❌           | ❌     |
| Furniture             | ❌          | ❌             | ❌            | ❌           | ❌     |
| Race                  | ❌          | ❌             | ❌            | ❌           | ❌     |
| TalkingActivator      | ❌          | ❌             | ❌            | ❌           | ❌     |
| Enchantment           | ❌          | ❌             | ❌            | ❌           | ❌     |
| Ingestible            | ❌          | ❌             | ❌            | ❌           | ❌     |

## Distribution modes

| Mode                  | Status |
|-----------------------|--------|
| Regular               | ❌     |
| DistributeByWeight    | ❌     |
| DistributeByValue     | ❌     |

## Boolean composition

| Operator | Meaning           | Status |
|----------|-------------------|--------|
| `-`      | NOT               | ❌     |
| `\|`     | OR                | ❌     |
| `,`      | AND               | ❌     |

## Trait-filter coverage (per record type)

Placeholder — the trait inventory is record-type-specific and will be
filled in during M3 (Weapon/Armor) and M6 (remainder). Reference
`LookupFilters.cpp` in the upstream KID repository for the full set.
