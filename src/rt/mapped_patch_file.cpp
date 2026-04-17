#include "mora/rt/mapped_patch_file.h"
#include <cstring>
#include <fstream>

namespace mora::rt {

std::string_view open_result_name(OpenResult r) {
    switch (r) {
        case OpenResult::Ok:                 return "ok";
        case OpenResult::FileNotFound:       return "file-not-found";
        case OpenResult::Truncated:          return "truncated";
        case OpenResult::TooLarge:           return "too-large";
        case OpenResult::ReadFailed:         return "read-failed";
        case OpenResult::BadMagic:           return "bad-magic";
        case OpenResult::BadVersion:         return "bad-version";
        case OpenResult::SizeMismatch:       return "size-mismatch";
        case OpenResult::DirectoryTruncated: return "directory-truncated";
    }
    return "unknown";
}

bool MappedPatchFile::open(const std::string& path) {
    return open_detailed(path) == OpenResult::Ok;
}

OpenResult MappedPatchFile::open_detailed(const std::string& path) {
    bytes_.clear();
    directory_.clear();
    header_ = {};

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return OpenResult::FileNotFound;

    auto sz = f.tellg();
    if (sz < static_cast<std::streamsize>(sizeof(emit::PatchFileV2Header))) {
        return OpenResult::Truncated;
    }
    if (static_cast<size_t>(sz) > kMaxPatchFileBytes) {
        return OpenResult::TooLarge;
    }

    bytes_.resize(static_cast<size_t>(sz));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes_.data()), sz)) {
        bytes_.clear();
        return OpenResult::ReadFailed;
    }

    std::memcpy(&header_, bytes_.data(), sizeof(header_));
    if (header_.magic != 0x41524F4Du) { bytes_.clear(); return OpenResult::BadMagic; }
    if (header_.version != 4u)        { bytes_.clear(); return OpenResult::BadVersion; }
    if (header_.file_size != bytes_.size()) {
        bytes_.clear();
        return OpenResult::SizeMismatch;
    }

    directory_.resize(header_.section_count);
    const size_t dir_offset = sizeof(emit::PatchFileV2Header);
    const size_t dir_bytes  = header_.section_count * sizeof(emit::SectionDirectoryEntry);
    if (dir_offset + dir_bytes > bytes_.size()) {
        bytes_.clear();
        directory_.clear();
        return OpenResult::DirectoryTruncated;
    }
    std::memcpy(directory_.data(), bytes_.data() + dir_offset, dir_bytes);
    return OpenResult::Ok;
}

SectionView MappedPatchFile::section(emit::SectionId id) const {
    for (const auto& e : directory_) {
        if (e.section_id == static_cast<uint32_t>(id)) {
            if (e.offset + e.size > bytes_.size()) return {};
            return {bytes_.data() + e.offset, static_cast<size_t>(e.size)};
        }
    }
    return {};
}

} // namespace mora::rt
