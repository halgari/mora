#include "mora/codegen/address_library.h"

#include <cstdio>
#include <cstring>

namespace mora {

bool AddressLibrary::load(const std::filesystem::path& bin_path) {
    FILE* f = std::fopen(bin_path.c_str(), "rb");
    if (!f) return false;

    // Read format version
    int32_t format = 0;
    if (std::fread(&format, sizeof(format), 1, f) != 1) {
        std::fclose(f);
        return false;
    }

    if (format != 1) {
        // Only format 1 (SSE) is supported for now
        std::fclose(f);
        return false;
    }

    // Read version[4]
    if (std::fread(version_.data(), sizeof(int32_t), 4, f) != 4) {
        std::fclose(f);
        return false;
    }

    // Read pointer_size
    int32_t pointer_size = 0;
    if (std::fread(&pointer_size, sizeof(pointer_size), 1, f) != 1) {
        std::fclose(f);
        return false;
    }

    // Read address_count
    int32_t address_count = 0;
    if (std::fread(&address_count, sizeof(address_count), 1, f) != 1) {
        std::fclose(f);
        return false;
    }

    if (address_count < 0) {
        std::fclose(f);
        return false;
    }

    offsets_.reserve(static_cast<size_t>(address_count));

    for (int32_t i = 0; i < address_count; ++i) {
        uint64_t id = 0;
        uint64_t offset = 0;
        if (std::fread(&id, sizeof(id), 1, f) != 1 ||
            std::fread(&offset, sizeof(offset), 1, f) != 1) {
            std::fclose(f);
            return false;
        }
        offsets_[id] = offset;
    }

    std::fclose(f);
    return true;
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
