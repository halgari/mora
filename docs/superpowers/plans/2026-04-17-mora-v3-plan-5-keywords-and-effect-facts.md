# Mora v3 — Plan 5: Keywords + PatchSet → Effect-Fact Bridge

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `Value::Kind::Keyword` as a first-class scalar (lexed from `:ident`, propagated through parse/sema/eval), then bridge the evaluator's existing `PatchBuffer` output into the `skyrim/set`/`skyrim/add`/`skyrim/remove`/`skyrim/multiply` output relations so parquet snapshots have meaningful data for the first time.

**Architecture:** Two milestones stacked on top of Plan 4's schema machinery. M1 makes `:Name` a legal literal everywhere a scalar can appear — no semantic meaning yet beyond "an interned identifier you can pass around as data." M2 introduces a post-evaluation adapter that walks `PatchBuffer::entries()` and, for each `PatchEntry(FormID, field_id, op, Value)`, populates a tuple `(FormID, :FieldName, Value)` into the effect relation the `op` selects. `:FieldName` comes from the existing `field_name` helper already in `src/main.cpp`. The evaluator and `src/emit/` are untouched — this plan is strictly additive; the binary `mora_patches.bin` output continues alongside the new in-FactDB effect facts.

**Tech Stack:** C++20, xmake, gtest.

**Spec:** `/home/tbaldrid/.claude/plans/we-need-to-think-iridescent-abelson.md`. Plan 5 delivers the lexer/AST/Value piece of spec step 9 ("add `Kind::Keyword`") plus a pragmatic stepping stone toward step 11 ("evaluator produces output relations"). The full "evaluator stops producing PatchSet" transition arrives in a later plan once parquet handles heterogeneous value columns.

**Branch:** continue on `mora-v3-foundation`. Plan 5 layers two commits on top of Plan 4's twelve.

**Baseline:** HEAD `bd3f8ff` (P4 M3). Clean tree. `xmake build` green. 88 test binaries pass.

---

## Design notes (read before editing)

1. **Keyword storage piggybacks on String's slot.** `Value` already has `data_.string_index` for `Kind::String`; the new `Kind::Keyword` reuses the same `uint32_t` field, storing an interned `StringId`. No `union` expansion; `sizeof(Value)` unchanged.

2. **Lexer ambiguity resolution for `:`.** The existing lexer emits `TokenKind::Colon` for a bare `:` and `TokenKind::DoubleColon` for `::`. Plan 5 adds a third disambiguation: `:` followed by an identifier-start character (letter or `_`) is consumed as a single `TokenKind::Keyword` token whose `string_id` holds the interned identifier text (without the leading colon). Bare `:` and `::` behavior is unchanged.

3. **No sema type-checking.** Keywords are opaque scalars. Sema must not reject them where other scalar literals are accepted; it also must not try to infer a type for them beyond "this is a value of some scalar kind." The existing loose type system in `TypeChecker` treats Value kinds as coarse shapes; `Kind::Keyword` slots in like `Kind::String`.

4. **Effect-fact bridge layout.** The translator produces tuples `(FormID, :FieldName, Value)`:
   - Column 0: `Value::make_formid(formid)`.
   - Column 1: `Value::make_keyword(pool.intern(field_name(FieldId)))` — the existing free function `field_name()` in `src/main.cpp` maps `FieldId` enum values to strings like `"GoldValue"`, `"Name"`, etc.
   - Column 2: decoded `Value` from `(value_type, raw_u64)`:
     - `PatchValueType::FormID` → `Value::make_formid(static_cast<uint32_t>(value))`.
     - `PatchValueType::Int` → `Value::make_int(static_cast<int64_t>(value))`.
     - `PatchValueType::Float` → `Value::make_float(std::bit_cast<double>(value))`.
     - `PatchValueType::StringIndex` → `Value::make_string(StringId{static_cast<uint32_t>(value)})`.
   - Relation selected by `FieldOp`:
     - `Set → skyrim/set`
     - `Add → skyrim/add`
     - `Remove → skyrim/remove`
     - `Multiply → skyrim/multiply`

5. **`skyrim/multiply` needs to be registered.** Plan 4 registered only `set`/`add`/`remove` (three of four ops). Plan 5 M2 extends `register_skyrim` to also register `skyrim/multiply`. Without this, Multiply patches would have nowhere to land; we'd either drop them silently (bad) or have to introspect + lazy-register (complexity).

6. **Lazy `configure_relation` on FactDB.** The bridge function doesn't know ahead of time whether the output relations were pre-configured (they aren't — the only current FactDB configurator is the ESP reader's `schema.configure_fact_db(out)`, which only configures the core Skyrim default relations). The bridge calls `db.configure_relation(rel_id, /*arity*/ 3, /*indexed*/ {0})` lazily on the first fact it emits per relation. Re-configuring an already-configured relation with the same arity is a no-op under the existing FactDB semantics — safe either way. Alternative: have `SkyrimEspDataSource::load` pre-configure the four effect relations. **Plan 5 picks lazy configure** to keep the data path clean; if the load-time pre-configure turns out to read better, M2 can trivially move the calls.

