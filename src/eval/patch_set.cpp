#include "mora/eval/patch_set.h"
#include <algorithm>
#include <map>
#include <utility>

namespace mora {

// ---------------------------------------------------------------------------
// ResolvedPatchSet
// ---------------------------------------------------------------------------

const std::vector<FieldPatch> ResolvedPatchSet::empty_patches_;

const std::vector<FieldPatch>&
ResolvedPatchSet::get_patches_for(uint32_t formid) const {
    auto it = patches_.find(formid);
    if (it == patches_.end()) return empty_patches_;
    return it->second;
}

std::vector<ResolvedPatch> ResolvedPatchSet::all_patches_sorted() const {
    // patches_ is a std::map so iteration is already sorted by formid.
    std::vector<ResolvedPatch> result;
    result.reserve(patches_.size());
    for (const auto& [fid, fields] : patches_) {
        result.push_back(ResolvedPatch{fid, fields});
    }
    return result;
}

const std::vector<Conflict>& ResolvedPatchSet::get_conflicts() const {
    return conflicts_;
}

size_t ResolvedPatchSet::patch_count() const {
    size_t total = 0;
    for (const auto& [fid, fields] : patches_) {
        total += fields.size();
    }
    return total;
}

// ---------------------------------------------------------------------------
// PatchSet
// ---------------------------------------------------------------------------

void PatchSet::add_patch(uint32_t  formid,
                         FieldId   field,
                         FieldOp   op,
                         Value     value,
                         StringId  source_mod,
                         uint32_t  priority) {
    raw_.push_back(RawPatch{formid, field, op, std::move(value), source_mod, priority});
}

ResolvedPatchSet PatchSet::resolve() const {
    ResolvedPatchSet out;

    // Key: (formid, field) — only used for Set-op conflict resolution.
    // We track the current winner and all candidates for conflict reporting.
    struct SetSlot {
        FieldPatch            winner;
        std::vector<RawPatch> all; // every Set op that targeted this slot
    };
    std::map<std::pair<uint32_t, FieldId>, SetSlot> set_slots;

    // Non-conflicting patches (Add / Remove): collect all.
    // We store them keyed by formid for final assembly.
    std::map<uint32_t, std::vector<FieldPatch>> addremove;

    for (const RawPatch& p : raw_) {
        if (p.op == FieldOp::Set) {
            auto key = std::make_pair(p.formid, p.field);
            auto it  = set_slots.find(key);
            if (it == set_slots.end()) {
                // First Set op for this slot.
                FieldPatch fp{p.field, p.op, p.value, p.source_mod, p.priority};
                set_slots.emplace(key, SetSlot{fp, {p}});
            } else {
                // Existing slot — last-write-wins by priority (higher wins).
                it->second.all.push_back(p);
                if (p.priority > it->second.winner.priority) {
                    it->second.winner = FieldPatch{p.field, p.op, p.value,
                                                   p.source_mod, p.priority};
                }
            }
        } else {
            // Add / Remove — never conflict, keep all.
            addremove[p.formid].push_back(
                FieldPatch{p.field, p.op, p.value, p.source_mod, p.priority});
        }
    }

    // Build patches_ map and conflict list.
    // First, insert all winning Set ops.
    for (auto& [key, slot] : set_slots) {
        uint32_t formid = key.first;
        out.patches_[formid].push_back(slot.winner);

        // Record conflict when more than one Set op targeted this slot.
        if (slot.all.size() > 1) {
            Conflict c;
            c.target_formid = formid;
            c.field         = key.second;
            for (const RawPatch& rp : slot.all) {
                c.entries.push_back(ConflictEntry{rp.value, rp.source_mod, rp.priority});
            }
            out.conflicts_.push_back(std::move(c));
        }
    }

    // Then, merge Add/Remove patches into the same per-formid vectors.
    for (auto& [formid, fps] : addremove) {
        auto& dest = out.patches_[formid];
        dest.insert(dest.end(), fps.begin(), fps.end());
    }

    return out;
}

} // namespace mora
