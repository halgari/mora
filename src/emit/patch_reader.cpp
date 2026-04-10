#include "mora/emit/patch_reader.h"
#include <cstring>

namespace mora {

// ---------------------------------------------------------------------------
// Little-endian primitive readers — return false on EOF/error
// ---------------------------------------------------------------------------

bool PatchReader::read_u8(std::istream& in, uint8_t& v) {
    char c;
    if (!in.get(c)) return false;
    v = static_cast<uint8_t>(c);
    return true;
}

bool PatchReader::read_u16(std::istream& in, uint16_t& v) {
    uint8_t buf[2];
    if (!in.read(reinterpret_cast<char*>(buf), 2)) return false;
    v = static_cast<uint16_t>(buf[0])
      | (static_cast<uint16_t>(buf[1]) << 8);
    return true;
}

bool PatchReader::read_u32(std::istream& in, uint32_t& v) {
    uint8_t buf[4];
    if (!in.read(reinterpret_cast<char*>(buf), 4)) return false;
    v = static_cast<uint32_t>(buf[0])
      | (static_cast<uint32_t>(buf[1]) << 8)
      | (static_cast<uint32_t>(buf[2]) << 16)
      | (static_cast<uint32_t>(buf[3]) << 24);
    return true;
}

bool PatchReader::read_u64(std::istream& in, uint64_t& v) {
    uint8_t buf[8];
    if (!in.read(reinterpret_cast<char*>(buf), 8)) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<uint64_t>(buf[i]) << (i * 8);
    }
    return true;
}

bool PatchReader::read_i64(std::istream& in, int64_t& v) {
    uint64_t u;
    if (!read_u64(in, u)) return false;
    std::memcpy(&v, &u, sizeof(v));
    return true;
}

bool PatchReader::read_f64(std::istream& in, double& v) {
    uint64_t u;
    if (!read_u64(in, u)) return false;
    std::memcpy(&v, &u, sizeof(v));
    return true;
}

// ---------------------------------------------------------------------------
// Value decoding
// ---------------------------------------------------------------------------

bool PatchReader::read_value(std::istream& in, Value& v) {
    uint8_t type;
    if (!read_u8(in, type)) return false;

    switch (type) {
        case 0: {  // int
            int64_t i;
            if (!read_i64(in, i)) return false;
            v = Value::make_int(i);
            return true;
        }
        case 1: {  // float
            double f;
            if (!read_f64(in, f)) return false;
            v = Value::make_float(f);
            return true;
        }
        case 2: {  // string
            uint16_t len;
            if (!read_u16(in, len)) return false;
            std::string s(len, '\0');
            if (len > 0 && !in.read(s.data(), len)) return false;
            v = Value::make_string(pool_.intern(s));
            return true;
        }
        case 3: {  // formid
            uint32_t id;
            if (!read_u32(in, id)) return false;
            v = Value::make_formid(id);
            return true;
        }
        case 4: {  // bool
            uint8_t b;
            if (!read_u8(in, b)) return false;
            v = Value::make_bool(b != 0);
            return true;
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Top-level read
// ---------------------------------------------------------------------------

std::optional<PatchFile> PatchReader::read(std::istream& in) {
    // Magic
    char magic[4];
    if (!in.read(magic, 4)) return std::nullopt;
    if (magic[0] != 'M' || magic[1] != 'O' || magic[2] != 'R' || magic[3] != 'A') {
        return std::nullopt;
    }

    PatchFile file;

    // Header fields
    if (!read_u16(in, file.version))          return std::nullopt;
    if (!read_u64(in, file.load_order_hash))  return std::nullopt;
    if (!read_u64(in, file.source_hash))      return std::nullopt;

    uint32_t patch_count;
    if (!read_u32(in, patch_count)) return std::nullopt;

    file.patches.reserve(patch_count);

    for (uint32_t p = 0; p < patch_count; ++p) {
        ResolvedPatch rp;

        if (!read_u32(in, rp.target_formid)) return std::nullopt;

        uint16_t field_count;
        if (!read_u16(in, field_count)) return std::nullopt;

        rp.fields.reserve(field_count);

        for (uint16_t f = 0; f < field_count; ++f) {
            FieldPatch fp;

            uint16_t field_id_raw;
            if (!read_u16(in, field_id_raw)) return std::nullopt;
            fp.field = static_cast<FieldId>(field_id_raw);

            uint8_t op_raw;
            if (!read_u8(in, op_raw)) return std::nullopt;
            fp.op = static_cast<FieldOp>(op_raw);

            if (!read_value(in, fp.value)) return std::nullopt;

            // source_mod and priority are not stored in the binary format;
            // leave them as defaults.
            fp.source_mod = StringId{};
            fp.priority   = 0;

            rp.fields.push_back(std::move(fp));
        }

        file.patches.push_back(std::move(rp));
    }

    return file;
}

} // namespace mora
