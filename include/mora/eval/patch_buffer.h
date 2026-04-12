#pragma once
#include "mora/emit/patch_table.h"
#include <vector>
#include <cstddef>

namespace mora {

class PatchBuffer {
public:
    void reserve(size_t n) { entries_.reserve(n); }

    void emit(uint32_t formid, uint8_t field_id, uint8_t op,
              uint8_t value_type, uint64_t value) {
        entries_.push_back({formid, field_id, op, value_type, 0, value});
    }

    // Sort by (formid, field_id, op) then deduplicate.
    // Last-write-wins for duplicate (formid, field_id) Set ops.
    // Add ops are kept if they have different values.
    void sort_and_dedup();

    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    const std::vector<PatchEntry>& entries() const { return entries_; }
    std::vector<PatchEntry>& mutable_entries() { return entries_; }

private:
    std::vector<PatchEntry> entries_;
};

} // namespace mora
