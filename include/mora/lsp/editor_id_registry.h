#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mora::lsp {

struct EditorIdInfo {
    uint32_t form_id = 0;       // resolved FormID (e.g. 0x0001BCB1)
    std::string source_plugin;  // "Skyrim.esm", "Dawnguard.esm", ...
    std::string record_type;    // "FACT", "WEAP", etc.
};

class EditorIdRegistry {
public:
    EditorIdRegistry();

    // Replace the registry contents by scanning ESPs under `data_dir`.
    // Best-effort: silently no-ops if the path doesn't exist or is empty.
    void scan(const std::filesystem::path& data_dir);

    // Lookup an editor ID. Returns nullopt if not loaded or not found.
    std::optional<EditorIdInfo> lookup(std::string_view editor_id) const;

    // True if scan() has loaded at least one ESP.
    bool has_data() const { return !entries_.empty(); }

private:
    std::unordered_map<std::string, EditorIdInfo> entries_;
};

} // namespace mora::lsp