7. **Column-name positional mismatch is acknowledged.** Plan 4's `register_skyrim` registers the three effect relations with column names `entity` / `field` / `value`, but the parquet sink's empty-output path emits Arrow fields named `col0`/`col1`/`col2` (it doesn't consult `ColumnSpec::name`). Plan 5 does NOT fix this — it's a parquet-sink concern outside the scope of keywords + translator. The schema column names still live in `ColumnSpec::name` for future consumers.

---

## File Map

### M1 — `Value::Kind::Keyword`

**Modified:**
- `include/mora/lexer/token.h` — add `TokenKind::Keyword`.
- `src/lexer/lexer.cpp` — add the `:ident` disambiguation in `lex_symbol` (the existing entry point that handles `:`). Update `token_kind_name` in `src/lexer/token.cpp`.
- `include/mora/ast/ast.h` — add `KeywordLiteral` struct + extend `Expression::data` variant.
- `src/parser/parser.cpp` — handle `TokenKind::Keyword` in the expression-parser primary-dispatch, producing a `KeywordLiteral`.
- `include/mora/data/value.h` — add `Kind::Keyword`; declare `make_keyword`, `as_keyword`.
- `src/data/value.cpp` — implement `make_keyword`, `as_keyword`; extend `operator==`, `matches` (inherited via == call), and `hash` to handle `Kind::Keyword`.
- `src/eval/evaluator.cpp` — extend any kind-switch in the literal-to-value conversion path (look for `StringLiteral` handling; mirror for `KeywordLiteral`).
- `src/sema/name_resolver.cpp` and/or `src/sema/type_checker.cpp` — treat `KeywordLiteral` like `StringLiteral`. Most likely a single case-add in each visitor.
- Any diagnostic-rendering / `format_value` helper that prints Values — add the `Kind::Keyword` case to print `:Name`.

**New tests:**
- `tests/lexer/test_keyword_token.cpp` — lexer tokenizes `:GoldValue` and `:Name` as `Keyword` tokens with the interned text.
- `tests/ast/test_keyword_literal.cpp` — parser round-trips `:GoldValue` into a `KeywordLiteral` AST node.
- `tests/value_test.cpp` — extend to cover `make_keyword`/`as_keyword`/equality/hash.
- `tests/integration/test_keyword_end_to_end.cpp` (NEW subdirectory) — a `.mora` source with a rule body containing a keyword literal is parsed, evaluated, and the resulting fact contains a `Kind::Keyword` value.

### M2 — PatchSet → effect-fact bridge

**Modified:**
- `extensions/skyrim_compile/src/register.cpp` — extend the effect-relation registration loop from 3 names (`skyrim/set`/`add`/`remove`) to 4 (`+ skyrim/multiply`).
- `src/main.cpp` — add a new free function `populate_effect_facts(PatchBuffer&, FactDB&, StringPool&)` alongside the existing compile-pipeline helpers. Call it in `cmd_compile` after `evaluate_mora_rules` and before the sink-dispatch loop.

**New tests:**
- `tests/cli/test_effect_facts_bridge.cpp` — construct a synthetic `PatchBuffer` with one entry of each op (Set/Add/Remove/Multiply), call `populate_effect_facts`, assert that the four output relations each contain exactly one tuple with the expected `(FormID, :Keyword, Value)` shape.

**Updated tests:**
- `extensions/skyrim_compile/tests/register_mirrors_schemas_test.cpp` — the `RegistersExactlyThreeOutputRelations` test (renamed from M2) must become `RegistersExactlyFourOutputRelations` and expect `{skyrim/add, skyrim/multiply, skyrim/remove, skyrim/set}`.

---

## Baseline

- [ ] **Step B1: Verify branch state**

```bash
cd /home/tbaldrid/oss/mora
git status
git log master..HEAD --oneline
```

Expected: clean; 12 commits; HEAD `bd3f8ff`.

- [ ] **Step B2: Verify build + tests**

```bash
xmake build 2>&1 | tail -3
xmake test 2>&1 | tail -3
```

Expected: clean; 88 binaries pass.

---

## Milestone 1 — `Value::Kind::Keyword`

Goal: lex `:ident`, parse it into a `KeywordLiteral`, carry it through the pipeline as a `Value` of `Kind::Keyword`, preserve equality/hashing, print as `:Name`.

### Task 1: Add `TokenKind::Keyword`

**Files:**
- Modify: `include/mora/lexer/token.h`
- Modify: `src/lexer/token.cpp` — find `token_kind_name` (or equivalent); add the `Keyword` case returning `"Keyword"`.

- [ ] **Step 1.1: Add the enum variant**

Edit `include/mora/lexer/token.h`. The enum currently reads (line 10-14):

```cpp
    Integer, Float, String, Symbol, EditorId, Variable, Identifier, Discard,

    // Keywords
    KwNamespace, KwRequires, ...
```

Change the first line to:

```cpp
    Integer, Float, String, Keyword, Symbol, EditorId, Variable, Identifier, Discard,
```

(`Keyword` is inserted between `String` and `Symbol` to group literal-style tokens together.)

- [ ] **Step 1.2: Add the name**

Find `token_kind_name` in `src/lexer/token.cpp`. It's a function returning `const char*` with a switch on `TokenKind`. Add:

```cpp
        case TokenKind::Keyword: return "Keyword";
```

in alphabetical or enum-order position (match the existing style in the file).

- [ ] **Step 1.3: Build the lexer library**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_lib 2>&1 | tail -5
```

Expected: clean — the enum addition compiles without requiring the lexer to emit the new token yet.

### Task 2: Lex `:ident` as `TokenKind::Keyword`

**Files:**
- Modify: `src/lexer/lexer.cpp`

The existing `lex_symbol()` handles `:` and `::`. We extend it to also handle `:<ident-start>`.

- [ ] **Step 2.1: Find the `:` handling**

In `src/lexer/lexer.cpp`, search for `TokenKind::Colon`. There's a small dispatch that reads the next char to decide between `::` (DoubleColon) and `:` (Colon). Currently roughly:

```cpp
    case ':':
        if (match(':')) return make_token(TokenKind::DoubleColon);
        return make_token(TokenKind::Colon);
```

Change to:

```cpp
    case ':': {
        if (match(':')) return make_token(TokenKind::DoubleColon);
        // :ident  → keyword literal token with interned identifier text
        //           (the leading colon is not part of the name).
        char c = peek();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t const name_start = pos_;
            while (!at_end()) {
                char cc = peek();
                if (std::isalnum(static_cast<unsigned char>(cc)) ||
                    cc == '_' || cc == '-' || cc == '.') {
                    advance();
                } else {
                    break;
                }
            }
            std::string_view const name(source_.data() + name_start,
                                         pos_ - name_start);
            StringId const sid = pool_.intern(name);
            auto tok = make_token(TokenKind::Keyword);
            tok.string_id = sid;
            return tok;
        }
        return make_token(TokenKind::Colon);
    }
