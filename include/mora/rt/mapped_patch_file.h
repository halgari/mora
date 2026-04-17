#pragma once
#include "mora/emit/patch_file_v2.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mora::rt {

struct SectionView {
    const uint8_t* data = nullptr;
    size_t         size = 0;
};

// Hard cap on patch-file size. Real files are under 1 MB today; 128 MiB
// leaves generous headroom while refusing to allocate a gigabyte-sized
// buffer off a corrupt or adversarial file before the header check runs.
inline constexpr size_t kMaxPatchFileBytes = 128u << 20;

enum class OpenResult {
    Ok,
    FileNotFound,           // ifstream open failed
    Truncated,              // smaller than header
    TooLarge,               // exceeds kMaxPatchFileBytes
    ReadFailed,             // stream hit EOF / IO error mid-read
    BadMagic,               // magic bytes didn't match
    BadVersion,             // version field didn't match
    SizeMismatch,           // header.file_size != actual size on disk
    DirectoryTruncated,     // section directory doesn't fit in the file
};

// Human-readable (and stable) name for logging. Safe to call on any
// OpenResult value including future additions — returns "unknown".
std::string_view open_result_name(OpenResult r);

class MappedPatchFile {
public:
    // Back-compat wrapper: returns true on OpenResult::Ok.
    bool open(const std::string& path);

    // Detailed outcome so the runtime's SKSE log line can name the
    // specific reject reason instead of silently presenting zero
    // patches. On any non-Ok result `bytes_` is cleared and every
    // accessor returns an empty view.
    OpenResult open_detailed(const std::string& path);

    SectionView section(emit::SectionId id) const;
    const emit::PatchFileV2Header& header() const { return header_; }
    bool is_open() const { return !bytes_.empty(); }

private:
    std::vector<uint8_t> bytes_;
    emit::PatchFileV2Header header_{};
    std::vector<emit::SectionDirectoryEntry> directory_;
};

} // namespace mora::rt
