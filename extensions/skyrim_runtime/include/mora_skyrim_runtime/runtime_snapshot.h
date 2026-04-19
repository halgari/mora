#pragma once

#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/fact_db.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mora_skyrim_runtime {

// Runtime-snapshot binary format. Emitted by the compile-time host as a
// sidecar to the parquet output (via the skyrim_runtime.snapshot sink);
// read by MoraRuntime.dll at DataLoaded. Flat, mmap-friendly, no Arrow
// dependency on the runtime side.
//
// Layout:
//   Header (16 bytes):
//     u32 magic            = 0x4d525253  ("SRRM" little-endian → "MRRS" forward)
//     u32 version          = 1
//     u32 num_rows         = count of Row entries
//     u32 string_pool_size = size of string pool in bytes
//
//   Row array (num_rows × 24 bytes):
//     u8  op              (0=Set, 1=Add, 2=Remove, 3=Multiply)
//     u8  value_kind      (see ValueKind below; matches mora::Value::Kind ordering
//                          where it makes sense)
//     u8  _pad[2]
//     u32 target_formid
//     u32 field_string_offset  — byte offset into the string pool of the field
//                                 keyword (e.g. "GoldValue", "Damage", "Keyword")
//     u64 value_payload        — interpretation depends on value_kind:
//                                  Int:     sign-extended int64
//                                  Float:   bit-cast double
//                                  FormID:  truncate-to-u32
//                                  String:  u32 offset into string pool
//                                  Keyword: u32 offset into string pool
//                                  Bool:    0 or 1
//
//   String pool (string_pool_size bytes):
//     Each string: u32 length + raw bytes (NOT null-terminated).

enum class Op : uint8_t {
    Set      = 0,
    Add      = 1,
    Remove   = 2,
    Multiply = 3,
};

enum class ValueKind : uint8_t {
    Int     = 0,
    Float   = 1,
    FormID  = 2,
    String  = 3,
    Keyword = 4,
    Bool    = 5,
};

constexpr uint32_t kSnapshotMagic   = 0x4d525253u;  // 'SRRM' LE → reads 'MRRS' in-file
constexpr uint32_t kSnapshotVersion = 1u;

struct SnapshotHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t num_rows;
    uint32_t string_pool_size;
};
static_assert(sizeof(SnapshotHeader) == 16, "SnapshotHeader must be 16 bytes");

struct SnapshotRow {
    uint8_t  op;
    uint8_t  value_kind;
    uint8_t  _pad[2];
    uint32_t target_formid;
    uint32_t field_string_offset;
    uint64_t value_payload;
};
static_assert(sizeof(SnapshotRow) == 24, "SnapshotRow must be 24 bytes");

// In-memory snapshot, loaded from disk.
struct LoadedSnapshot {
    std::vector<SnapshotRow> rows;
    // String pool bytes, indexed by field_string_offset / value-string offset.
    // Each string entry: u32 length + raw bytes.
    std::vector<uint8_t>     string_pool;

    // Read a string by its offset in the pool. Returns a view into
    // string_pool — stable as long as the LoadedSnapshot lives.
    std::string_view get_string(uint32_t offset) const;
};

// Write a snapshot to disk. Reads the four effect relations
// (skyrim/{set,add,remove,multiply}) from `db`, converts to the flat
// binary format, and writes to `out_path`. Returns true on success;
// on failure appends a diagnostic to `diags`.
//
// The caller owns `pool` + `db`; this function only reads from them.
bool write_snapshot(const std::filesystem::path& out_path,
                     const mora::FactDB&           db,
                     const mora::StringPool&       pool,
                     mora::DiagBag&                diags);

// Read a snapshot from disk. On parse error appends a diagnostic and
// returns std::nullopt. No Arrow / no filesystem dep beyond basic
// ifstream — safe for use inside the SKSE DLL.
std::optional<LoadedSnapshot>
read_snapshot(const std::filesystem::path& path,
               mora::DiagBag&               diags);

class GameAPI;  // forward-declared in game_api.h

// Iterate the snapshot rows and dispatch each through the GameAPI.
// `pool` is used to intern field keyword / string values before
// passing them to the GameAPI as mora::Value instances.
size_t apply_snapshot(const LoadedSnapshot& snap,
                       GameAPI&              api,
                       mora::StringPool&     pool);

} // namespace mora_skyrim_runtime