```

Allow `-` and `.` in keyword names so values like `:gold-value` or `:form.name` are future-friendly; the existing identifier lexer likewise accepts these characters in some positions. If the existing identifier policy is stricter, narrow this — the minimum is `[A-Za-z_][A-Za-z0-9_]*`.

- [ ] **Step 2.2: Include `<cctype>` if not already present**

Check the top of `src/lexer/lexer.cpp` for `#include <cctype>`. If absent, add it.

- [ ] **Step 2.3: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_lib 2>&1 | tail -5
```

Expected: clean.

### Task 3: Unit-test the lexer

**Files:**
- Create: `tests/lexer/test_keyword_token.cpp`

- [ ] **Step 3.1: Write the test**

```cpp
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include "mora/lexer/lexer.h"

#include <gtest/gtest.h>

namespace {

TEST(LexerKeyword, TokenizesColonIdent) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::Lexer lex(":GoldValue :Name",  "test", pool, diags);

    auto t1 = lex.next();
    EXPECT_EQ(t1.kind, mora::TokenKind::Keyword);
    EXPECT_EQ(pool.get(t1.string_id), "GoldValue");

    auto t2 = lex.next();
    EXPECT_EQ(t2.kind, mora::TokenKind::Keyword);
    EXPECT_EQ(pool.get(t2.string_id), "Name");
}

TEST(LexerKeyword, BareColonStillColon) {
    mora::StringPool pool;
    mora::DiagBag diags;
    // Bare `:` unambiguously — not followed by an identifier-start char.
    mora::Lexer lex(": ", "test", pool, diags);

    auto t = lex.next();
    EXPECT_EQ(t.kind, mora::TokenKind::Colon);
}

TEST(LexerKeyword, DoubleColonStillDoubleColon) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::Lexer lex("::foo", "test", pool, diags);

    auto t1 = lex.next();
    EXPECT_EQ(t1.kind, mora::TokenKind::DoubleColon);

    auto t2 = lex.next();
    EXPECT_EQ(t2.kind, mora::TokenKind::Identifier);
}

} // namespace
```

- [ ] **Step 3.2: Build + run**

```bash
cd /home/tbaldrid/oss/mora
xmake build test_keyword_token 2>&1 | tail -3
./build/linux/x86_64/debug/test_keyword_token 2>&1 | tail -15
```

Expected: 3 test cases pass.

If xmake doesn't discover the new file, re-run `xmake f -p linux -m debug --yes` and try again (xmake evaluates `os.files` at config time).

### Task 4: Add `Value::Kind::Keyword`

**Files:**
- Modify: `include/mora/data/value.h`
- Modify: `src/data/value.cpp`

- [ ] **Step 4.1: Extend the `Kind` enum**

Edit `include/mora/data/value.h`. The enum currently is:

```cpp
    enum class Kind { Var, FormID, Int, Float, String, Bool, List };
```

Change to:

```cpp
    enum class Kind { Var, FormID, Int, Float, String, Keyword, Bool, List };
```

(`Keyword` after `String` is analogous; storage reuses `string_index`.)

- [ ] **Step 4.2: Add constructor + accessor declarations**

In the same header, under the existing `make_*` block, add:

```cpp
    static Value make_keyword(StringId s);
```

Under the existing `as_*` block:

```cpp
    StringId as_keyword() const;
```

- [ ] **Step 4.3: Implement in value.cpp**

Edit `src/data/value.cpp`. After `make_string`:

```cpp
Value Value::make_keyword(StringId s) {
    Value v;
    v.kind_ = Kind::Keyword;
    v.data_.string_index = s.index;
    return v;
}
```

After `as_string`:

```cpp
StringId Value::as_keyword() const {
    assert(kind_ == Kind::Keyword);
    return StringId{data_.string_index};
}
```

- [ ] **Step 4.4: Extend `operator==`**

In the existing `operator==` switch, add a case:

```cpp
        case Kind::Keyword: return data_.string_index == other.data_.string_index;
```

Place between `String` and `Bool` to match enum order.

- [ ] **Step 4.5: Extend `hash`**

Same file, `hash()`:

```cpp
        case Kind::Keyword: return std::hash<uint32_t>{}(data_.string_index);
```

Also place between `String` and `Bool`.

- [ ] **Step 4.6: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_lib 2>&1 | tail -5
```

Expected: clean. Compiler may warn about missing `Kind::Keyword` cases in any `switch` that doesn't have `default:` — fix each warning point per Task 5 (they're the `format_value` / printer / evaluator entry points).

### Task 5: Wire through the AST, parser, sema, evaluator, formatter

**Files:**
- Modify: `include/mora/ast/ast.h`
- Modify: `src/parser/parser.cpp`
- Modify: `src/sema/name_resolver.cpp` (or wherever `StringLiteral` is handled)
- Modify: `src/sema/type_checker.cpp`
- Modify: `src/eval/evaluator.cpp`
- Modify: any value-print helper (find via `grep -n 'Kind::String' src/cli src/diag src/eval`)

- [ ] **Step 5.1: Add `KeywordLiteral` to the AST**

Edit `include/mora/ast/ast.h`. After `struct StringLiteral { StringId value; SourceSpan span; };`:

```cpp
struct KeywordLiteral { StringId value; SourceSpan span; };
```

