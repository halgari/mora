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

---

# Plan 3 additions — game-interop types

Plan 2 covered the SKSE plugin ABI. Plan 3 adds **partial** Rust
layouts for the Skyrim game types needed to look up a form by FormID
and add a keyword to it. Every struct here is a **minimum viable
layout** — only the fields and methods the `skse-rs-smoke` test
exercises. Each Rust `#[repr(C)]` carries an `// M1-minimal`
comment; future plans extend as real consumers land.

## Address Library bin format (v2, AE)

File: `Data/SKSE/Plugins/versionlib-1.6.1170.0.bin` (or similar).

Source: `CommonLibSSE-NG/include/REL/ID.h`. All fields little-endian.

### Header (prefix)
- `i32` **format version** — must equal `2`
- `i32` **version[0]** (major, e.g. `1`)
- `i32` **version[1]** (minor, e.g. `6`)
- `i32` **version[2]** (patch, e.g. `1170`)
- `i32` **version[3]** (build, e.g. `0`)
- `i32` **name_len** — length of the embedded name string (ignored)
- `name_len` bytes of UTF-8 name
- `i32` **pointer_size** — must equal `8` on AE
- `i32` **address_count** — number of `(id, offset)` pairs

### Delta-encoded pairs

For each pair, read one `u8` **type byte**. Its low nibble encodes
the ID update mode, its high nibble encodes the offset update mode
plus a single "divide offset by pointer_size" bit.

ID low-nibble modes:
```
0  read full u64
1  prev_id + 1
2  prev_id + read_u8
3  prev_id - read_u8
4  prev_id + read_u16
5  prev_id - read_u16
6  read_u16      (absolute u16, zero-extended to u64)
7  read_u32      (absolute u32, zero-extended to u64)
```

Offset high-nibble modes. `hi` is `type_byte >> 4`. Bit 3 of `hi`
(`hi & 8`) is the "scale by pointer_size" flag. Bits 0-2 (`hi & 7`)
select the encoding, with the pre-scaled base taken from
`prev_offset / pointer_size` when the flag is set, else
`prev_offset`:
```
mode 0  read full u64
mode 1  base + 1
mode 2  base + read_u8
mode 3  base - read_u8
mode 4  base + read_u16
mode 5  base - read_u16
mode 6  read_u16
mode 7  read_u32
```
After decoding, if the scale flag was set, multiply the resulting
offset by `pointer_size` (i.e. by 8).

Decoded pairs are stored as a flat sorted array (sorted by id); a
binary search resolves a lookup.

## `TESForm` — partial layout

Source: `CommonLibSSE-NG/include/RE/T/TESForm.h`. Documented size
`0x20`. Plan 3 only needs the `formID` field; the rest is opaque.

| Offset | Size | Field         | Type                           | Mora use   |
|--------|------|---------------|--------------------------------|------------|
| 0x00   | 8    | vtable        | `*const ()`                    | opaque     |
| 0x08   | 8    | source_files  | `*mut ()` (`TESFileArray*`)    | opaque     |
| 0x10   | 4    | form_flags    | `u32`                          | opaque     |
| 0x14   | 4    | **form_id**   | `FormID` (u32)                 | **read**   |
| 0x18   | 2    | in_game_flags | `u16`                          | opaque     |
| 0x1A   | 1    | form_type     | `u8`                           | opaque     |
| 0x1B   | 1    | pad           | `u8`                           | opaque     |
| 0x1C   | 4    | pad           | `u32`                          | opaque     |

Total: `0x20` bytes (size-asserted).

## `BSReadWriteLock` — partial layout

Source: `CommonLibSSE-NG/include/RE/B/BSAtomic.h`. Size `0x08`.

| Offset | Size | Field            | Type               |
|--------|------|------------------|--------------------|
| 0x00   | 4    | `writer_thread`  | `u32` (volatile)   |
| 0x04   | 4    | `lock`           | `u32` (volatile)   |

AE Address Library IDs for the four methods Mora calls:

| Method            | AE ID |
|-------------------|-------|
| `LockForRead`     | 68233 |
| `UnlockForRead`   | 68239 |
| `LockForWrite`    | 68234 |
| `UnlockForWrite`  | 68240 |

Mora only uses `LockForRead` / `UnlockForRead` (read-only lookup in `LookupByID`).

## `BSTHashMap<FormID, *mut TESForm>` — partial layout

Source: `CommonLibSSE-NG/include/RE/B/BSTHashMap.h` (alias of
`BSTScatterTable` with `BSTScatterTableHeapAllocator`).

| Offset | Size | Field         | Rust type                    | Mora use |
|--------|------|---------------|------------------------------|----------|
| 0x00   | 8    | `_pad00`      | `u64`                        | skip     |
| 0x08   | 4    | `_pad08`      | `u32`                        | skip     |
| 0x0C   | 4    | `capacity`    | `u32` (power-of-2)           | read     |
| 0x10   | 4    | `free`        | `u32`                        | skip     |
| 0x14   | 4    | `good`        | `u32`                        | skip     |
| 0x18   | 8    | `sentinel`    | `*const ()` (== `0xDEADBEEF`) | read (chain-end check) |
| 0x20   | 8    | `_alloc_pad`  | `u64`                        | skip     |
| 0x28   | 8    | `entries`     | `*mut HashMapEntry`          | read     |

