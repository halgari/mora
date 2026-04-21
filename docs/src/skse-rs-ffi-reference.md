# skse-rs ABI Reference

This doc is the source of truth for the `#[repr(C)]` layouts in
`skse-rs`. Every struct here has a citation to its origin in
CommonLibSSE-NG (the C++ SKSE framework skse-rs intentionally does
*not* depend on — we mirror layouts from CommonLibSSE-NG's public
headers to stay wire-compatible with SKSE).

All offsets are in bytes from the struct start. All fields are
little-endian. Total sizes are asserted at compile time via
`const _: () = assert!(size_of::<T>() == N);`.

## `PluginVersionData` — the plugin-info data export

Data export symbol: `SKSEPlugin_Version`.
Source: `CommonLibSSE-NG/include/SKSE/Interfaces.h` (the
`SKSEPluginInfo` macro region, `static_assert(sizeof(PluginVersionData) == 0x350)`).

Total size: `0x350` bytes.

| Offset | Size | Field name            | Type          | Meaning                                                                |
|--------|------|-----------------------|---------------|------------------------------------------------------------------------|
| 0x000  | 4    | `data_version`        | `u32`         | must equal `1`                                                         |
| 0x004  | 4    | `plugin_version`      | `u32`         | packed `(major<<24)\|(minor<<16)\|(patch<<4)\|build` per `REL::Version` |
| 0x008  | 256  | `plugin_name`         | `[u8; 256]`   | NUL-terminated UTF-8 plugin identifier                                 |
| 0x108  | 256  | `author`              | `[u8; 256]`   | NUL-terminated                                                         |
| 0x208  | 252  | `support_email`       | `[u8; 252]`   | NUL-terminated                                                         |
| 0x304  | 4    | flags group A         | `u32`         | bit 0 = `no_struct_use`                                                |
| 0x308  | 4    | flags group B         | `u32`         | bit 0 = `address_library`, bit 1 = `sig_scanning`, bit 2 = `structs_post629` |
| 0x30C  | 64   | `compatible_versions` | `[u32; 16]`   | packed runtime versions; zero-terminated list                          |
| 0x34C  | 4    | `xse_minimum`         | `u32`         | minimum SKSE version; zero = any                                       |

**Mora's defaults for v0.1:**
- `data_version = 1`
- `plugin_name = "MoraRuntime"` (or `"MoraTestHarness"` for the harness build; `skse-rs-smoke` uses `"SkseRsSmoke"`)
- flags group B bit 0 (`address_library`) = `1` — so compatibility is checked against the address library rather than an exact runtime match
- `compatible_versions = [0, 0, ...]` — an all-zero list means "any version (address library handles compatibility)"

## `PluginInfo` — the info struct passed to `SKSEPlugin_Query`

Source: `CommonLibSSE-NG/include/SKSE/Interfaces.h`, `SKSEPlugin_Query` signature.

Total size: `0x18` bytes (24). Breakdown:

- `info_version` at offset `0x00` (4 bytes)
- 4 bytes of natural padding so the pointer is 8-byte aligned
- `name` at offset `0x08` (8 bytes)
- `version` at offset `0x10` (4 bytes)
- 4 bytes of trailing padding so the struct itself is 8-byte aligned (for arrays)
- Total: 4 + 4 + 8 + 4 + 4 = 24 = `0x18`

| Offset | Size | Field          | Type            | Meaning                                     |
|--------|------|----------------|-----------------|---------------------------------------------|
| 0x00   | 4    | `info_version` | `u32`           | must be `1`                                 |
| 0x08   | 8    | `name`         | `*const c_char` | pointer to a static NUL-terminated string   |
| 0x10   | 4    | `version`      | `u32`           | packed plugin version (same as above)       |

## `SKSEInterface` — the first argument to `SKSEPlugin_Load`

Source: `CommonLibSSE-NG/include/SKSE/Impl/Stubs.h`.

Total size (function pointers on x64 are 8 bytes each, `u32` fields are 4 bytes with 4 bytes of padding before the first pointer):

| Offset | Size | Field                 | Type              |
|--------|------|-----------------------|-------------------|
| 0x00   | 4    | `skse_version`        | `u32`             |
| 0x04   | 4    | `runtime_version`     | `u32`             |
| 0x08   | 4    | `editor_version`      | `u32`             |
| 0x0C   | 4    | `is_editor`           | `u32` (bool as u32) |
| 0x10   | 8    | `query_interface`     | fn(u32) -> *mut c_void |
| 0x18   | 8    | `get_plugin_handle`   | fn() -> u32       |
| 0x20   | 8    | `get_release_index`   | fn() -> u32       |
| 0x28   | 8    | `get_plugin_info`     | fn(*const c_char) -> *const c_void |

Total: `0x30` bytes.

## `SKSEMessagingInterface` — queried via `query_interface(5)` at load time

Source: `CommonLibSSE-NG/include/SKSE/Impl/Stubs.h`.

`kMessaging` interface ID: `5` (from `LoadInterface` enum in `Interfaces.h`).

| Offset | Size | Field                    | Type |
|--------|------|--------------------------|------|
| 0x00   | 4    | `interface_version`      | `u32` (== 2 at AE) |
| 0x08   | 8    | `register_listener`      | fn(handle: u32, sender: *const c_char, callback: *mut c_void) -> bool |
| 0x10   | 8    | `dispatch`               | fn(handle: u32, msg_type: u32, data: *mut c_void, data_len: u32, receiver: *const c_char) -> bool |
| 0x18   | 8    | `get_event_dispatcher`   | fn(id: u32) -> *mut c_void |

Total: `0x20` bytes.

## Message types

From `CommonLibSSE-NG/include/SKSE/Interfaces.h`, `MessagingInterface::kXxx`:

| Name             | Value |
|------------------|-------|
| `kPostLoad`      | 0     |
| `kPostPostLoad`  | 1     |
| `kPreLoadGame`   | 2     |
| `kPostLoadGame`  | 3     |
| `kSaveGame`      | 4     |
| `kDeleteGame`    | 5     |
| `kInputLoaded`   | 6     |
| `kNewGame`       | 7     |
| `kDataLoaded`    | 8     |

Mora's runtime registers for **`kDataLoaded` (8)**.

## Version packing (`REL::Version`)

Source: `CommonLibSSE-NG/include/REL/Version.h`.

```
packed = ((major & 0xFF) << 24)
       | ((minor & 0xFF) << 16)
       | ((patch & 0xFFF) << 4)
       | (build & 0xF)
```

Plugin version example: Mora v0.1.0 → `(0 << 24) | (1 << 16) | (0 << 4) | 0 = 0x0001_0000`.

Runtime version example: Skyrim SE 1.6.1170 → `(1 << 24) | (6 << 16) | (1170 << 4) | 0 = 0x0106_4920`.

For Mora we don't compare runtime versions directly — we set `address_library = true` and `compatible_versions = [0]`, which signals to SKSE: "load me on any version; I use the Address Library to resolve my own offsets." Plan 3 implements the Address Library parser.