Extend the `Expression::data` variant. Find the existing declaration (around line 45):

```cpp
    std::variant<VariableExpr, SymbolExpr, EditorIdExpr, IntLiteral, FloatLiteral,
                 StringLiteral, DiscardExpr, BinaryExpr, CallExpr> data;
```

Change to:

```cpp
    std::variant<VariableExpr, SymbolExpr, EditorIdExpr, IntLiteral, FloatLiteral,
                 StringLiteral, KeywordLiteral, DiscardExpr, BinaryExpr, CallExpr> data;
```

- [ ] **Step 5.2: Parse `TokenKind::Keyword` into `KeywordLiteral`**

Edit `src/parser/parser.cpp`. Find the primary-expression dispatch that handles `TokenKind::String` (producing a `StringLiteral`). Add a parallel case for `TokenKind::Keyword`:

```cpp
    if (cur_.kind == TokenKind::Keyword) {
        auto const span = cur_.span;
        auto const sid  = cur_.string_id;
        advance_token();
        Expression e;
        e.data = KeywordLiteral{sid, span};
        return e;
    }
```

The exact location and function name depends on the parser's structure — search for `TokenKind::String` and mirror the pattern.

- [ ] **Step 5.3: Handle `KeywordLiteral` in sema**

In `src/sema/name_resolver.cpp`, find where `StringLiteral` is visited. It's most likely a `std::visit` over `Expression::data`. Add a matching `[](const KeywordLiteral&) {}` or equivalent case — keyword literals have no names to resolve.

In `src/sema/type_checker.cpp`, similarly find where `StringLiteral` is handled and add the mirror for `KeywordLiteral`. The type it produces should match whatever StringLiteral produces (a "scalar" type in the existing coarse type system).

- [ ] **Step 5.4: Handle `KeywordLiteral` in the evaluator**

In `src/eval/evaluator.cpp`, find the expression-to-Value conversion (usually a visitor over `Expression::data` or a helper like `eval_expr`). Where `StringLiteral` maps to `Value::make_string(lit.value)`, add:

```cpp
        [&](const KeywordLiteral& kw) {
            return Value::make_keyword(kw.value);
        },
```

(or equivalent code depending on the visitor style).

- [ ] **Step 5.5: Handle `Kind::Keyword` in value printing**

Find any switch on `Value::Kind` in `src/diag/`, `src/cli/`, or `src/eval/` that prints values. A likely candidate is `format_value` or similar. Add the `Kind::Keyword` case:

```cpp
        case Value::Kind::Keyword: return ":" + std::string(pool.get(v.as_keyword()));
```

Run `grep -rnE 'case .*Kind::String' src include | head -20` to find all the sites to patch.

- [ ] **Step 5.6: Build + run full suite**

```bash
cd /home/tbaldrid/oss/mora
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -3
```

Expected: clean; 88 binaries pass (existing tests unchanged; no new bins yet).

### Task 6: Round-trip test

**Files:**
- Create: `tests/value_test_keyword.cpp` (new test file for the `tests/*_test.cpp` glob — note the `_test.cpp` suffix for the top-level glob pattern).

Actually the existing `tests/value_test.cpp` is the natural home. Extend it.

- [ ] **Step 6.1: Add gtest cases to `tests/value_test.cpp`**

Append:

```cpp
TEST(ValueKeyword, MakeAndRead) {
    mora::StringPool pool;
    auto name = pool.intern("Name");
    auto v = mora::Value::make_keyword(name);

    EXPECT_EQ(v.kind(), mora::Value::Kind::Keyword);
    EXPECT_EQ(v.as_keyword(), name);
}

TEST(ValueKeyword, EqualityAndHash) {
    mora::StringPool pool;
    auto a = mora::Value::make_keyword(pool.intern("GoldValue"));
    auto b = mora::Value::make_keyword(pool.intern("GoldValue"));
    auto c = mora::Value::make_keyword(pool.intern("Name"));

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_EQ(a.hash(), b.hash());
}

TEST(ValueKeyword, DistinctFromString) {
    mora::StringPool pool;
    auto id = pool.intern("Name");
    auto kw  = mora::Value::make_keyword(id);
    auto str = mora::Value::make_string(id);

    EXPECT_NE(kw, str)
        << "keywords and strings must compare unequal even when the "
           "interned text is identical";
}
```

The third case is the important invariant: `:Name` and `"Name"` are different values even though their `string_id` is identical.

- [ ] **Step 6.2: Build + run**

```bash
cd /home/tbaldrid/oss/mora
xmake build value_test 2>&1 | tail -3
./build/linux/x86_64/debug/value_test 2>&1 | tail -15
```

Expected: new cases pass alongside the existing value-test cases.

Full suite: `xmake test 2>&1 | tail -3` → `100% tests passed, 0 test(s) failed out of 89` (88 prior + 1 new `test_keyword_token`).

### Task 7: Commit M1

