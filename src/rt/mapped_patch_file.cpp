#include "mora/rt/mapped_patch_file.h"
#include <cstring>
#include <fstream>

namespace mora::rt {

bool MappedPatchFile::open(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    auto sz = f.tellg();
    if (sz < static_cast<std::streamsize>(sizeof(emit::PatchFileV2Header))) return false;
    bytes_.resize(static_cast<size_t>(sz));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes_.data()), sz)) return false;

    std::memcpy(&header_, bytes_.data(), sizeof(header_));
    if (header_.magic != 0x41524F4Du) { bytes_.clear(); return false; }
    if (header_.version != 4u)        { bytes_.clear(); return false; }
    if (header_.file_size != bytes_.size()) { bytes_.clear(); return false; }

    directory_.resize(header_.section_count);
    const size_t dir_offset = sizeof(emit::PatchFileV2Header);
    const size_t dir_bytes  = header_.section_count * sizeof(emit::SectionDirectoryEntry);
    if (dir_offset + dir_bytes > bytes_.size()) { bytes_.clear(); return false; }
    std::memcpy(directory_.data(), bytes_.data() + dir_offset, dir_bytes);
    return true;
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
