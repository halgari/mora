# F1 — ANY-bucket filter semantics diverge from KID

**Status:** open, highest-priority M4 follow-up.

## Symptom

Scenario `filter_keyword_any` with rule:

```ini
WeapMaterialIron = Weapon|*WeapTypeSword,*WeapTypeWarhammer
```

mora-kid matches a different set of weapons than real KID at runtime.

Golden post-state has `WeapMaterialIron = 0x0001E718` on ~203 weapons (from captured live Skyrim + real KID). The set includes weapons whose WEAP record carries:

- `WeapTypeSword` (0x1E711) ✓ — matches our expectation
- `WeapTypeDagger` (0x1E712), `WeapTypeWarAxe` (0x1E713), `WeapTypeGreatsword` (0x6D931), etc. — **golden has WeapMaterialIron on these too**, even though the rule only lists Sword + Warhammer.

mora-kid matches only the Sword/Warhammer set, misses the broader set KID uses, AND adds WeapMaterialIron to some weapons KID doesn't (e.g., form `0x000135b8` which has WeapTypeSword — KID skipped, mora added).

Net: mora's `filter_keyword_any/weapons.jsonl` diff shows both missing AND extra entries vs golden — the sets partially overlap but differ.

## Shortest repro

```bash
export MORA_SKYRIM_DATA=/skyrim-base/Data   # on runner
cargo test --package mora-kid --test golden golden_filter_keyword_any -- --ignored --nocapture 2>&1 | head -40
```

Expected: per-form diff output showing ~100 forms with `missing [0x0001e718]` and ~50 forms with `extra [0x0001e718]`.

## Leading hypothesis

Looking at mora-kid's current `parse_filter_field` (crates/mora-kid/src/filter.rs:12):

```rust
pub fn parse_filter_field(s: &str) -> FilterBuckets {
    let mut buckets = FilterBuckets::default();
    for t in s.split(',') {
        let t = t.trim();
        if let Some(rest) = t.strip_prefix('+') {
            buckets.all.push(/* parsed */);
        } else if let Some(rest) = t.strip_prefix('-') {
            buckets.not.push(Reference::parse(rest.trim()));
        } else if let Some(rest) = t.strip_prefix('*') {
            buckets.any.push(rest.trim().to_string());
        } else {
            buckets.match_.push(Reference::parse(t));
        }
    }
    buckets
}
```

Each comma-separated token gets independent classification. So `*WeapTypeSword,*WeapTypeWarhammer` → `buckets.any = [WeapTypeSword, WeapTypeWarhammer]`. That's one ANY bucket with two candidates.

`evaluate_with_any` (crates/mora-kid/src/filter.rs:96-115) then checks `any_matched = buckets.any.iter().any(|sub| form_has_keyword_substring(sub))` — matches if form has ANY of the listed keywords (either/or).

This SHOULD match exactly what KID does, per our `docs/src/kid-ini-grammar.md`:

> `*Kw` — Ignore bucket (partial match, at least one must be present).

But the observed golden set doesn't match. Possibilities:

1. **KID evaluates ANY differently** — maybe it matches on substring of keyword editor-IDs, not exact equality. `*WeapTypeSword` substring-matches `WeapTypeGreatsword`. This would explain why greatswords also match.
2. **KID has a bug (or feature) where `*Kw1,*Kw2` is treated as AND, not OR** — unlikely but possible.
3. **Our captured goldens are stale** — if KID was run with a different INI than what we staged, the goldens reflect that INI. Re-verify by inspecting the staging dir on the runner.
4. **KID falls back to a default rule set** when our INI parses wrong. If `*WeapTypeSword,*WeapTypeWarhammer` syntax is rejected by KID (e.g., wrong pipe count) but KID has a fallback that applies WeapMaterialIron to all iron-family weapons via vanilla heuristics, that'd explain the broad golden match.

## How to investigate

1. **Verify the golden INI actually got staged.** Re-run the capture for `filter_keyword_any` with `--scenario filter_keyword_any` and inspect `/root/.tmpXXXX/Data/SKSE/Plugins/filter_keyword_any_KID.ini` (tempdir path is in the xtask's `eprintln!`). Confirm KID saw the file we meant.
2. **Check KID's log.** `/tmp/prefix/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition GOG/SKSE/po3_KeywordItemDistributor.log` will have KID's own evaluation trace. Look for `WeapMaterialIron` additions — KID logs each.
3. **Read KID's filter source.** po3/Keyword-Item-Distributor on GitHub, specifically `Distribute.cpp` / `Filter.cpp`. Confirm the exact semantics of `*Kw1,*Kw2`.
4. **Check mora-kid's substring match in ANY.** `evaluate_with_any` calls `form_has_keyword_substring` (line 101) — substring match, not equality. That's probably the right semantic but worth confirming matches KID's code.

## Relevant files

- `crates/mora-kid/src/filter.rs` — the parser + evaluator
- `crates/mora-kid/tests/filter_activation.rs` — existing unit tests
- `docs/src/kid-ini-grammar.md` — our grammar reference (may be wrong)
- `tests/golden-data/kid-inis/filter_keyword_any/filter_keyword_any_KID.ini`
- `tests/golden-data/expected/filter_keyword_any/weapons.jsonl` — the ground-truth capture

## Acceptance

`cargo test --package mora-kid --test golden golden_filter_keyword_any -- --ignored` passes. Unignore the test. Likely fixes several other scenarios as a side-effect (filter_keyword_not, filter_edid_substring, etc. all depend on the same filter evaluator).
