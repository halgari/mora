# CLI Reference

Quick reference for every `mora` command and option.

---

## Synopsis

```bash
mora <command> [options] [path]
```

---

## Global Options

These options are accepted by all commands.

`--no-color`
: Disable colored terminal output. Useful when piping output to a file or when running in a CI environment.

`-v`
: Verbose output. Prints additional detail during each pipeline stage.

---

## Commands

### mora compile

Compiles one or more `.mora` files to a native SKSE DLL. This is the primary command — it runs the full pipeline: parsing, type checking, plugin loading, Datalog evaluation, code generation, and linking.

```bash
mora compile my_rules.mora --data-dir /path/to/Skyrim/Data
mora compile my_rules.mora --data-dir /path/to/Skyrim/Data --output /path/to/output
```

**Options**

`--data-dir DIR`
: Path to the Skyrim `Data/` directory. Mora loads `.esm` and `.esp` files from this location to resolve form references and evaluate rules against real plugin data. Required for a successful compilation — without it, form lookups cannot be resolved.

`--output DIR`
: Directory where the compiled DLL is written. Defaults to `MoraCache/` in the current working directory.

`-v`
: Print per-stage timing and intermediate counts.

`--no-color`
: Disable colored output.

**Example output**

```
✓ Parsing 1 files
✓ Resolving 1 rules
✓ Type checking 1 rules
✓ 1 static, 0 dynamic
✓ 15 plugins, 3 relations → 59522 facts
✓ 200 patches generated
✓ 428461 entries (Address Library)
✓ MoraRuntime.dll (16.5 KB)
✓ Compiled 1 rules in 389ms
```

**What each line means**

`Parsing N files`
: The listed `.mora` files were parsed without syntax errors.

`Resolving N rules`
: All form references (e.g. `:WeapMaterialIron`) were found in the loaded plugins.

`Type checking N rules`
: All clauses and effects have consistent types.

`N static, N dynamic`
: Rules classified as **static** depend only on plugin data and are fully evaluated at compile time. **Dynamic** rules (future feature) require runtime evaluation.

`N plugins, N relations → N facts`
: Number of plugins loaded from `--data-dir`, the distinct relations extracted from them, and the total fact count passed to the Datalog engine.

`N patches generated`
: Forms that matched all rule clauses. Each receives the rule's effect.

`N entries (Address Library)`
: Size of the Address Library database loaded for offset resolution.

`MoraRuntime.dll (N KB)`
: The compiled output written to `--output`.

`Compiled N rules in Nms`
: Total wall-clock time for the full pipeline.

!!! tip
    On a typical Steam install on Linux the Data directory is at  
    `~/.steam/steam/steamapps/common/Skyrim Special Edition/Data`.

---

### mora check

Type-checks `.mora` files without compiling. Does not load ESP data, so it runs fast. Use this to catch syntax and type errors during development without waiting for plugin loading.

```bash
mora check my_rules.mora
```

**Options**

`-v`
: Print per-file diagnostic counts.

`--no-color`
: Disable colored output.

**Example output**

```
✓ Parsing 1 files
✓ Resolving 1 rules
✓ Type checking 1 rules
✓ No errors
```

If there are errors:

```
✗ Type checking 1 rules
  my_rules.mora:8: set_damage expects (Weapon, Int) — got (Armor, Int)
```

!!! tip
    `mora check` is the right command to run in a save-on-write hook or editor integration. It completes in milliseconds even for large rule files because it skips all plugin I/O.

---

### mora inspect

Shows the patch set that would be generated, without building a DLL. Useful for verifying that rules produce the expected patches, and for debugging conflicts between rules.

```bash
mora inspect my_rules.mora --data-dir /path/to/Skyrim/Data
mora inspect my_rules.mora --data-dir /path/to/Skyrim/Data --conflicts
```

**Options**

`--data-dir DIR`
: Path to the Skyrim `Data/` directory. Required — inspect evaluates rules against real plugin data.

`--conflicts`
: Show only forms where two or more rules emit contradictory effects on the same field. Hides all non-conflicting patches.

`-v`
: Print full fact tables in addition to the patch list.

`--no-color`
: Disable colored output.

**Example output (default)**

```
Patch set — 200 patches across 1 rule

[my_mod.balance] iron_weapons
  IronSword         set_damage(99)
  IronGreatsword    set_damage(99)
  IronDagger        set_damage(99)
  ... (197 more)
```

**Example output (--conflicts)**

```
Conflicts — 2 forms with contradictory patches

  IronSword
    my_mod.balance::iron_weapons   set_damage(99)
    my_mod.nerfs::iron_nerfs       set_damage(12)

  IronGreatsword
    my_mod.balance::iron_weapons   set_damage(99)
    my_mod.nerfs::iron_nerfs       set_damage(12)
```

!!! tip
    Run `mora inspect --conflicts` before deploying to check that rules from multiple `.mora` files do not write opposing values to the same form field.

---

### mora info

Prints a project status overview: how many `.mora` files are found, how many rules they define, how many plugins are loaded, and how many facts the current load order produces.

```bash
mora info --data-dir /path/to/Skyrim/Data
```

**Options**

`--data-dir DIR`
: Path to the Skyrim `Data/` directory. Mora loads plugins from this location to report fact and plugin counts.

`-v`
: Break down fact counts per relation.

`--no-color`
: Disable colored output.

**Example output**

```
Project overview

  Source files    3
  Rules           11
  Namespaces      3

  Plugins loaded  47
  Relations       6
  Facts           183291

  Static rules    11
  Dynamic rules   0
```

---

### mora import

Scans a directory for SPID, KID, and SkyPatcher INI files and prints them as equivalent Mora rules. Nothing is written to disk — output is printed to stdout. Use this as a starting point when migrating an existing INI-based setup to Mora.

```bash
mora import /path/to/Skyrim/Data
```

**Options**

`-v`
: Print each INI file as it is scanned and report how many entries were converted.

`--no-color`
: Disable colored output.

**Example output**

```
-- Imported from SPID: SpellDistributor_DISTR.ini --

namespace imported.SpellDistributor

requires mod("Skyrim.esm")

# Spell=0x12FD2~Skyrim.esm|ActorTypeNPC
spelldistr_0(NPC):
    npc(NPC)
    has_keyword(NPC, :ActorTypeNPC)
    => add_spell(NPC, 0x12FD2~Skyrim.esm)

-- Imported from KID: ArmorKeywords_DISTR.ini --

namespace imported.ArmorKeywords

requires mod("Skyrim.esm")
requires mod("Dawnguard.esm")

# Item=ArmorBoots|0x3E99C~Dawnguard.esm
armorkeywords_0(Armor):
    armor(Armor)
    form_id(Armor, 0x3E99C~Dawnguard.esm)
    => add_keyword(Armor, :ArmorBoots)
```

!!! tip
    The imported rules are a mechanical translation. Review them before use — some SPID/KID patterns have runtime conditions (level checks, chance rolls) that Mora cannot yet express, and those lines will be flagged with a comment.