Total: `0x30` bytes. Alias: `pub type FormMap = BSTHashMap<FormID, *mut TESForm>;`.

### Entry layout

Each entry is 24 bytes:

| Offset | Size | Field   | Rust type             |
|--------|------|---------|-----------------------|
| 0x00   | 4    | `key`   | `u32` (FormID)        |
| 0x04   | 4    | `_pad`  | `u32` (alignment)     |
| 0x08   | 8    | `value` | `*mut TESForm`        |
| 0x10   | 8    | `next`  | `*mut HashMapEntry`   |

Empty slot: `next == null`. End of chain: `next == 0xDEADBEEF as *mut _`.

### Hash + lookup algorithm

1. Hash: `BSCRC32(u32) = crc32(key.to_ne_bytes())`. Bethesda's `BSCRC32<arithmetic_type>` hashes the raw little-endian bytes through the standard CRC-32 (polynomial `0xEDB88320`). Use the `crc32fast` crate.
2. Slot: `idx = hash & (capacity - 1)`.
3. Read `entries[idx]`. If `next == null`, return `None`.
4. Walk the chain: if `entry.key == target_key`, return `Some(entry.value)`. Else `entry = entry.next`. Stop if `entry == sentinel`.

Global map access: `allForms: *mut *mut FormMap` at AE ID `400507` (**double pointer** — the global slot holds a `FormMap*`, so dereference once to get the map, then walk). Lock: `allFormsMapLock: *mut BSReadWriteLock` at AE ID `400517`.

## `TESDataHandler` — partial layout

Source: `CommonLibSSE-NG/include/RE/T/TESDataHandler.h`.
Singleton: static variable at AE ID `400269`.

Plan 3 only needs the singleton-pointer resolution. Field offsets are
deliberately left blank; the smoke test does not read inner fields.
Future plans populate as needed.

## `BGSKeyword` — partial layout

Source: `CommonLibSSE-NG/include/RE/B/BGSKeyword.h`. Documented size
`0x28`.

| Offset | Size | Field              | Type             |
|--------|------|--------------------|------------------|
| 0x00   | 0x20 | (TESForm base)     | inline `TESForm` |
| 0x20   | 8    | `form_editor_id`   | `BSFixedString`  |

Mora only uses the inline `TESForm` base (for the FormID of the
keyword being added). `form_editor_id` is not read.

## `BGSKeywordForm` — partial layout (mixin component)

Source: `CommonLibSSE-NG/include/RE/B/BGSKeywordForm.h`. Documented
size `0x18`.

| Offset | Size | Field          | Rust type                | Mora use          |
|--------|------|----------------|--------------------------|-------------------|
| 0x00   | 8    | `vtable`       | `*const ()`              | opaque (untyped)  |
| 0x08   | 8    | `keywords`     | `*mut *mut BGSKeyword`   | read + replace    |
| 0x10   | 4    | `num_keywords` | `u32`                    | read + replace    |
| 0x14   | 4    | `_pad14`       | `u32`                    | opaque            |

### Invariant (ported from `CommonLibSSE-NG/src/RE/B/BGSKeywordForm.cpp`)

`AddKeyword` logic:
1. If `num_keywords > 0` and the keyword is already present in the
   array (linear scan), return `false` — no mutation.
2. Allocate a new `BGSKeyword**` array of length `num_keywords + 1`
   via `MemoryManager::Allocate`.
3. Memcpy the old array into the new (pointer-wise copy).
4. Write the new keyword pointer at index `num_keywords`.
5. Swap: `old_keywords = self.keywords; self.keywords = new_array;
   self.num_keywords = num_keywords + 1;`
6. `MemoryManager::Deallocate(old_keywords)` iff `old_keywords`
   was non-null.
7. Return `true`.

## `MemoryManager` — partial layout

Source: `CommonLibSSE-NG/include/RE/M/MemoryManager.h`.

Plan 3 does not define a struct for `MemoryManager` — it only needs
three function IDs to call through:

| Function                   | AE ID  | Signature (calling via Rust fn ptr)                                     |
|----------------------------|--------|-------------------------------------------------------------------------|
| `MemoryManager::GetSingleton` | 11141 | `unsafe extern "C" fn() -> *mut MemoryManager`                         |
| `MemoryManager::Allocate`     | 68115 | `unsafe extern "C" fn(mm: *mut MemoryManager, size: usize, alignment: u32, aligned: bool) -> *mut u8` |
| `MemoryManager::Deallocate`   | 68117 | `unsafe extern "C" fn(mm: *mut MemoryManager, ptr: *mut u8, aligned: bool)` |

Mora's callers pass `alignment = 0` and `aligned = false` for raw
heap allocation (matches `RE::malloc(size)` convention in
CommonLibSSE-NG).

## SKSE messaging callback (kDataLoaded) — wiring

The `SKSEMessagingInterface` (Plan 2's ABI) is queried via
`SKSEInterface::query_interface(5)`. Registering a listener:
```
(interface.register_listener)(plugin_handle, null, callback_fn_ptr)
```
`null` as sender means "all senders" (SKSE global broadcast).
The callback receives `*mut SKSEMessage`; Mora filters on
`msg.msg_type == MessageType::DataLoaded as u32` (value `8`).

