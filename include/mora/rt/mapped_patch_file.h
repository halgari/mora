#pragma once
#include "mora/emit/patch_file_v2.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mora::rt {

struct SectionView {
    const uint8_t* data = nullptr;
    size_t         size = 0;
};

class MappedPatchFile {
public:
    bool open(const std::string& path);
    SectionView section(emit::SectionId id) const;
    const emit::PatchFileV2Header& header() const { return header_; }
    bool is_open() const { return !bytes_.empty(); }

private:
    std::vector<uint8_t> bytes_;
    emit::PatchFileV2Header header_{};
    std::vector<emit::SectionDirectoryEntry> directory_;
};

} // namespace mora::rt
