#include "mora/lsp/editor_id_registry.h"

namespace mora::lsp {

EditorIdRegistry::EditorIdRegistry() = default;

void EditorIdRegistry::scan(const std::filesystem::path& data_dir) {
    entries_.clear();
    if (data_dir.empty() || !std::filesystem::is_directory(data_dir)) return;
    // Phase-3 v1: this is a stub. A future task will use mora's existing ESP
    // reader (src/esp/) to walk the data dir and populate `entries_` with
    // FACT / WEAP / NPC_ / etc. records that have an EDID subrecord. For
    // now we leave entries_ empty; hover degrades to "FormID unknown".
    // The point of the stub is to wire all the plumbing so we can drop in
    // the real scan in a follow-up without touching call sites.
    (void)data_dir;
}

std::optional<EditorIdInfo> EditorIdRegistry::lookup(std::string_view editor_id) const {
    auto it = entries_.find(std::string(editor_id));
    if (it == entries_.end()) return std::nullopt;
    return it->second;
}

} // namespace mora::lsp
