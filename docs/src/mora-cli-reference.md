# `mora` CLI Reference

## Commands

### `mora compile`

Discover KID INIs in a Skyrim Data directory, parse rules against the
active plugins, produce `mora_patches.bin`.

**Usage:**

```
mora compile --data-dir <PATH> --plugins-txt <PATH> [--output <PATH>] [--dry-run] [--verbose]
```

**Flags:**

| Flag              | Required | Description                                                          |
|-------------------|----------|----------------------------------------------------------------------|
| `--data-dir`      | yes      | Skyrim's `Data/` directory (contains `*.esm`, `*.esp`, KID INIs).    |
| `--plugins-txt`   | yes      | Path to `plugins.txt` defining active load order.                    |
| `--output`        | no       | Output path. Default: `<data-dir>/SKSE/Plugins/mora_patches.bin`.    |
| `--dry-run`       | no       | Run the full pipeline but don't write the output file.               |
| `--verbose`       | no       | Enable debug-level logging.                                          |

**Exit codes:**

| Code | Meaning                                |
|------|----------------------------------------|
| 0    | Success                                |
| 1    | User-facing error (bad path, parse error, etc.) |
| 2    | Internal error (panic, bug)            |

**Typical output (stderr):**

```
Mora v0.1.0
[OK] Loaded plugins.txt: 247 active plugins
[OK] Opened EspWorld: 1,247,301 records across 247 plugins
[OK] Discovered 34 _KID.ini files
[OK] Parsed 1,892 rules
[OK] Distributed keywords: 83,428 patches emitted
[OK] Wrote mora_patches.bin (1.1 MB)
  load_order_hash: 0xdeadbeefcafef00d
  candidates considered: 247,103
  rejected by filter:    163,615
  rejected by chance:    60
  rules evaluated:       1,892
Total: 623ms
```

### Future commands (not implemented yet)

- `mora check` — parse INIs + validate without writing output
- `mora info` — dump load order + rule counts for debugging

## `mora_patches.bin` format

postcard-serialized `mora_core::PatchFile`. See
`docs/src/mora-core-reference.md` for the struct layout.

The `load_order_hash` field is a 64-bit blake3 digest over the
canonical load-order representation (plugin filenames lowercased,
joined by NUL, including their master lists). Runtime verifies this
on load; mismatch → refuse to apply.
