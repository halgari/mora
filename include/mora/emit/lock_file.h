#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>

namespace mora {

struct LockFile {
    uint64_t source_hash = 0;
    uint64_t load_order_hash = 0;

    static uint64_t hash_files(const std::vector<std::filesystem::path>& files);
    static uint64_t hash_string(const std::string& s);

    void write(const std::filesystem::path& path) const;
    static LockFile read(const std::filesystem::path& path);
    bool matches(uint64_t src_hash, uint64_t lo_hash) const;
};

} // namespace mora
