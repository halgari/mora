# F2 — Armor template inheritance residual divergence

**Status:** open, low-priority (affects ~8 forms after the main template-inheritance fix landed in M4).

## Symptom

After the M4 fix added template-keyword inheritance to `compute_post_state` (the test helper walks TNAM chains and unions inherited keywords), most armor divergences resolved. The residual pattern:

```
scenario filter_form_positive [armors] diverges from golden:
  form 0x0008b6a2: missing [], extra [FormId(441313)]
  form 0x0008b6a3: missing [], extra [FormId(441313)]
  form 0x0008b6a4: missing [], extra [FormId(441313)]
  form 0x0008b6a5: missing [], extra [FormId(441313)]
  form 0x0008b6a6: missing [], extra [FormId(441313)]
  form 0x0008b6a7: missing [], extra [FormId(441313)]
  form 0x0008b6a8: missing [], extra [FormId(441313)]
  form 0x0008b6a9: missing [], extra [FormId(441313)]
```

`FormId(441313)` = `0x6BBE1` = `ArmorMaterialImperialStudded` (Skyrim.esm keyword). mora is adding it via template-inheritance; golden shows it absent at runtime.

The 8 forms are consecutive — likely Imperial Studded armor enchanted variants. Same pattern appears across all ARMO scenarios.

## Shortest repro

```bash
export MORA_SKYRIM_DATA=/skyrim-base/Data
cargo test --package mora-kid --test golden golden_filter_form_positive -- --ignored --nocapture 2>&1 | grep '\[armors\]' -A 10
```

## Leading hypothesis

The template-inheritance helper (`flatten_templates` in `crates/mora-kid/tests/golden.rs`) walks the TNAM chain up to depth 16 and unions each ancestor's keywords. For these 8 armor forms, mora's walk picks up `ArmorMaterialImperialStudded` somewhere in the chain.

Skyrim's actual runtime behavior is different — it either:

1. **Stops at a different chain depth.** Skyrim may only inherit from the IMMEDIATE template, not transitively. Our helper walks the full chain.
2. **Inherits selectively.** BGSKeywordForm's `FinishLoadingImpl` or similar may merge only certain keyword "kinds" (armor-type keywords but not armor-material keywords).
3. **The TNAM chain mora walks is wrong.** Maybe `template_armor` points to a RACE or something that isn't actually a keyword-bearing form, and we're reading the wrong field on that record.

## How to investigate

1. **Dump the TNAM chain for form 0x8b6a2.** Add a diagnostic `eprintln!` in `compute_post_state` that prints `fid → template_armor` for this range. Walk manually and find where `0x6BBE1` enters the union.
2. **Check what template_armor value mora reads.** `mora-esp/src/subrecords/form_id_ref.rs` is the parser; single-u32 LE resolve. Verify vs xEdit.
3. **Read CommonLibSSE-NG's `TESObjectARMO::FinishLoadingImpl`** — specifically the keyword-merging block. Confirm the exact criterion Skyrim uses for template inheritance.
4. **Consider limiting inheritance depth to 1.** If Skyrim only inherits from the direct parent, change `MAX_DEPTH` from 16 to 1 and see what passes.

## Relevant files

- `crates/mora-kid/tests/golden.rs::flatten_templates` — the walker
- `crates/mora-esp/src/records/armor.rs` — ARMO parser (TNAM → template_armor)
- `crates/mora-esp/src/subrecords/form_id_ref.rs` — FormID reference parser
- CommonLibSSE-NG: `include/RE/T/TESObjectARMO.h` + its cpp

## Acceptance

The 8 spurious keywords go away in `filter_form_positive [armors]` and related scenarios. `cargo test --test golden golden_filter_form_positive -- --ignored` passes on the `[armors]` check.
