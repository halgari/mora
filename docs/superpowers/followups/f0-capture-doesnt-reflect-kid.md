# F0 — Captured goldens don't reflect KID's distribution

**Status:** open, **BLOCKER** for all golden-test scenarios.

## Symptom

Every committed golden post-`xtask capture-kid-goldens` looks like Skyrim's **vanilla + CC + override-merged** keyword state with no KID-applied additions. All 10 scenarios have identical `WeapMaterialIron` counts (203) regardless of scenario — confirming no per-scenario KID distribution is reflected.

## What we've verified

1. **KID loads our scenario INI.** After the F1-branch xtask fix (moved INIs from `Data/SKSE/Plugins/` to `Data/`), KID's log prints:
   ```
   [21:52:08] 1 matching inis found
   [21:52:08]      INI : Data\traits_weapon_all_KID.ini
   [21:52:30] **********************LOOKUP**********************
   ```
2. **Our harness runs AFTER KID in SKSE dispatch order.** KID is handle 1, harness is handle 2; SKSE dispatches kDataLoaded in registration order. Confirmed via `skse64.log`: `sending message type 8 to plugin 1 / sending message type 8 to plugin 2`.
3. **Harness sleeps 20 seconds after kDataLoaded before dumping** (421536a), giving KID ample time to finish. The dump count is identical with or without the sleep.
4. **Per KID source review** (see F1 doc's subagent quotes, and the follow-up analysis):
   - KID's `AddKeywords()` is synchronous inside the kDataLoaded handler — joins all `std::jthread` workers before returning (`include/KeywordData.h:228`).
   - No async deferral. No save-load-time hook for WEAP/ARMO (only BOOK uses `InitItemImpl`; WEAP/ARMO are bulk-distributed via `TESDataHandler::GetFormArray<T>()`).
5. **KID's log shows LOOKUP header but no distribution output.** `Forms::LookupForms()` may be returning false, which skips `AddKeywords()` entirely. Per `Distribute.h:63-66`, KID prints aggregate counts after distribution; the absence of those lines confirms distribution didn't run.

## Leading hypothesis

`Forms::LookupForms()` returns `false` when no rules matched any forms. Our scenario rules SHOULD match (e.g., `filter_keyword_any`'s `*WeapTypeSword` matches by substring against every sword keyword's edid per KID's `ContainsStringFilter`). But either:

1. **KID's per-form iteration in LookupForms() doesn't see `TESDataHandler::GetFormArray<TESObjectWEAP>()` populated yet at kDataLoaded.** The form-array is populated during data-file parsing which should be done before kDataLoaded fires; but there might be AE-specific timing we don't understand.
2. **Our INI has a subtle parse error.** KID's ruleset format might be stricter than `docs/src/kid-ini-grammar.md` suggests. If the rule silently drops (count=0 rules after parse), LookupForms has nothing to match.
3. **A missing KID.ini configuration** (we ship an empty one; maybe KID requires specific config entries to enable WEAP/ARMO distribution).

## How to investigate

1. **Enable KID verbose logging.** KID's `Logger.cpp` uses `spdlog::level::info` hard-coded. Patch the installed KID.dll at runtime? Or compile our own test KID with debug. Easier: read KID's source for `Forms::LookupForms` return path and log each branch manually (by patching the DLL — harder — or by running a KID fork we compile).
2. **Alternative capture methodology: listen for `"KID_KeywordDistributionDone"` ModCallbackEvent.** KID fires this after distribution. Our harness would register a mod-event sink instead of (or in addition to) kDataLoaded. Requires extending `skse-rs` with ModCallbackEvent bindings.
3. **Run a trivially-matching rule and observe.** Write a rule like `WeapMaterialIron = Weapon|+0x00012EB7` (forces match on exactly one well-known form). Check runtime state — does Iron Sword gain WeapMaterialIron? (It already has it vanilla, so hard to tell.) Use a keyword Iron Sword doesn't have, like `WeapTypeWarhammer`, as the target. If it gains WeapTypeWarhammer, distribution is running. If not, KID is silently skipping our rule.
4. **Compare against KID's own example INIs.** KID ships with sample _KID.ini files in its FOMOD. Running one of those as our scenario would tell us if ANY KID configuration works, or if there's a setup-level issue.

## Relevant files

- `crates/mora-golden-harness/src/lib.rs` — the capture harness
- `crates/xtask/src/capture_kid_goldens/staging.rs` — mod-dir staging
- KID logs: `/tmp/prefix/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition GOG/SKSE/po3_KeywordItemDistributor.log`
- KID source: https://github.com/powerof3/Keyword-Item-Distributor

## Acceptance

A golden captured with a known-trivially-matching rule shows the target keyword applied to the target form (i.e., golden ≠ vanilla for that form). Once distribution is reflected in captures, F1 / F2 / F3 become testable again.
