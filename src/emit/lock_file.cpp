#include "mora/emit/lock_file.h"
#include <fstream>
#include <functional>
#include <stdexcept>

namespace mora {

// ---------------------------------------------------------------------------
// hash_string: use std::hash<std::string>
// ---------------------------------------------------------------------------

uint64_t LockFile::hash_string(const std::string& s) {
    return static_cast<uint64_t>(std::hash<std::string>{}(s));
}

// ---------------------------------------------------------------------------
// hash_files: combine per-file content hashes and path hashes using the
// mixing function: h ^= hash + 0x9e3779b9 + (h << 6) + (h >> 2)
// ---------------------------------------------------------------------------

uint64_t LockFile::hash_files(const std::vector<std::filesystem::path>& files) {
    uint64_t h = 0;
    for (const auto& p : files) {
        // Hash the file path itself
        uint64_t path_hash = hash_string(p.string());
        h ^= path_hash + 0x9e3779b9ULL + (h << 6) + (h >> 2);

        // Hash the file contents
        std::ifstream in(p, std::ios::binary);
        if (in) {
            std::string content((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
            uint64_t content_hash = hash_string(content);
            h ^= content_hash + 0x9e3779b9ULL + (h << 6) + (h >> 2);
        }
    }
    return h;
}

// ---------------------------------------------------------------------------
// write: binary file — source_hash (u64 LE) + load_order_hash (u64 LE)
// ---------------------------------------------------------------------------

void LockFile::write(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("LockFile::write: cannot open " + path.string());
    }

    auto write_u64 = [&](uint64_t v) {
        uint8_t buf[8];
        for (int i = 0; i < 8; ++i) {
            buf[i] = static_cast<uint8_t>(v >> (i * 8));
        }
        out.write(reinterpret_cast<const char*>(buf), 8);
    };

    write_u64(source_hash);
    write_u64(load_order_hash);
}

// ---------------------------------------------------------------------------
// read: read back the two u64s
// ---------------------------------------------------------------------------

LockFile LockFile::read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("LockFile::read: cannot open " + path.string());
    }

    auto read_u64 = [&]() -> uint64_t {
        uint8_t buf[8];
        if (!in.read(reinterpret_cast<char*>(buf), 8)) {
            throw std::runtime_error("LockFile::read: unexpected EOF in " + path.string());
        }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(buf[i]) << (i * 8);
        }
        return v;
    };

    LockFile lf;
    lf.source_hash     = read_u64();
    lf.load_order_hash = read_u64();
    return lf;
}

// ---------------------------------------------------------------------------
// matches: compare both hashes
// ---------------------------------------------------------------------------

bool LockFile::matches(uint64_t src_hash, uint64_t lo_hash) const {
    return source_hash == src_hash && load_order_hash == lo_hash;
}

} // namespace mora
