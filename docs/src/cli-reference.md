# CLI Reference

Reference for every `mora` subcommand and flag.

---

## Synopsis

```bash
mora <command> [options] [path]
```

Commands:

- [`mora check`](#mora-check) — parse + type-check `.mora` files.
- [`mora compile`](#mora-compile) — full pipeline to `mora_patches.bin`.
- [`mora inspect`](#mora-inspect) — show the patch set without writing output.
- [`mora info`](#mora-info) — project status overview.
- [`mora docs`](#mora-docs) — emit the auto-generated language reference.

---

## Global options

Accepted by every command:

`--no-color`
: Disable colored terminal output. Useful in CI or when piping to a file.

`-v`
: Verbose output.

---

## `mora check`

Type-checks `.mora` files without loading ESP data. Runs in milliseconds —
the right command for editor save-hooks and CI pre-flight.

```bash
mora check my_rules.mora
mora check .              # scan the current directory for .mora files
```

Runs parsing, name resolution, and type checking. Skips all plugin I/O,
so it cannot validate `@EditorID` references (those require a load
order). Use `mora compile` or `mora inspect` for full validation.

---

## `mora compile`

The primary command. Runs the full pipeline: parse, resolve, type check,
phase-classify, evaluate static rules against your ESP data, lower
dynamic rules to operator-DAG bytecode, and write `mora_patches.bin`.

```bash
mora compile my_rules.mora
mora compile my_rules.mora --data-dir "/path/to/Skyrim/Data"
mora compile my_rules.mora --output out/
```

With a detected Skyrim install (Steam on Linux/Windows, Proton prefix, or
portable GOG), `mora compile` runs with zero flags: data-dir, plugins.txt,
and the output directory all resolve automatically, and `mora_patches.bin`
lands directly in the game's `Data/SKSE/Plugins/` where the runtime DLL
picks it up on next launch.

### Options

`--data-dir DIR`
: Path to the Skyrim `Data/` directory. Mora loads `.esm`/`.esp`/`.esl`
files from this location to resolve `@EditorID` references and evaluate
static rules. If omitted, Mora tries to auto-detect a Steam install.

`--output DIR`
: Directory for `mora_patches.bin`. Defaults to `<Data>/SKSE/Plugins/`
when a Skyrim install is detected (same install as `--data-dir`), otherwise
falls back to `MoraCache/` relative to the source file.

`--plugins-txt PATH`
: Path to `Plugins.txt`, the authoritative load-order file. Auto-detected
alongside `Data/`, in the Proton/GOG AppData prefix, or at `%LOCALAPPDATA%\Skyrim Special Edition\Plugins.txt` on Windows.

### Example output

```
  Mora v0.1.0

  [OK] Parsing 1 files
  [OK] Resolving 1 rules
  [OK] Type checking 1 rules
  [OK] 1 static, 0 dynamic
  [OK] 15 plugins, 3 relations -> 59522 facts
  [OK] Evaluating (.mora rules) done
  [OK] 200 patches -> mora_patches.bin (4.1 KB)
  [OK] Wrote MoraCache/mora_patches.bin
```

Output line meanings:

`N static, N dynamic`
: Rule classification. Static rules are fully evaluated now; dynamic
rules are lowered to DAG bytecode for the runtime.

`N plugins, N relations -> N facts`
: ESP loader summary. Relations are loaded lazily — only the ones your
rules touch.

`N patches -> mora_patches.bin`
: Static rules matched this many facts; each one becomes a 16-byte patch
entry.

---

## `mora inspect`

Shows the patch set that `mora compile` would produce, without writing
any output file. Use for debugging, for diffing rule changes, and for
finding conflicts.

```bash
mora inspect my_rules.mora
mora inspect my_rules.mora --conflicts
```

### Options

`--conflicts`
: Report conflicts only (cases where two rules write the same field on
the same FormID with different values or operations).

### Example output

```
  mora inspect — 200 patches (from 1 files)

  0x0001397E:
    damage: set 20

  0x00013980:
    damage: set 20
  ...
```

With `--conflicts`:

```
  0 conflict(s)
```

---

## `mora info`

Project status overview: how many `.mora` files are in scope, how many
rules they define, and — if a `--data-dir` is given — what the current
load order looks like and how many facts it produces.

```bash
mora info
mora info --data-dir "/path/to/Skyrim/Data"
```

### Options

`--data-dir DIR`
: Path to the Skyrim `Data/` directory. With this set, `info` also
reports plugin count and total extracted fact count.

### Example output

```
  Mora v0.1.0

  Mora rules:    11 across 3 files
  Cache status:  MoraRuntime.dll (16.5 KB)
  Data dir:      /path/to/Skyrim/Data
  Plugins found: 47
  Facts loaded:  183291
```

---

## `mora docs`

Prints an auto-generated relation catalog to stdout, derived from the
compiled-in constexpr relation table. Useful as a quick CLI reference for
what the current binary knows about.

```bash
mora docs | less
```

No options.

The hand-curated on-disk `docs/src/relations.md` is regenerated from
`data/relations/**/*.yaml` by `tools/gen_docs.py` and committed to the
repo — don't overwrite it with `mora docs`.

---

## Exit codes

- `0` — success
- `1` — parse / type / compile error, or invalid CLI usage

Diagnostic output goes to stderr for errors and stdout for progress and
info messages.
