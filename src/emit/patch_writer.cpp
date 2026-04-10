#include "mora/emit/patch_writer.h"
#include <cstring>

namespace mora {

// ---------------------------------------------------------------------------
// Little-endian primitive writers
// ---------------------------------------------------------------------------

void PatchWriter::write_u8(std::ostream& out, uint8_t v) {
    out.write(reinterpret_cast<const char*>(&v), 1);
}

void PatchWriter::write_u16(std::ostream& out, uint16_t v) {
    uint8_t buf[2];
    buf[0] = static_cast<uint8_t>(v);
    buf[1] = static_cast<uint8_t>(v >> 8);
    out.write(reinterpret_cast<const char*>(buf), 2);
}

void PatchWriter::write_u32(std::ostream& out, uint32_t v) {
    uint8_t buf[4];
    buf[0] = static_cast<uint8_t>(v);
    buf[1] = static_cast<uint8_t>(v >> 8);
    buf[2] = static_cast<uint8_t>(v >> 16);
    buf[3] = static_cast<uint8_t>(v >> 24);
    out.write(reinterpret_cast<const char*>(buf), 4);
}

void PatchWriter::write_u64(std::ostream& out, uint64_t v) {
    uint8_t buf[8];
    for (int i = 0; i < 8; ++i) {
        buf[i] = static_cast<uint8_t>(v >> (i * 8));
    }
    out.write(reinterpret_cast<const char*>(buf), 8);
}

void PatchWriter::write_i64(std::ostream& out, int64_t v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    write_u64(out, u);
}

void PatchWriter::write_f64(std::ostream& out, double v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    write_u64(out, u);
}

// ---------------------------------------------------------------------------
// Value encoding:
//   value_type: 0=int, 1=float, 2=string, 3=formid, 4=bool
// ---------------------------------------------------------------------------

void PatchWriter::write_value(std::ostream& out, const Value& v) {
    switch (v.kind()) {
        case Value::Kind::Int:
            write_u8(out, 0);
            write_i64(out, v.as_int());
            break;
        case Value::Kind::Float:
            write_u8(out, 1);
            write_f64(out, v.as_float());
            break;
        case Value::Kind::String: {
            write_u8(out, 2);
            std::string_view s = pool_.get(v.as_string());
            write_u16(out, static_cast<uint16_t>(s.size()));
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
            break;
        }
        case Value::Kind::FormID:
            write_u8(out, 3);
            write_u32(out, v.as_formid());
            break;
        case Value::Kind::Bool:
            write_u8(out, 4);
            write_u8(out, v.as_bool() ? 1 : 0);
            break;
        case Value::Kind::Var:
            // Var should not appear in a resolved patch; write as int 0 as fallback.
            write_u8(out, 0);
            write_i64(out, 0);
            break;
    }
}

// ---------------------------------------------------------------------------
// Top-level write
// ---------------------------------------------------------------------------

void PatchWriter::write(std::ostream& out, const ResolvedPatchSet& patches,
                        uint64_t load_order_hash, uint64_t source_hash) {
    // Header
    out.write("MORA", 4);
    write_u16(out, 1);                                         // version
    write_u64(out, load_order_hash);
    write_u64(out, source_hash);

    auto sorted = patches.all_patches_sorted();
    write_u32(out, static_cast<uint32_t>(sorted.size()));      // patch_count

    for (const auto& rp : sorted) {
        write_u32(out, rp.target_formid);
        write_u16(out, static_cast<uint16_t>(rp.fields.size()));
        for (const auto& fp : rp.fields) {
            write_u16(out, static_cast<uint16_t>(fp.field));
            write_u8(out,  static_cast<uint8_t>(fp.op));
            write_value(out, fp.value);
        }
    }
}

} // namespace mora
