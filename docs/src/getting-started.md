# Getting Started

This guide walks you through installing Mora, writing your first `.mora` file, compiling it to a DLL, and deploying it to Skyrim. By the end you will have a working SKSE plugin that applies patches at game load.

---

## Prerequisites

Before you can use Mora, you need the following installed and working:

**Skyrim Special Edition (Steam)**

Mora targets Skyrim SE. The Game Pass version is not supported because it does not allow SKSE.

**SKSE64**

The Skyrim Script Extender is required to load Mora's output DLL. Download and install it from [skse.silverlock.org](https://skse.silverlock.org). Match the version to your Skyrim build number exactly. SKSE will tell you at launch if there is a mismatch.

**Address Library for SKSE Plugins**

Mora's runtime uses the Address Library to locate memory offsets without hardcoding addresses. Install it from [Nexus Mods (mod 32444)](https://www.nexusmods.com/skyrimspecialedition/mods/32444) with your mod manager.

**Mora CLI**

See the next section.

---

## Installing Mora

Mora is built from source. It is a cross-compiler: it runs on Linux and produces Windows x86-64 DLLs. You do not need a Windows machine.

### Build dependencies

- [xmake](https://xmake.io): build system
- `clang-cl` and `lld-link`: Clang's MSVC-compatible compiler and linker
- Windows SDK headers via [xwin](https://github.com/Jake-Shadle/xwin): provides `windows.h` and MSVC CRT headers on Linux

Once those are on your `PATH`, clone and build:

```bash
# Clone the repository
git clone https://github.com/halgari/mora.git
cd mora

# Build with xmake
xmake build mora

# The binary is at build/linux/x86_64/release/mora
```

Add the binary to your `PATH` or call it by its full path throughout this guide.

!!! tip
    Run `mora --version` after the build to confirm everything linked correctly.

---

## Your First .mora File

Create a file called `balance.mora` anywhere you like:

```mora
namespace my_mod.balance

requires mod("Skyrim.esm")

# All iron weapons get boosted damage
iron_weapons(Weapon):
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialIron)
    => set_damage(Weapon, 99)
```

Here is what each line does:

`namespace my_mod.balance`
: Declares the namespace for this file. Use reverse-domain style to avoid collisions with other mods. Every `.mora` file must start with a namespace declaration.

`requires mod("Skyrim.esm")`
: Declares that this file depends on `Skyrim.esm` being present in your Data directory. Mora will refuse to compile if the listed plugins are missing. Add one `requires` line per dependency.

`# All iron weapons get boosted damage`
: A comment. Comments begin with `#` and run to the end of the line.

`iron_weapons(Weapon):`
: The rule head. `iron_weapons` is the rule name. `Weapon` is a logic variable that will be bound to each form that satisfies the clauses below. The colon opens the rule body.

`weapon(Weapon)`
: A clause. This constrains `Weapon` to forms that are weapons. Mora will only consider records from the weapon category.

`has_keyword(Weapon, :WeapMaterialIron)`
: Another clause. `:WeapMaterialIron` is a form reference. The colon prefix means "look this EditorID up in the loaded plugins." This clause further constrains `Weapon` to only those weapons that carry the iron material keyword.

`=> set_damage(Weapon, 99)`
: The effect. The `=>` separator divides the conditions from the action. `set_damage` sets the base damage field on each matched weapon to `99`. Every `Weapon` that satisfies all clauses above will receive this patch.

Together, the rule reads: "for every form that is a weapon and has the iron keyword, set its damage to 99."

---

## Compiling

Point `mora compile` at your `.mora` file and your Skyrim Data directory:

```bash
mora compile balance.mora --data-dir /path/to/Skyrim Special Edition/Data
```

Replace `/path/to/Skyrim Special Edition/Data` with the actual path on your system. On a typical Steam install on Linux this is something like `~/.steam/steam/steamapps/common/Skyrim Special Edition/Data`.

Expected output:

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

What each line means:

`Parsing 1 files`
: Mora parsed `balance.mora` without syntax errors.

`Resolving 1 rules`
: All form references (like `:WeapMaterialIron`) were found in the loaded plugins.

`Type checking 1 rules`
: All clauses and effects have consistent types. `set_damage` expects a weapon form and an integer, and both match.

`1 static, 0 dynamic`
: The rule was classified as **static**: it depends only on plugin data, not on runtime game state. Static rules are fully evaluated at compile time. Dynamic rules (future feature) would require a runtime evaluator.

`15 plugins, 3 relations → 59522 facts`
: Mora loaded 15 plugins from your Data directory, extracted the `weapon`, `has_keyword`, and `set_damage` relations, and produced 59,522 facts to evaluate against.

`200 patches generated`
: The Datalog engine found 200 weapons that matched all clauses. Each one gets a `set_damage(99)` patch.

`428461 entries (Address Library)`
: Mora loaded the Address Library database. This is used to find the memory offsets where the patches are applied at runtime.

`MoraRuntime.dll (16.5 KB)`
: The compiled output. A 16.5 KB native x86-64 DLL containing the pre-computed patches as direct memory writes. Only the results are baked in; no rules are evaluated at runtime.

`Compiled 1 rules in 389ms`
: Total wall-clock time for the full compilation pipeline.

!!! warning
    If you see `Error: form :WeapMaterialIron not found`, the keyword EditorID is not present in any of your loaded plugins. Double-check the spelling and confirm that `Skyrim.esm` (or the plugin that defines it) is in your Data directory.

---

## Deploying

Copy the output DLL into Skyrim's SKSE plugins folder:

```
Data/SKSE/Plugins/MoraRuntime.dll
```

Create the `Plugins` directory if it does not exist. The full path from your Skyrim root looks like:

```
Skyrim Special Edition/
  Data/
    SKSE/
      Plugins/
        MoraRuntime.dll   ← place it here
```

Then launch the game through SKSE, either `skse64_loader.exe` directly or via your mod manager's SKSE launch option. Launching through the standard `SkyrimSE.exe` will not load SKSE plugins.

!!! tip
    If you use Mod Organizer 2, deploy the DLL into your mod's folder under the same `SKSE/Plugins/` path. MO2 will virtualize it into the Data directory automatically.

---

## Verifying

After loading a save (or starting a new game), Mora writes a log file:

```
Data/SKSE/Plugins/MoraRuntime.log
```

Open it and look for a line like:

```
[Mora] Applied 200 patches in 0.42 ms
```

This confirms the DLL loaded, SKSE called it at the right point, and all 200 patches were written to memory. If the file is missing, SKSE did not load the plugin. Check that SKSE itself is working by looking for `skse64.log` in the same folder.

!!! tip
    The patch count in the log should match the count shown at compile time. If the numbers differ, your Address Library version may not match your Skyrim build. Re-download Address Library for your exact Skyrim version.

---

## Next Steps

You have a working Mora plugin. From here:

- [Language Guide](language-guide.md): learn the full syntax: negation, disjunction, comparison operators, and every built-in relation
- [Examples](examples.md): annotated real-world `.mora` files covering NPCs, factions, level checks, and more
- [How Mora Works](how-mora-works.md): understand why Mora is fast and how it differs from SPID, SkyPatcher, and Synthesis
