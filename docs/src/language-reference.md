# Language Reference

Concise reference for Mora's type system, verbs, operators, and built-in
functions. For the full relation catalog — every relation, its signature,
type, legal verbs, and source — see [relations.md](relations.md), which
is auto-generated from `data/relations/**/*.yaml` and always reflects
the shipping set.

For a tutorial introduction, see the
[Language Guide](language-guide.md).

---

## Argument types

Every relation argument has one of these types:

| Type      | Description                                    |
|-----------|------------------------------------------------|
| `Int`     | 32-bit signed integer                          |
| `Float`   | IEEE-754 single-precision                      |
| `String`  | UTF-8 (stored as `BSFixedString` at runtime)   |
| `FormRef` | A record FormID (compile-time `@EditorID`)     |
| `RefId`   | A placed-reference FormID (live instance)      |
| `Keyword` | Interned symbol (user-defined `:tag` value)    |

`FormRef` refers to a base record (a WEAP, NPC\_, KYWD, …). `RefId`
refers to a placed reference — an instance of a base record in the world.
The only way from a `RefId` to its `FormRef` is the explicit bridge
`ref/base_form(R, F)`.

---

## Type constructors and verbs

Every relation is declared with a type of the form `ctor<Elem>`. The
constructor determines which verbs are legal in head (effect) position.

| Constructor       | Legal verbs in head        | Example                                  |
|-------------------|----------------------------|------------------------------------------|
| `scalar<T>`       | `set`                      | `=> set form/name(F, "Nazeem")`          |
| `countable<T>`    | `set`, `add`, `sub`        | `=> add player/gold(P, 100)`             |
| `list<T>`         | `add`, `remove`            | `=> add form/keyword(W, @Enchanted)`     |
| `const<T>`        | *(none — read-only)*       | `ref/health(R, H)` in body only          |
| `predicate`       | *(none — unary existence)* | `form/npc(F)`                            |

Verb/shape mismatches are compile errors pointing at the offending line.

---

## Effect syntax

```
=> <verb> <namespace>/<name>(<args>)
```

Examples:

```mora
=> set form/damage(W, 20)
=> add form/keyword(W, @Enchanted)
=> remove form/keyword(W, @Cursed)
=> sub player/gold(P, 10)
=> add player/notification(P, "You found a bandit.")
```

---

## Comparison operators

| Operator | Meaning                 |
|----------|-------------------------|
| `==`     | Equal                   |
| `!=`     | Not equal               |
| `<`      | Less than               |
| `<=`     | Less than or equal      |
| `>`      | Greater than            |
| `>=`     | Greater than or equal   |

Comparisons don't bind variables — their operands must already be bound
by earlier clauses.

---

## Arithmetic

| Operator | Meaning           |
|----------|-------------------|
| `+`      | Addition          |
| `-`      | Subtraction       |
| `*`      | Multiplication    |
| `/`      | Division          |

Standard precedence: `*` and `/` bind tighter than `+` and `-`.
Expressions widen to `Float` if any operand is a float.

---

## Built-in functions

| Function       | Arity | Description                                  |
|----------------|-------|----------------------------------------------|
| `max(a, b)`    | 2     | Larger of two numeric values                 |
| `min(a, b)`    | 2     | Smaller of two numeric values                |
| `abs(x)`       | 1     | Absolute value                               |
| `clamp(x,l,h)` | 3     | Clamp `x` to `[l, h]`                        |

All are pure and deterministic. Unknown name or wrong arity is a hard
compile-time error. Result types widen to `Float` when any argument is a
float, otherwise stay `Int`.

---

## Rule annotations

| Annotation    | Meaning                                                       |
|---------------|---------------------------------------------------------------|
| *(none)*      | Static. Body uses only compile-time-known relations.          |
| `maintain`    | Differential truth maintenance; auto-retracts on `-1`.        |
| `on`          | Edge-triggered; fires once on `+1`, no retraction.            |

The phase classifier validates annotations against the rule body:

- Unannotated rule using any dynamic relation → error, "did you mean `maintain` or `on`?"
- `maintain` rule using `event/*` → error (events are edge-shaped, not state).
- `maintain` rule whose head uses an effect without a retract handler → error.

---

## Identifier literals

| Syntax              | Meaning                                                  |
|---------------------|----------------------------------------------------------|
| `@WeapMaterialIron` | EditorID reference; resolved to a FormID at compile time |
| `:high`, `:fire`    | Keyword (interned symbol)                                |
| `42`, `1.5`         | Int / Float literal                                      |
| `"Nazeem"`          | String literal (double-quoted, UTF-8)                    |
| `W`, `Base`         | Logic variable (Capitalized identifier)                  |

---

See also:

- [Relation Reference](relations.md) — the full relation inventory.
- [Language Guide](language-guide.md) — tutorial walk-through.
- [CLI Reference](cli-reference.md) — `mora compile`, `check`, `inspect`, `info`, `docs`.
