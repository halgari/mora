#pragma once

#include <cstdint>

namespace mora::ext {

// Plugin runtime-index descriptor packed into a uint32_t for exchange
// via LoadCtx::plugin_runtime_index_out. Encoding:
//
//   bit 31:     1 iff ESL / light plugin (FE-space)
//   bits 11..0: raw index value
//                 - ESP/ESM: runtime load-order position (0x00..0xFD)
//                 - ESL:     light-slot number (0x000..0xFFF)
//   bits 30..12: unused (must be zero)
//
// The zero descriptor (`0`) is the canonical "unknown plugin" value —
// globalize_formid returns 0 for it so callers can check for a miss.
// (A real ESP index of 0 packs to `0x00000000`, which collides with
// the miss sentinel, but index 0 is reserved for Skyrim.esm which is
// never the payload of a `0xNNN~Plugin.esp` KID reference — authors
// write those for non-Skyrim master plugins. The KID resolver also
// does a separate presence check before calling globalize_formid, so
// the collision doesn't surface at the API boundary.)
inline constexpr uint32_t kRuntimeIdxEsl = 0x80000000u;

// Compose the final 32-bit runtime FormID from a packed descriptor
// and a local record id taken from a KID `0xNNN~Plugin.ext` ref.
//
//   ESP/ESM: (idx << 24) | (local_id & 0x00FFFFFF)
//   ESL:     0xFE000000 | (slot << 12) | (local_id & 0xFFF)
//
// Matches the encoding used by RuntimeIndexMap::globalize for self-
// references (where the KID author's `~Plugin.ext` IS the target
// plugin, not a master of some other plugin).
//
// Inline so callers in any static archive resolve it locally — avoids
// the "symbol in mora_lib referenced by mora_skyrim_compile linked
// before mora_lib" GNU ld ordering trap for a 5-line helper.
inline uint32_t globalize_formid(uint32_t descriptor, uint32_t local_id) {
    if (descriptor == 0) return 0;
    if ((descriptor & kRuntimeIdxEsl) != 0) {
        uint32_t const slot = descriptor & 0xFFFu;
        return 0xFE000000u | (slot << 12) | (local_id & 0xFFFu);
    }
    uint32_t const idx = descriptor & 0xFFu;
    return (idx << 24) | (local_id & 0x00FFFFFFu);
}

} // namespace mora::ext
