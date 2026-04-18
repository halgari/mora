#include "mora/codegen/address_library.h"

#include <cstdio>
#include <cstring>

namespace mora {

// Read a single value of type T from the file
template <typename T>
static bool read_val(FILE* f, T& out) {
    return std::fread(&out, sizeof(T), 1, f) == 1;
}

// Format 2 (AE) uses delta-encoded entries for compression.
// Each entry is encoded as a type byte where lo nibble encodes the ID
// and hi nibble encodes the offset, using delta from previous values.
static bool unpack_format2(FILE* f, int32_t pointer_size, int32_t count,
                            std::unordered_map<uint64_t, uint64_t>& offsets) {
    uint64_t prev_id = 0;
    uint64_t prev_offset = 0;

    for (int32_t i = 0; i < count; ++i) {
        uint8_t type = 0;
        if (!read_val(f, type)) return false;

        uint8_t const lo = type & 0xF;
        uint8_t const hi = type >> 4;

        // Decode ID
        uint64_t id = 0;
        switch (lo) {
            case 0: { uint64_t v{}; if (!read_val(f, v)) return false; id = v; break; }
            case 1: id = prev_id + 1; break;
            case 2: { uint8_t v{}; if (!read_val(f, v)) return false; id = prev_id + v; break; }
            case 3: { uint8_t v{}; if (!read_val(f, v)) return false; id = prev_id - v; break; }
            case 4: { uint16_t v{}; if (!read_val(f, v)) return false; id = prev_id + v; break; }
            case 5: { uint16_t v{}; if (!read_val(f, v)) return false; id = prev_id - v; break; }
            case 6: { uint16_t v{}; if (!read_val(f, v)) return false; id = v; break; }
            case 7: { uint32_t v{}; if (!read_val(f, v)) return false; id = v; break; }
            default: return false;
        }

        // Decode offset
        uint64_t const tmp = (hi & 8) ? (prev_offset / static_cast<uint64_t>(pointer_size)) : prev_offset;
        uint64_t offset = 0;
        switch (hi & 7) {
            case 0: { uint64_t v{}; if (!read_val(f, v)) return false; offset = v; break; }
            case 1: offset = tmp + 1; break;
            case 2: { uint8_t v{}; if (!read_val(f, v)) return false; offset = tmp + v; break; }
            case 3: { uint8_t v{}; if (!read_val(f, v)) return false; offset = tmp - v; break; }
            case 4: { uint16_t v{}; if (!read_val(f, v)) return false; offset = tmp + v; break; }
            case 5: { uint16_t v{}; if (!read_val(f, v)) return false; offset = tmp - v; break; }
            case 6: { uint16_t v{}; if (!read_val(f, v)) return false; offset = v; break; }
            case 7: { uint32_t v{}; if (!read_val(f, v)) return false; offset = v; break; }
            default: return false;
        }

        if (hi & 8) {
            offset *= static_cast<uint64_t>(pointer_size);
        }

        offsets[id] = offset;
        prev_id = id;
        prev_offset = offset;
    }
    return true;
}

bool AddressLibrary::load(const std::filesystem::path& bin_path) {
    FILE* f = std::fopen(bin_path.string().c_str(), "rb");
    if (!f) return false;

    // Read format version
    int32_t format = 0;
    if (!read_val(f, format)) { std::fclose(f); return false; }

    if (format != 1 && format != 2) {
        std::fclose(f);
        return false;
    }

    // Read version[4]
    if (std::fread(version_.data(), sizeof(int32_t), 4, f) != 4) {
        std::fclose(f);
        return false;
    }

    // Format 2 (AE) has a name string after version
    if (format == 2) {
        int32_t name_len = 0;
        if (!read_val(f, name_len)) { std::fclose(f); return false; }
        if (std::fseek(f, name_len, SEEK_CUR) != 0) { std::fclose(f); return false; }
    }

    // Read pointer_size and address_count
    int32_t pointer_size = 0;
    int32_t address_count = 0;
    if (!read_val(f, pointer_size) || !read_val(f, address_count)) {
        std::fclose(f);
        return false;
    }

    if (address_count < 0) { std::fclose(f); return false; }

    offsets_.reserve(static_cast<size_t>(address_count));

    bool ok = false;
    if (format == 1) {
        // Format 1: raw id/offset pairs
        ok = true;
        for (int32_t i = 0; i < address_count; ++i) {
            uint64_t id = 0;
            uint64_t offset = 0;
            if (!read_val(f, id) || !read_val(f, offset)) { ok = false; break; }
            offsets_[id] = offset;
        }
    } else {
        // Format 2: delta-encoded entries
        ok = unpack_format2(f, pointer_size, address_count, offsets_);
    }

    std::fclose(f);
    return ok;
}

std::optional<uint64_t> AddressLibrary::resolve(uint64_t id) const {
    auto it = offsets_.find(id);
    if (it == offsets_.end()) return std::nullopt;
    return it->second;
}

size_t AddressLibrary::entry_count() const {
    return offsets_.size();
}

std::array<int32_t, 4> AddressLibrary::skyrim_version() const {
    return version_;
}

AddressLibrary AddressLibrary::mock(
    std::initializer_list<std::pair<uint64_t, uint64_t>> entries) {
    AddressLibrary lib;
    lib.version_ = {0, 0, 0, 0};
    for (auto& [id, offset] : entries) {
        lib.offsets_[id] = offset;
    }
    return lib;
}

} // namespace mora
