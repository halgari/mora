# Importing from SPID, KID, and SkyPatcher

If you already maintain mods that use SPID, KID, or SkyPatcher, you do not need to rewrite them from scratch. Mora ships with an importer that scans your Data folder and generates equivalent `.mora` rules automatically.

---

## Why Migrate?

Each of the three patchers has its own INI dialect. The formats are simple for small cases but diverge quickly once you need to combine conditions, share logic between rules, or diagnose an unexpected result at runtime. Maintaining the same conceptual rule across three different files and three different syntaxes compounds the problem.

Mora gives you a single language for all three use cases:

**One format instead of three.** Spell distribution, keyword injection, and record patching all live in `.mora` files with the same syntax and the same toolchain.

**Compile-time evaluation.** Mora resolves constant expressions, record lookups, and predicate logic at compile time. The generated DLL does the minimum work at game load, with no interpreter running at runtime.

**Typed error messages.** The compiler tells you which rule is wrong, which predicate failed to resolve, and what type it expected. INI parsers are silent about most mistakes.

**Composable predicates.** You can define a predicate once and reuse it across rules. If ten rules all apply only to playable NPCs, you write `playable_npc` once and reference it ten times rather than repeating the same filter block everywhere.

---

## Using `mora import`

```bash
mora import /path/to/Skyrim/Data
```

The importer walks the directory tree under the path you provide and looks for:

- Files ending in `_DISTR.ini`, treated as SPID distributor configs
- Files ending in `_KID.ini`, treated as KID keyword configs
- Directories named `SkyPatcher` or subdirectories containing a `patches` layout, treated as SkyPatcher configs

For each file it finds, the importer prints the equivalent Mora rules to stdout. Redirect to a file to capture them:

```bash
mora import /path/to/Skyrim/Data > my_patches.mora
```

The output is valid Mora source that you can compile immediately, though you should review it before deploying. See [Migration Workflow](#migration-workflow) and [Limitations](#limitations) below.

---

## SPID to Mora

SPID distributes spells, perks, items, and other records to NPCs by matching against keywords, factions, race, and similar filters.

**Before (`MyMod_DISTR.ini`):**

```ini
Spell = 0x12345~MyMod.esp | ActorTypeNPC
```

**After (Mora equivalent):**

```mora
distribute_spell(NPC):
    npc(NPC)
    has_keyword(NPC, :ActorTypeNPC)
    => add_spell(NPC, MyMod.esp|0x12345)
```

The SPID line reads: "add the spell `0x12345` from `MyMod.esp` to every actor that has the `ActorTypeNPC` keyword." The Mora rule says the same thing explicitly: define a predicate `distribute_spell` that matches any `NPC` actor carrying that keyword, then fire `add_spell`.

The explicit predicate name matters: it becomes the unit of error reporting and the hook point for further composition. If you later want to exclude a specific faction you add one line to the predicate body rather than hunting for the right filter column in the INI.

---

## KID to Mora

KID injects keywords onto records that do not have them in the base game or in the distributing mod.

**Before (`MyMod_KID.ini`):**

```ini
Keyword = 0xABC~MyMod.esp | WeapMaterialIron
```

**After (Mora equivalent):**

```mora
add_kw(Weapon):
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialIron)
    => add_keyword(Weapon, MyMod.esp|0xABC)
```

The KID line reads: "add keyword `0xABC` from `MyMod.esp` to every weapon that already has `WeapMaterialIron`." The Mora rule makes the record type explicit (`weapon(Weapon)`) and names the predicate so you can reference it elsewhere.

---

## SkyPatcher to Mora

SkyPatcher edits record fields directly (damage values, weight, price, speed, and so on) using a section-based INI format.

**Before (`SkyPatcher/weapon/balance.ini`):**

```ini
[Weapon]
filterByKeyword=WeapMaterialIron:damage=99
```

**After (Mora equivalent):**

```mora
iron_damage(Weapon):
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialIron)
    => set_damage(Weapon, 99)
```

SkyPatcher encodes the filter and the assignment on the same line, separated by a colon. Mora separates the match conditions from the action with `=>`, which makes the intent clearer when conditions grow beyond one or two terms.

---

## Migration Workflow

1. **Run the importer.** Point `mora import` at your Data folder and redirect the output to a `.mora` file.

2. **Review the output.** Open the generated file and read through each rule. The importer adds a comment above any rule it could not translate fully. Look for lines starting with `# WARN:` or `# UNSUPPORTED:`.

3. **Save and adjust.** Move the file into your project, rename predicates to match your conventions, and manually fill in any rules the importer left as stubs.

4. **Compile.** Run `mora compile` against your `.mora` file. Fix any errors the compiler reports. Type mismatches and unresolved record references are the most common.

5. **Test with the original patcher disabled.** Disable the SPID/KID/SkyPatcher version of your mod in your load order, enable the compiled Mora DLL, and verify the results in-game. Having both active at the same time will double-apply effects.

---

## Limitations

The importer handles the common cases well but cannot translate everything automatically.

**Complex filter combinations.** SPID supports combining multiple filter columns (race AND faction AND keyword, for example) in a single line. The importer generates the Mora equivalent when the combination is straightforward, but deeply nested NOT and OR logic across multiple columns may be emitted as a stub with a `# UNSUPPORTED:` comment for you to complete by hand.

**Regex string filters.** SPID and SkyPatcher allow filtering by name with a regex. Mora does not have a built-in regex predicate. The importer will emit a comment noting what the original pattern was so you can decide whether to approximate it with discrete keyword checks or handle it another way.

**Leveled list manipulation.** Some SPID configurations distribute to leveled lists rather than directly to actors. The importer will flag these with `# UNSUPPORTED: leveled list target` because Mora's distribution model currently operates on actor instances, not list definitions.

**Conditional probability.** SPID supports a chance column that randomly skips distribution. Mora's rule model is deterministic: if the conditions match, the action fires. Probabilistic distribution is not expressible in the current language and will be flagged.

**SkyPatcher script calls.** SkyPatcher can invoke Papyrus scripts as a side effect. Mora does not generate Papyrus calls and these entries will be skipped with a warning.

Any entry the importer skips is printed to stderr with the original text preserved as a comment in the output file so you do not lose track of what needs manual attention.
