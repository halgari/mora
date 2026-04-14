# Examples

Annotated `.mora` files in current (Mora v2) syntax. Each example shows
the full source and explains what it does. Read top to bottom or jump to
the pattern you need.

For a tutorial introduction see the [Language Guide](language-guide.md);
for the relation catalog see [relations.md](relations.md).

---

## 1. Iron weapons damage boost — static

The simplest useful rule: find every iron weapon and pin its damage.

```mora
namespace my_mod.balance

use form :as f

iron_weapons(W):
    f/weapon(W)
    f/keyword(W, @WeapMaterialIron)
    => set form/damage(W, 20)
```

### What it does

Iterates every WEAP record in the load order, keeps those with the
`WeapMaterialIron` keyword, and sets each one's base damage to `20`.

Because the body only references `form/*` relations, the phase
classifier tags this as **static**. The compiler fully evaluates the
rule against the FactDB and emits one patch entry per match. At game
load the runtime just applies the patches — no rule evaluation.

### Patterns

- `=> set` is legal on `form/damage` because its type is
  `countable<Int>`. `countable` also supports `add` and `sub`, if you
  wanted to bump or slash damage instead of pinning it.
- `f/weapon(W)` is the shape-gating predicate. Without it, `f/keyword`
  would match on any record carrying the keyword (armor, magic effects,
  NPCs). `form/*` relations are polymorphic across record types; restrict
  with a predicate like `form/weapon`, `form/npc`, `form/armor`.

---

## 2. Silver weapons except greatswords — static with negation

```mora
namespace my_mod.silver

use form :as f

vampire_bane(W):
    f/weapon(W)
    f/keyword(W, @WeapMaterialSilver)
    not f/keyword(W, @WeapTypeGreatsword)
    => add form/keyword(W, @VampireBane)
```

### What it does

Finds silver weapons that are **not** greatswords and adds the
`VampireBane` keyword to each. The silver keyword is preserved —
`form/keyword` is a `list<FormRef>`, so keywords accumulate unless you
explicitly `remove`.

### Patterns

- `not` applies to the single clause that follows. `W` is already bound
  by the earlier `f/weapon(W)` clause, so negation is safe.
- Both `add` and `remove` are legal on `list<_>` relations; `set`
  is not.

---

## 3. Tag bandits with derived rules

```mora
namespace my_mod.bandits

use form :as f

# Derived rule: name a concept once, reuse it.
bandit(NPC):
    f/npc(NPC)
    f/faction(NPC, @BanditFaction)

elite_bandit(NPC):
    bandit(NPC)
    f/base_level(NPC, L)
    L >= 30

# Patching rule: adds a keyword.
tag_elite_bandits(NPC):
    elite_bandit(NPC)
    => add form/keyword(NPC, @ActorTypeNPC)
```

### What it does

`bandit` and `elite_bandit` are **derived rules** (no effects, just
predicates). They're inlined wherever used. Changing the definition of
"bandit" once updates every downstream rule automatically.

---

## 4. Rename every NPC — scalar `set`

```mora
namespace test.nazeem

everyone_is_nazeem(NPC):
    form/npc(NPC)
    => set form/name(NPC, "Nazeem")
```

`form/name` is a `scalar<String>`. Only `set` is legal on a scalar; the
type checker would reject `=> add form/name(...)` with a verb-mismatch
error.

---

## 5. Maintain a threat marker — auto-retract

```mora
namespace my_mod.threats

use ref :as r
use form :as f

# Add a ThreatMarker keyword to every placed reference whose base is
# an NPC in the bandit faction. The keyword is automatically removed
# when the reference is unloaded or the faction membership changes.
maintain threat_marker(R):
    r/is_npc(R)
    r/base_form(R, Base)
    f/faction(Base, @BanditFaction)
    => add ref/keyword(R, @ThreatMarker)
```

### What it does

**`maintain`** tells the compiler this rule tracks truth values
differentially. The runtime engine tracks every binding: when the body
becomes satisfied, it calls the `add` handler and records an effect
handle; when the body stops being satisfied, it uses the handle to call
the matching retract handler. No removal logic in the rule.

### Patterns

