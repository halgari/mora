# Language Guide

A tutorial walk through writing `.mora` files in the current (Mora v2)
syntax. Each section builds on the previous. For the full relation
inventory see [relations.md](relations.md); for the architecture see
[how-mora-works.md](how-mora-works.md).

---

## File structure

Every `.mora` file starts with a `namespace` declaration, optional
`requires mod(...)` guards, and optional `use` imports:

```mora
namespace my_mod.weapons

requires mod("Skyrim.esm")

use form :as f
use ref  :as r
use event :refer [killed, entered_location]
```

- **`namespace`** — unique identifier for this module. Other files can
  `use my_mod.weapons :refer [rule_name]` to import your rules.
- **`requires mod(...)`** — compile fails if the plugin isn't present in
  the load order. Add one line per dependency.
- **`use`** — Clojure-style namespace import. `:as ALIAS` defines an
  alias; `:refer [a, b]` pulls names in unqualified. Unaliased fully
  qualified names (`form/weapon`) always work — `use` is purely additive.

There is no prelude: every namespace use is explicit.

---

## Rules

A rule has a **head** (name and arguments, colon-terminated), an optional
annotation (`maintain` or `on`), and a **body** of clauses and effects:

```mora
my_rule(NPC):
    form/npc(NPC)
    form/keyword(NPC, @Bandit)
```

Indentation is significant (Python-style). Multiple body clauses are
implicitly AND. Rules without any `=>` effect are **derived rules** —
they name a concept for reuse but don't patch anything on their own.

---

## Variables and identifiers

| Syntax                | Meaning                                           |
|-----------------------|---------------------------------------------------|
| `Weapon`, `NPC`, `VL` | Logic variable (Capitalized identifier)           |
| `@WeapMaterialIron`   | EditorID reference, resolved to a FormID at compile time |
| `:low`, `:fire`       | Keyword (interned symbol, user-defined tag value) |
| `42`, `1.5`           | Int / Float literal                               |
| `"Nazeem"`            | String literal                                    |
| `form/weapon`         | Namespaced relation name                          |

**`@EditorID`** is how you refer to a specific record by its Editor ID.
Mora looks it up in your ESPs at compile time and fails the build if
nothing matches.

**`:keyword`** is a completely different thing — a lightweight, interned
tag value. You can use keywords as rule arguments to categorize things
without allocating FormIDs for them. Example:

```mora
threat_level(NPC, :high):
    form/npc(NPC)
    form/base_level(NPC, L)
    L >= 30

threat_level(NPC, :low):
    form/npc(NPC)
    form/base_level(NPC, L)
    L < 10
```

The result is your own `threat_level` predicate whose second argument is
the keyword `:low` or `:high`.

---

## Clauses

Body clauses are queries:

- **Positive pattern**: `ns/rel(args)`. True when the relation holds.
- **Negation**: `not ns/rel(args)`. True when it doesn't. Only use on
  clauses whose variables are already bound by earlier clauses.
- **Comparison**: `Expr op Expr` with `>`, `<`, `>=`, `<=`, `==`, `!=`.

Example combining most of them:

```mora
high_value_weapon(W):
    form/weapon(W)
    form/gold_value(W, GV)
    GV >= 1000
    not form/keyword(W, @DaedricArtifact)
```

---

## Types and verbs

Each relation carries a type of the form `ctor<Elem>`:

| Type constructor | Legal verbs in head position | Example effect                            |
|------------------|------------------------------|-------------------------------------------|
| `scalar<T>`      | `set`                        | `=> set form/name(F, "Nazeem")`           |
| `countable<T>`   | `set`, `add`, `sub`          | `=> add player/gold(P, 100)`              |
| `list<T>`        | `add`, `remove`              | `=> add form/keyword(W, @Enchanted)`      |
| `const<T>`       | *(none — read-only)*         | used only in body position                |
| `predicate`      | *(none — unary existence)*   | `form/npc(F)`                             |

Argument types are one of: `Int`, `Float`, `String`, `FormRef`, `Keyword`,
`RefId`. Mismatches are compile errors pointing at the source span.

The full list of every relation, its type, and which verbs are legal is in
[relations.md](relations.md).

---

## Effects

Effects follow the body, each on its own line, prefixed with `=> <verb>`:

```mora
iron_boost(W):
    form/weapon(W)
    form/keyword(W, @WeapMaterialIron)
    => set form/damage(W, 20)
    => add form/keyword(W, @WeapMaterialDaedric)
```

