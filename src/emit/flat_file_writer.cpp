#include "mora/emit/flat_file_writer.h"
#include <cstring>

namespace mora::emit {

FlatFileWriter::FlatFileWriter() = default;

void FlatFileWriter::add_section(SectionId id, const void* data, size_t bytes) {
    PendingSection s{id, {}};
    s.payload.resize(bytes);
    if (bytes) std::memcpy(s.payload.data(), data, bytes);
    sections_.push_back(std::move(s));
}

void FlatFileWriter::set_esp_digest(const std::array<uint8_t, 32>& d) { esp_digest_ = d; }
void FlatFileWriter::set_toolchain_id(uint64_t id) { toolchain_id_ = id; }

static size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

std::vector<uint8_t> FlatFileWriter::finish() {
    const size_t header_size = sizeof(PatchFileV2Header);
    const size_t dir_size    = sections_.size() * sizeof(SectionDirectoryEntry);

    size_t cursor = align_up(header_size + dir_size, 8);
    std::vector<uint64_t> offsets;
    offsets.reserve(sections_.size());
    for (const auto& s : sections_) {
        offsets.push_back(cursor);
        cursor = align_up(cursor + s.payload.size(), 8);
    }
    const size_t total = cursor;

    std::vector<uint8_t> out(total, 0);

    PatchFileV2Header header{};
    header.section_count = static_cast<uint32_t>(sections_.size());
    header.file_size     = total;
    header.toolchain_id  = toolchain_id_;
    header.esp_digest    = esp_digest_;
    std::memcpy(out.data(), &header, sizeof(header));

    for (size_t i = 0; i < sections_.size(); ++i) {
        SectionDirectoryEntry e{};
        e.section_id = static_cast<uint32_t>(sections_[i].id);
        e.flags      = 0;
        e.offset     = offsets[i];
        e.size       = sections_[i].payload.size();
        std::memcpy(out.data() + header_size + (i * sizeof(e)), &e, sizeof(e));

        if (e.size) std::memcpy(out.data() + e.offset, sections_[i].payload.data(), e.size);
    }

    return out;
}

} // namespace mora::emit