- `r/base_form(R, Base)` is the canonical join from a live reference
  (`RefId`) to its base record (`FormRef`). Any `form/*` query over a
  reference must go through `base_form` first.
- `ref/keyword` is declared `list<FormRef>` with both `apply_handler`
  (`RefAddKeyword`) and `retract_handler` (`RefRemoveKeyword`) wired
  up, so using it in `maintain` is legal. A `maintain` rule targeting
  a list relation that lacks a retract handler is a compile error.

---

## 6. Bandit bounty — `on` rule with arithmetic

This is the capstone. It's an edge-triggered rule with arithmetic and
a built-in function — exactly the file shipped as
`test_data/bandit_bounty.mora`.

```mora
namespace my_mod.bounty

use ref :as r
use form :as f

# Pay the player a bounty for killing a bandit, scaled by the victim's
# level plus a danger bonus when the victim out-levels the player.
on bandit_bounty(Player, Victim):
    event/killed(Victim, Player)
    r/is_player(Player)
    r/is_npc(Victim)
    r/base_form(Victim, Base)
    f/faction(Base, @BanditFaction)
    r/level(Victim, VL)
    r/level(Player, PL)
    => add player/gold(Player, 10 * VL + 5 * max(0, VL - PL))
```

### What it does

When an actor kills another actor, the `event/killed` relation fires.
If the killer is the player, the victim is an NPC in `BanditFaction`,
and both have levels, the rule credits the player:

- A base reward of `10 * VL`, scaling with victim level.
- A danger bonus of `5 * max(0, VL - PL)`: extra gold when the victim
  out-levels the player, zero otherwise. `max(0, ...)` clamps the
  difference from below so a lower-level victim doesn't subtract.

Example: level-4 victim, level-1 player → `10*4 + 5*max(0,3) = 55` gold.
Same victim, level-10 player → `10*4 + 5*max(0,-6) = 40` gold.

### Patterns

- **`on`** — edge-triggered, fires once per `+1` transition of the body.
  Unlike `maintain`, there's no retraction; adding gold is a one-shot.
- `event/killed` drives the edge. Using any `event/*` relation is what
  makes a rule `on`-eligible; using it in a `maintain` rule is a compile
  error (events are deltas, not state).
- `r/base_form` crosses from the live `Victim` reference into
  `form/faction` on its base. Without the bridge, the type checker
  would reject `form/faction(Victim, ...)` because `Victim` is a
  `RefId`, not a `FormRef`.
- Arithmetic binds with standard precedence: `10 * VL + 5 * max(...)`
  parses as `(10*VL) + (5*max(...))`, not left-to-right.
- Built-in `max` widens to `Float` when any argument is a float; here
  both `VL` and `PL` are `Int`, so the result is `Int`.

---

## 7. Weapon rebalance — multi-rule file

A realistic file combining derived rules, multiple effects per rule,
and negation.

```mora
namespace rebalance.weapons

use form :as f

# Derived: all non-unique, non-staff weapons.
common_weapon(W):
    f/weapon(W)
    not f/keyword(W, @WeapTypeStaff)
    not f/keyword(W, @DaedricArtifact)

# Iron tier: modest damage, low gold value.
iron_rebalance(W):
    common_weapon(W)
    f/keyword(W, @WeapMaterialIron)
    => set form/damage(W, 12)
    => set form/gold_value(W, 15)

# Steel tier: better damage, higher value.
steel_rebalance(W):
    common_weapon(W)
    f/keyword(W, @WeapMaterialSteel)
    => set form/damage(W, 18)
    => set form/gold_value(W, 45)
```

### Patterns

- Single definition, multiple consumers. Both tier rules inherit the
  exclusions in `common_weapon` for free.
- Multiple effects per rule. Each `=>` line applies to every match;
  the body only evaluates once.
- Rules don't interact. A weapon with both `WeapMaterialIron` and
  `WeapMaterialSteel` (unusual but possible) would be patched by both
  rules; conflict resolution (last rule wins by file order) is reported
  by `mora inspect --conflicts`.

---

See [relations.md](relations.md) for the full catalog of relations you
can reference in bodies and heads; see [language-guide.md](language-guide.md)
for the tutorial walk-through.
