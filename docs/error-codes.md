# Mora diagnostic codes

Every diagnostic `mora` emits to the CLI or LSP carries a short code so
you can look up its meaning here instead of guessing from the message
text. Codes are intentionally **stable** — we add new ones, but we don't
renumber or repurpose existing ones so that editor / CI search strings
don't bit-rot.

Three families are in use today:

- `E0001`–`E0xxx` — **lexer/sema errors**. The source text is understood
  syntactically but something about its meaning is wrong: unknown
  reference, type mismatch, effect shape, etc.
- `P0001`–`P0007` — **parser errors**. The tokens didn't form a valid
  mora module; the parser bailed and the rest of the file is best-effort.
- `W0xxx` — **warnings**. Didn't stop compilation, but likely bug.

Kebab-case codes (`requires-unmet`, `plugin-missing`, `plugin-missing`,
…) come from the CLI/load-order layer rather than the language
front-end; those are expected to grow.

## Lexer

| Code   | Meaning |
|--------|---------|
| E0001  | lex error — unterminated string, invalid char, numeric literal overflow, etc. Message varies. |

## Parser (P0001–P0007)

| Code  | Meaning |
|-------|---------|
| P0001 | parse error (generic) |
| P0002 | unexpected token at top level |
| P0003 | expected newline after `:` |
| P0004 | expected `(` after identifier in rule body |
| P0005 | unexpected token in rule body |
| P0006 | expected expression |
| P0007 | expected verb (`set`/`add`/`sub`/`remove`) after `=>` |

## Name resolution (E011–E013)

| Code | Meaning |
|------|---------|
| E011 | unknown fact or rule |
| E012 | duplicate rule definition |
| E013 | unknown identifier — e.g. `@SymbolRef` that's neither an editor id, relation, nor bound variable |

## Type checker (E020–E041, W007)

| Code | Meaning |
|------|---------|
| E020 | relation argument count or shape mismatch |
| E021 | type mismatch for a variable |
| E022 | type mismatch — expected X, got Y |
| E023 | unknown relation in body or head |
| E030 | type mismatch in effect (set) |
| E031 | type mismatch in effect (add/sub) |
| E032 | arithmetic operator requires numeric type |
| E033 | comparison requires numeric types |
| E034 | effect op (set/add/sub/remove) not valid for this field |
| E035 | effect rejected by verb-legality policy |
| E036 | `event/*` relation used outside an `on` rule |
| E037 | effect not allowed in this rule shape |
| E040 | unknown function |
| E041 | function arity/type mismatch |
| W007 | unused variable bound in rule body |

## Phase classifier

| Code                     | Meaning |
|--------------------------|---------|
| `E_PHASE_UNANNOTATED`    | rule has dynamic relations in body but is missing the `maintain` / `on` annotation |

## Load order / CLI (kebab-case)

| Code             | Meaning |
|------------------|---------|
| `requires-unmet` | a module's `requires mod("X")` dependency isn't in the resolved load order; the module's rules are skipped |
| `plugin-missing` | a plugin listed in `plugins.txt` could not be found in `--data-dir` (neither exact case nor case-insensitive match); the entry is dropped |

New codes should be added at the end of the appropriate section and
accompanied by a one-line description here. The test harness doesn't
enforce a 1:1 mapping, but keeping this catalog current is the first
check when adding a new `DiagBag::error(...)` call.