```bash
cd /home/tbaldrid/oss/mora
git add -A
git status --short
git commit -m "$(cat <<'EOF'
mora v3: Value::Kind::Keyword + :ident lexer support

Adds `:ident` keyword literals as a first-class Value kind:

  * TokenKind::Keyword — new lexer token, emitted for `:<ident>`
    (identifier-start char after the colon). Bare `:` and `::`
    behavior unchanged.
  * ast::KeywordLiteral — new AST node with an interned StringId
    value. Added to Expression::data variant.
  * Value::Kind::Keyword — new scalar kind, storing the interned
    name in the existing string_index union slot (no sizeof
    growth). Distinct from Kind::String: :Name and "Name" compare
    unequal even though they share the same StringId.
  * Value::make_keyword + Value::as_keyword.
  * Equality + hash extended for the new kind.
  * Parser, sema, evaluator, and the value-printer all propagate
    keyword literals as scalar values (mirroring StringLiteral).

No semantic meaning yet — keywords are opaque data you can pass
around through rules. The evaluator handles them as pure
pass-through values. Sema doesn't type-check or name-resolve
them. The post-evaluation PatchSet→effect-fact bridge in M2 is
the first real use.

Tests:
  * tests/lexer/test_keyword_token.cpp — 3 cases covering `:ident`
    tokenization, bare `:` (still Colon), and `::` (still DoubleColon).
  * tests/value_test.cpp — 3 new cases for make/equality/hash and
    the String-vs-Keyword distinction invariant.

Part 5 of the v3 rewrite (milestone 1 of 2 for this plan).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Also include the untracked plan doc at `docs/superpowers/plans/2026-04-17-mora-v3-plan-5-keywords-and-effect-facts.md` in this commit (`git add -A` handles it).

Verify: `git log -1 --stat` → expect a file-list covering the lexer/parser/ast/value/sema/eval changes plus the new test + plan doc.

---

## Milestone 2 — PatchSet → effect-fact bridge

Goal: after the evaluator produces a `PatchBuffer`, walk its entries and populate the four output relations (`skyrim/set`, `skyrim/add`, `skyrim/remove`, `skyrim/multiply`). The bridge runs in `cmd_compile` before sink dispatch.

### Task 8: Register `skyrim/multiply`

**Files:**
- Modify: `extensions/skyrim_compile/src/register.cpp`
- Modify: `extensions/skyrim_compile/tests/register_mirrors_schemas_test.cpp`

- [ ] **Step 8.1: Extend the effect-relation loop**

Edit `extensions/skyrim_compile/src/register.cpp`. The existing loop reads:

```cpp
    for (std::string_view effect : {"skyrim/set", "skyrim/add", "skyrim/remove"}) {
```

Change to:

```cpp
    for (std::string_view effect : {"skyrim/set", "skyrim/add", "skyrim/remove", "skyrim/multiply"}) {
```

- [ ] **Step 8.2: Update the test to expect four outputs**

Edit `extensions/skyrim_compile/tests/register_mirrors_schemas_test.cpp`. The `RegistersExactlyThreeOutputRelations` test has a comparison:

```cpp
    EXPECT_EQ(outputs,
              (std::vector<std::string>{"skyrim/add", "skyrim/remove", "skyrim/set"}));
```

Change the test name to `RegistersExactlyFourOutputRelations` and the expected list to:

```cpp
    EXPECT_EQ(outputs,
              (std::vector<std::string>{"skyrim/add", "skyrim/multiply",
                                         "skyrim/remove", "skyrim/set"}));
```

- [ ] **Step 8.3: Build + run**

```bash
cd /home/tbaldrid/oss/mora
xmake build register_mirrors_schemas_test 2>&1 | tail -3
./build/linux/x86_64/debug/register_mirrors_schemas_test 2>&1 | tail -10
```

Expected: both test cases pass.

### Task 9: Implement the bridge function

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 9.1: Add the bridge function**

Near other compile-pipeline helpers in `src/main.cpp` (close to `write_patch_file` or `evaluate_mora_rules`), add:

```cpp
// Walks a PatchBuffer's entries and populates the four Skyrim effect
// relations (skyrim/set, skyrim/add, skyrim/remove, skyrim/multiply)
// with `(FormID, :FieldName, Value)` tuples. Configures the relations
// lazily on first use — the ESP reader's schema.configure_fact_db()
// only covers input relations, not outputs.
static void populate_effect_facts(const mora::PatchBuffer& buf,
                                   mora::FactDB& db,
                                   mora::StringPool& pool) {
    // Pre-intern the four relation names once; interning is idempotent
    // but doing it per-entry would be needless work.
    auto id_set      = pool.intern("skyrim/set");
    auto id_add      = pool.intern("skyrim/add");
    auto id_remove   = pool.intern("skyrim/remove");
    auto id_multiply = pool.intern("skyrim/multiply");

    // Track which relations we've configured this invocation so we only
    // call configure_relation once per relation per run.
    std::unordered_set<uint32_t> configured;
    auto ensure_configured = [&](mora::StringId rel_id) {
        if (configured.insert(rel_id.index).second) {
            db.configure_relation(rel_id, /*arity*/ 3, /*indexed*/ {0});
        }
    };

    for (const auto& e : buf.entries()) {
        mora::StringId rel_id;
        switch (static_cast<mora::FieldOp>(e.op)) {
            case mora::FieldOp::Set:      rel_id = id_set;      break;
            case mora::FieldOp::Add:      rel_id = id_add;      break;
            case mora::FieldOp::Remove:   rel_id = id_remove;   break;
            case mora::FieldOp::Multiply: rel_id = id_multiply; break;
        }
        ensure_configured(rel_id);

        // Decode the PatchEntry's typed value back into a Value.
        mora::Value val;
        switch (static_cast<mora::PatchValueType>(e.value_type)) {
            case mora::PatchValueType::FormID:
                val = mora::Value::make_formid(static_cast<uint32_t>(e.value));
                break;
            case mora::PatchValueType::Int:
                val = mora::Value::make_int(static_cast<int64_t>(e.value));
                break;
            case mora::PatchValueType::Float:
                val = mora::Value::make_float(std::bit_cast<double>(e.value));
                break;
            case mora::PatchValueType::StringIndex:
                val = mora::Value::make_string(
                    mora::StringId{static_cast<uint32_t>(e.value)});
                break;
        }

        auto field_kw = mora::Value::make_keyword(
            pool.intern(field_name(static_cast<mora::FieldId>(e.field_id))));

        db.add_fact(rel_id, mora::Tuple{
            mora::Value::make_formid(e.formid),
            field_kw,
            val,
        });
    }
}
```

Add `#include <bit>` and `#include <unordered_set>` near the other `<>` includes at the top of `src/main.cpp` if they aren't already present.

- [ ] **Step 9.2: Wire into `cmd_compile`**

Inside `cmd_compile`, find the call to `evaluate_mora_rules(...)`. Right after it (before `write_patch_file` and the sink dispatch), add:

```cpp
    populate_effect_facts(patch_buf, db, cr.pool);
```

`patch_buf`, `db`, `cr.pool` are already locals in that scope — see the existing `evaluate_mora_rules(cr, evaluator, patch_buf, string_table, out);` call for the naming.

- [ ] **Step 9.3: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora 2>&1 | tail -3
```

Expected: clean.

### Task 10: Unit-test the bridge

**Files:**
- Create: `tests/cli/test_effect_facts_bridge.cpp`

The root `xmake.lua` discovers `tests/**/test_*.cpp`. Since this test exercises `populate_effect_facts` which lives in `main.cpp`, we either (a) move the function into the `mora_lib` static lib, or (b) declare the function `static` in `main.cpp` and duplicate its body in the test. For Plan 5, promote it into `mora_lib`:

- [ ] **Step 10.1: Move the function body into `src/eval/`**

Create `src/eval/effect_facts.cpp`:

```cpp
#include "mora/eval/effect_facts.h"

#include "mora/emit/patch_table.h"
#include "mora/eval/patch_buffer.h"
#include "mora/eval/patch_set.h"
#include "mora/eval/fact_db.h"
#include "mora/data/value.h"
#include "mora/model/relations.h"  // for mora::FieldId

#include <bit>
#include <unordered_set>

namespace mora {

void populate_effect_facts(const PatchBuffer& buf,
                            FactDB& db,
                            StringPool& pool) {
    auto id_set      = pool.intern("skyrim/set");
    auto id_add      = pool.intern("skyrim/add");
    auto id_remove   = pool.intern("skyrim/remove");
    auto id_multiply = pool.intern("skyrim/multiply");

    std::unordered_set<uint32_t> configured;
    auto ensure_configured = [&](StringId rel_id) {
        if (configured.insert(rel_id.index).second) {
            db.configure_relation(rel_id, /*arity*/ 3, /*indexed*/ {0});
        }
    };

    for (const auto& e : buf.entries()) {
        StringId rel_id;
        switch (static_cast<FieldOp>(e.op)) {
            case FieldOp::Set:      rel_id = id_set;      break;
            case FieldOp::Add:      rel_id = id_add;      break;
            case FieldOp::Remove:   rel_id = id_remove;   break;
            case FieldOp::Multiply: rel_id = id_multiply; break;
        }
        ensure_configured(rel_id);

        Value val;
        switch (static_cast<PatchValueType>(e.value_type)) {
            case PatchValueType::FormID:
                val = Value::make_formid(static_cast<uint32_t>(e.value));
                break;
            case PatchValueType::Int:
                val = Value::make_int(static_cast<int64_t>(e.value));
                break;
            case PatchValueType::Float:
                val = Value::make_float(std::bit_cast<double>(e.value));
                break;
            case PatchValueType::StringIndex:
                val = Value::make_string(
                    StringId{static_cast<uint32_t>(e.value)});
                break;
        }

        auto field_kw = Value::make_keyword(
            pool.intern(field_id_name(static_cast<FieldId>(e.field_id))));

        db.add_fact(rel_id, Tuple{
            Value::make_formid(e.formid),
            field_kw,
            val,
        });
    }
}

} // namespace mora
```

Create `include/mora/eval/effect_facts.h`:

```cpp
#pragma once

#include "mora/core/string_pool.h"

namespace mora {

class FactDB;
class PatchBuffer;

// Walks a PatchBuffer's entries and populates the four Skyrim effect
// relations (skyrim/set, skyrim/add, skyrim/remove, skyrim/multiply)
// with `(FormID, :FieldName, Value)` tuples. Configures the relations
// lazily on first use.
void populate_effect_facts(const PatchBuffer& buf,
                            FactDB& db,
                            StringPool& pool);

} // namespace mora
```

- [ ] **Step 10.2: Add `field_id_name` to the model library**

The existing `field_name` in `src/main.cpp` is a static helper. Its contents need to be accessible from `src/eval/effect_facts.cpp`. Rather than duplicate, move it into the model library.

Search for `field_name` in `src/main.cpp` to confirm it's a static function returning `std::string` (or `const char*`) for a `FieldId`. Then create:

`include/mora/model/field_names.h`:

```cpp
#pragma once

#include "mora/model/relations.h"  // for FieldId
#include <string>

namespace mora {

// Returns the canonical snake-case-ish name for a FieldId, e.g.
// FieldId::GoldValue -> "GoldValue". Used as the keyword payload for
// effect-fact emission and by several CLI display paths.
std::string field_id_name(FieldId id);

} // namespace mora
```

`src/model/field_names.cpp`:

```cpp
#include "mora/model/field_names.h"

namespace mora {

std::string field_id_name(FieldId id) {
    switch (id) {
        case FieldId::Name:            return "Name";
        case FieldId::Damage:          return "Damage";
        case FieldId::ArmorRating:     return "ArmorRating";
        case FieldId::GoldValue:       return "GoldValue";
        // ... full body copied from src/main.cpp's existing field_name()
    }
    return "Unknown";
}

} // namespace mora
```

Copy the complete switch body from `src/main.cpp`'s existing `field_name` (find via `grep -n 'field_name' src/main.cpp`). In `src/main.cpp`, replace the static `field_name` function with a thin wrapper that calls `field_id_name` — or just delete it and update the lone caller to use the new name (preferred).

The `src/model/` directory is already part of `mora_lib`'s source globs (root `xmake.lua:168` has `"src/model/*.cpp"`).

- [ ] **Step 10.3: Wire effect_facts into `mora_lib`**

Edit root `xmake.lua`. The `mora_lib` target's `add_files` list includes globs like `"src/eval/*.cpp"`. Confirm `src/eval/effect_facts.cpp` is picked up by the glob. If the existing glob is `"src/eval/*.cpp"`, no change needed.

- [ ] **Step 10.4: Update `main.cpp` to use the library function**

Edit `src/main.cpp`:
- Delete the in-file `populate_effect_facts` definition (if added in Task 9).
- Add `#include "mora/eval/effect_facts.h"` near the other `mora/eval/*` includes.
- Keep the call `populate_effect_facts(patch_buf, db, cr.pool);` at the same site.

- [ ] **Step 10.5: Write the unit test**

Create `tests/cli/test_effect_facts_bridge.cpp`:

```cpp
#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/emit/patch_table.h"
#include "mora/eval/effect_facts.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_buffer.h"
#include "mora/eval/patch_set.h"
#include "mora/model/relations.h"

#include <gtest/gtest.h>

namespace {

TEST(EffectFactsBridge, OneEntryPerOp) {
    mora::StringPool pool;
    mora::FactDB db(pool);
    mora::PatchBuffer buf;

    // Four entries — one per op. All target the same FormID + FieldId
    // for simplicity; values differ so we can distinguish tuples later.
    auto const form_id = uint32_t{0x000DEFEA};  // arbitrary
    auto const field   = static_cast<uint8_t>(mora::FieldId::GoldValue);

    buf.emit(form_id, field, static_cast<uint8_t>(mora::FieldOp::Set),
             static_cast<uint8_t>(mora::PatchValueType::Int), /*value*/ 100);
    buf.emit(form_id, field, static_cast<uint8_t>(mora::FieldOp::Add),
             static_cast<uint8_t>(mora::PatchValueType::Int), /*value*/ 50);
    buf.emit(form_id, field, static_cast<uint8_t>(mora::FieldOp::Remove),
             static_cast<uint8_t>(mora::PatchValueType::Int), /*value*/ 25);
    buf.emit(form_id, field, static_cast<uint8_t>(mora::FieldOp::Multiply),
             static_cast<uint8_t>(mora::PatchValueType::Int), /*value*/ 2);

    mora::populate_effect_facts(buf, db, pool);

    // Each of the four effect relations should have exactly one tuple.
    for (const char* rel_name : {"skyrim/set", "skyrim/add",
                                  "skyrim/remove", "skyrim/multiply"}) {
        auto rel_id = pool.intern(rel_name);
        ASSERT_EQ(db.fact_count(rel_id), 1U)
            << "relation " << rel_name << " has unexpected count";
    }

    // Spot-check skyrim/set's single tuple: (FormID, :GoldValue, Int 100).
    auto id_set = pool.intern("skyrim/set");
    const auto& set_tuples = db.get_relation(id_set);
    ASSERT_EQ(set_tuples.size(), 1U);
    const auto& t = set_tuples.front();
    ASSERT_EQ(t.size(), 3U);
    EXPECT_EQ(t[0].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(t[0].as_formid(), form_id);
    EXPECT_EQ(t[1].kind(), mora::Value::Kind::Keyword);
    EXPECT_EQ(pool.get(t[1].as_keyword()), "GoldValue");
    EXPECT_EQ(t[2].kind(), mora::Value::Kind::Int);
    EXPECT_EQ(t[2].as_int(), 100);
}

TEST(EffectFactsBridge, EmptyBufferPopulatesNothing) {
    mora::StringPool pool;
    mora::FactDB db(pool);
    mora::PatchBuffer buf;

    mora::populate_effect_facts(buf, db, pool);

    for (const char* rel_name : {"skyrim/set", "skyrim/add",
                                  "skyrim/remove", "skyrim/multiply"}) {
        auto rel_id = pool.intern(rel_name);
        EXPECT_EQ(db.fact_count(rel_id), 0U)
            << "relation " << rel_name << " should be empty";
    }
}

TEST(EffectFactsBridge, StringAndFloatValuesRoundTrip) {
    mora::StringPool pool;
    mora::FactDB db(pool);
    mora::PatchBuffer buf;

    auto form = uint32_t{0x000A0B0C};
    auto field_name_id = static_cast<uint8_t>(mora::FieldId::Name);
    auto field_dmg_id  = static_cast<uint8_t>(mora::FieldId::Damage);

    // String: value carries a StringId.index.
    auto const interned_name = pool.intern("Skeever");
    buf.emit(form, field_name_id,
             static_cast<uint8_t>(mora::FieldOp::Set),
             static_cast<uint8_t>(mora::PatchValueType::StringIndex),
             static_cast<uint64_t>(interned_name.index));

    // Float: value carries a bit-cast double.
    double const damage = 12.5;
    buf.emit(form, field_dmg_id,
             static_cast<uint8_t>(mora::FieldOp::Set),
             static_cast<uint8_t>(mora::PatchValueType::Float),
             std::bit_cast<uint64_t>(damage));

    mora::populate_effect_facts(buf, db, pool);

    auto id_set = pool.intern("skyrim/set");
    const auto& tuples = db.get_relation(id_set);
    ASSERT_EQ(tuples.size(), 2U);

    // Find the string tuple (field keyword == :Name).
    const mora::Tuple* name_tuple = nullptr;
    const mora::Tuple* damage_tuple = nullptr;
    for (const auto& t : tuples) {
        if (pool.get(t[1].as_keyword()) == "Name") name_tuple = &t;
        else if (pool.get(t[1].as_keyword()) == "Damage") damage_tuple = &t;
    }
    ASSERT_NE(name_tuple, nullptr);
    ASSERT_NE(damage_tuple, nullptr);

    EXPECT_EQ((*name_tuple)[2].kind(), mora::Value::Kind::String);
    EXPECT_EQ(pool.get((*name_tuple)[2].as_string()), "Skeever");

    EXPECT_EQ((*damage_tuple)[2].kind(), mora::Value::Kind::Float);
    EXPECT_DOUBLE_EQ((*damage_tuple)[2].as_float(), damage);
}

} // namespace
```

Add `#include <bit>` at the top if the compile fails on `std::bit_cast`.

- [ ] **Step 10.6: Build + run**

```bash
cd /home/tbaldrid/oss/mora
xmake build test_effect_facts_bridge 2>&1 | tail -3
./build/linux/x86_64/debug/test_effect_facts_bridge 2>&1 | tail -15
```

Expected: 3 test cases pass.

If xmake doesn't discover the test, re-run `xmake f -p linux -m debug --yes`.

Full suite: `xmake test 2>&1 | tail -3` → `100% tests passed, 0 test(s) failed out of 90` (89 from M1 + 1 new).

### Task 11: Manual CLI smoke test

- [ ] **Step 11.1: Verify effect facts populate at compile time**

```bash
cd /tmp && rm -rf mora-p5-smoke && mkdir -p mora-p5-smoke && cd mora-p5-smoke
echo "namespace smoke" > empty.mora
/home/tbaldrid/oss/mora/build/linux/x86_64/release/mora compile empty.mora \
    --data-dir /tmp/mora-p5-smoke \
    --sink "parquet.snapshot=./parq?output-only"
echo "exit=$?"
ls -R parq 2>&1 | head -20
```

Expected: exit 0. `parq/skyrim/{set,add,remove,multiply}.parquet` exist. The files are still empty (because `empty.mora` produces no effects), so the count is four — one more than Plan 4's three. This confirms the new `skyrim/multiply` relation is registered.

If the release binary isn't current, rebuild first: `cd /home/tbaldrid/oss/mora && xmake f -p linux -m release --yes && xmake build`.

### Task 12: Commit M2

```bash
cd /home/tbaldrid/oss/mora
git add -A
git status --short
git commit -m "$(cat <<'EOF'
mora v3: bridge PatchBuffer entries into effect-fact relations

After the evaluator produces a PatchBuffer, mora::populate_effect_facts
walks the entries and populates the four Skyrim effect relations:

  * skyrim/set      — FieldOp::Set      entries
  * skyrim/add      — FieldOp::Add      entries
  * skyrim/remove   — FieldOp::Remove   entries
  * skyrim/multiply — FieldOp::Multiply entries (new in M2)

Each PatchEntry(FormID, field_id, op, value_type, value) becomes a
tuple (FormID, :FieldName, Value) in the op-selected relation.
:FieldName is the new Kind::Keyword carrying field_id_name(FieldId),
promoted from src/main.cpp into src/model/field_names.cpp for reuse.
The value is decoded back into its typed Value form (FormID / Int /
Float / String) per value_type.

The bridge runs in cmd_compile after evaluate_mora_rules and before
sink dispatch. FactDB relations are configured lazily on first emit
(arity 3, column 0 indexed on the FormID). The evaluator and
src/emit/ are untouched — PatchBuffer is read, not consumed; the
binary mora_patches.bin output continues in parallel.

register_skyrim now registers the fourth output relation
(skyrim/multiply). test_register_mirrors_schemas updated accordingly
(RegistersExactlyThreeOutputRelations → RegistersExactlyFourOutputRelations).

populate_effect_facts lives in src/eval/effect_facts.cpp, declared in
include/mora/eval/effect_facts.h. Moving it into mora_lib (rather
than keeping it static in main.cpp) is what makes
tests/cli/test_effect_facts_bridge.cpp feasible without main.cpp
symbol linkage gymnastics.

Parquet sink still warns + skips skyrim/set et al. because the value
column is heterogeneous — addressing that is a later plan's job.
For now the effect facts are queryable via FactDB but don't yet
round-trip through parquet. Downstream consumers can still iterate
the in-memory FactDB.

Tests:
  * tests/cli/test_effect_facts_bridge.cpp (new) — 3 cases:
    OneEntryPerOp (checks all four relations populate with correct
    tuple shape), EmptyBufferPopulatesNothing,
    StringAndFloatValuesRoundTrip (covers the non-int decode paths).
  * extensions/skyrim_compile/tests/register_mirrors_schemas_test.cpp
    — updated to expect four output relations.

Part 5 of the v3 rewrite (milestone 2 of 2 for this plan).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Done

After Task 12, Plan 5 is complete. Branch state:
- 14 commits on `mora-v3-foundation` (3 P1 + 3 P2 + 3 P3 + 3 P4 + 2 P5).
- `xmake build` green.
- 90 test binaries pass.
- `mora compile` now populates four effect relations (`skyrim/set`/`add`/`remove`/`multiply`) in the FactDB. These are queryable in-memory but parquet sink still warns + skips them on heterogeneous value columns (addressed in Plan 6).

**Deferred to later plans:**
- Teach parquet sink to emit heterogeneous value columns (List<union>, per-kind split files, or JSON-string fallback). Plan 6.
- Delete `src/emit/` once parquet carries the effect facts. Plan 6+.
- Drop the evaluator's `PatchBuffer` output entirely. Plan 7+.
- Move `data/relations/**/*.yaml` into the Skyrim extension. Later.
- Generalize `tools/gen_relations.py` and delete `src/model/relations_seed.cpp`. Later.

**What's next (Plan 6 — not in this plan):**
Likely candidate: parquet sink handles heterogeneous value columns so the effect facts actually reach disk. With that, `mora_patches.bin` and the parquet snapshot both carry the same information — and Plan 7 can delete `src/emit/`.
