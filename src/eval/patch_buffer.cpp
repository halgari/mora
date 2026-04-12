#include "mora/eval/patch_buffer.h"
#include <algorithm>

namespace mora {

void PatchBuffer::sort_and_dedup() {
    if (entries_.size() <= 1) return;

    // Sort by (formid, field_id, op, value)
    std::sort(entries_.begin(), entries_.end(),
              [](const PatchEntry& a, const PatchEntry& b) {
                  if (a.formid != b.formid) return a.formid < b.formid;
                  if (a.field_id != b.field_id) return a.field_id < b.field_id;
                  if (a.op != b.op) return a.op < b.op;
                  return a.value < b.value;
              });

    // Deduplicate:
    // - Set ops (op==0): last-write-wins per (formid, field_id). Since sorted,
    //   we keep the last Set entry for each (formid, field_id).
    // - Add ops (op==1): keep distinct (formid, field_id, value) tuples.
    // - Remove ops (op==2): same as Add — keep distinct values.
    size_t write = 0;
    for (size_t read = 0; read < entries_.size(); read++) {
        const auto& e = entries_[read];

        if (e.op == static_cast<uint8_t>(FieldOp::Set)) {
            // Look ahead: if the next entry is also a Set on the same
            // (formid, field_id), skip this one (last writer wins).
            if (read + 1 < entries_.size()) {
                const auto& next = entries_[read + 1];
                if (next.formid == e.formid &&
                    next.field_id == e.field_id &&
                    next.op == e.op) {
                    continue; // skip — next entry overwrites this one
                }
            }
            entries_[write++] = e;
        } else {
            // Add/Remove: keep if different from previous written entry
            // with same (formid, field_id, op).
            if (write > 0) {
                const auto& prev = entries_[write - 1];
                if (prev.formid == e.formid &&
                    prev.field_id == e.field_id &&
                    prev.op == e.op &&
                    prev.value == e.value) {
                    continue; // duplicate Add/Remove — skip
                }
            }
            entries_[write++] = e;
        }
    }
    entries_.resize(write);
}

} // namespace mora