Every `=>` line applies to every binding that satisfies the body. The
verb is validated against the relation's type constructor at compile time;
`=> add form/name(F, "...")` would fail because `form/name` is a scalar
and scalars only support `set`.

The **same relation** that you query in the body you assert in the head.
`form/keyword(W, @Iron)` in the body is a query, in the head after
`add` it's an assertion. Verbs distinguish the kind of assertion, so
there are no separate `has_keyword` / `add_keyword` / `remove_keyword`
relations — there's just `form/keyword`.

---

## Static, `maintain`, `on` — three rule tiers

The compiler classifies each rule from its body dependencies:

- **Unannotated** — all body relations live in compile-time-known
  namespaces (`form/*`). Fully evaluated at compile time.
- **`maintain`** — body uses runtime state (`ref/*`, `player/*`,
  `world/*`). Compiled to an operator DAG; effects auto-retract when the
  body stops being satisfied.
- **`on`** — body uses at least one `event/*` relation. Compiled to a
  DAG; effects fire once on `+1`, no retraction.

Examples of each:

```mora
# Static: compile-time patch, frozen at build time.
iron_weapons(W):
    form/weapon(W)
    form/keyword(W, @WeapMaterialIron)
    => set form/damage(W, 20)

# Maintain: tag refs whose base is a bandit; auto-retracts when the
# ref is unloaded (or its base changes).
maintain threat_marker(R):
    ref/is_npc(R)
    ref/base_form(R, Base)
    form/faction(Base, @BanditFaction)
    => add ref/keyword(R, @ThreatMarker)

# On: edge-triggered — fires exactly once per killing blow.
on kill_notify(Player, Victim):
    event/killed(Victim, Player)
    ref/is_player(Player)
    => add player/notification(Player, "Target eliminated.")
```

Misuses are compile errors: unannotated rule depending on a dynamic
relation, `maintain` rule using `event/*`, `maintain` rule whose effect
has no retract handler.

---

## Arithmetic and built-in functions

Arithmetic works in head-position expressions, with standard precedence
(`*` `/` bind tighter than `+` `-`). Variables are bound by body clauses;
arithmetic only reads them:

```mora
heavy_tax(W):
    form/weapon(W)
    form/gold_value(W, V)
    V > 100
    => set form/gold_value(W, V * 2 - 50)
```

Built-in functions: `max`, `min`, `abs`, `clamp`. They're pure,
validated at compile time (unknown name or wrong arity is an error),
and widen to `Float` if any argument is a float.

```mora
# Scaled bounty that never goes negative.
on bandit_bounty(Player, Victim):
    event/killed(Victim, Player)
    ref/is_player(Player)
    ref/is_npc(Victim)
    ref/base_form(Victim, Base)
    form/faction(Base, @BanditFaction)
    ref/level(Victim, VL)
    ref/level(Player, PL)
    => add player/gold(Player, 10 * VL + 5 * max(0, VL - PL))
```

---

## Derived rules

A rule without effects defines a derived predicate that other rules can
use as if it were built-in:

```mora
bandit(NPC):
    form/npc(NPC)
    form/faction(NPC, @BanditFaction)

elite_bandit(NPC):
    bandit(NPC)
    form/base_level(NPC, L)
    L >= 30

arm_elites(NPC):
    elite_bandit(NPC)
    => add form/spell(NPC, @FlameCloak)
```

Derived rules are fully inlined during evaluation. Use them wherever a
definition would otherwise be copied.

---

## Negation

```mora
silver_but_not_greatsword(W):
    form/weapon(W)
    form/keyword(W, @WeapMaterialSilver)
    not form/keyword(W, @WeapTypeGreatsword)
    => add form/keyword(W, @VampireBane)
```

Negation applies to a single clause and requires its variables to be
already bound by earlier clauses.

Disjunction — matching "A or B" inside one rule body — is not yet a
surface feature. For now, factor each branch into its own derived rule
and consume both where you need the union:

```mora
valuable_weapon(Item):
    form/weapon(Item)
    form/gold_value(Item, V)
    V >= 1000

valuable_armor(Item):
    form/armor(Item)
    form/gold_value(Item, V)
    V >= 1000
```

---

## Putting it together

The bandit bounty from `test_data/bandit_bounty.mora` is a good capstone.
It's an `on` rule — edge-triggered on the kill event — joining a live
reference to its base record through the `ref/base_form` bridge, reading
two stats, then computing an arithmetic reward with `max`:

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

For more annotated examples — static keyword patches, maintain rules with
auto-retract, additional `on` rules — see [examples.md](examples.md).
