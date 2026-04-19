#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <unordered_map>
#include <utility>

namespace mora {

class AddressLibrary {
public:
    bool load(const std::filesystem::path& bin_path);
    std::optional<uint64_t> resolve(uint64_t id) const;
    size_t entry_count() const;
    std::array<int32_t, 4> skyrim_version() const;

    /// Create a mock library for testing
    static AddressLibrary mock(std::initializer_list<std::pair<uint64_t, uint64_t>> entries);

private:
    std::unordered_map<uint64_t, uint64_t> offsets_;
    std::array<int32_t, 4> version_{};
};

} // namespace mora
