// ═══════════════════════════════════════════════════════════════════════════
// MoraRuntime.dll — SKSE plugin entry.
//
// At game startup (DataLoaded event):
//   1. Locate the DLL directory
//   2. Read the flat-binary runtime snapshot at <DLL-dir>/mora_runtime.bin
//   3. Construct an SKSEGameAPI bound to a per-session StringPool
//   4. Dispatch every effect fact through the GameAPI
//
// No DAG, no maintain/on, no dynamic rule evaluation — just snapshot
// apply. See extensions/skyrim_runtime/src/runtime_snapshot.cpp for the
// binary format and src/rt/skse_game_api.cpp for the concrete dispatch.
// ═══════════════════════════════════════════════════════════════════════════

#ifdef MORA_WITH_COMMONLIB

#include "mora_skyrim_runtime/game_api.h"
#include "mora_skyrim_runtime/runtime_snapshot.h"

#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

#include <SKSE/SKSE.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <windows.h>

namespace mora_skyrim_runtime {
// Declared in skse_game_api.cpp.
std::unique_ptr<GameAPI> make_skse_game_api(const mora::StringPool& pool);
} // namespace mora_skyrim_runtime

namespace {

std::filesystem::path get_dll_directory() {
    HMODULE hmod = nullptr;
    ::GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&get_dll_directory),
        &hmod);
    char path[MAX_PATH];
    ::GetModuleFileNameA(hmod, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

// Keep the pool alive for the SKSE session — SKSEGameAPI holds a ref
// to it and will be called again for every loaded game if we wire that
// up later.
std::unique_ptr<mora::StringPool>                 g_pool;
std::unique_ptr<mora_skyrim_runtime::GameAPI>     g_api;

void on_data_loaded() {
    auto dll_dir = get_dll_directory();
    SKSE::log::info("[Mora] DLL directory: {}", dll_dir.string());

    auto snap_path = dll_dir / "mora_runtime.bin";
    SKSE::log::info("[Mora] Looking for snapshot: {}", snap_path.string());

    if (!std::filesystem::exists(snap_path)) {
        SKSE::log::warn("[Mora] No snapshot at {}; nothing to apply.",
                         snap_path.string());
        return;
    }

    // Fresh pool per session. If the user reloads saves mid-session we
    // can re-apply from the same pool (no reason to rebuild strings).
    g_pool = std::make_unique<mora::StringPool>();
    g_api  = mora_skyrim_runtime::make_skse_game_api(*g_pool);

    mora::DiagBag diags;
    auto snap = mora_skyrim_runtime::read_snapshot(snap_path, diags);
    if (!snap) {
        SKSE::log::error("[Mora] Failed to parse snapshot ({} diag error(s))",
                          diags.error_count());
        return;
    }

    SKSE::log::info("[Mora] Snapshot loaded: {} rows, {} bytes in string pool",
                     snap->rows.size(), snap->string_pool.size());

    auto start = std::chrono::steady_clock::now();
    size_t applied = mora_skyrim_runtime::apply_snapshot(*snap, *g_api, *g_pool);
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    SKSE::log::info("[Mora] Applied {} effect fact(s) in {:.2f} ms",
                     applied, ms);
}

void message_handler(SKSE::MessagingInterface::Message* msg) {
    if (msg && msg->type == SKSE::MessagingInterface::kDataLoaded) {
        on_data_loaded();
    }
}

} // anonymous namespace

// ── SKSE plugin version descriptor (0x350-byte layout) ───────────────
struct SKSEPluginVersionData {
    uint32_t dataVersion;
    uint32_t pluginVersion;
    char     pluginName[256];
    char     author[256];
    char     supportEmail[252];
    uint32_t versionIndependenceEx;
    uint32_t versionIndependence;
    uint32_t compatibleVersions[16];
    uint32_t seVersionRequired;
};

extern "C" __declspec(dllexport)
SKSEPluginVersionData SKSEPlugin_Version = {
    1, 1,
    "MoraRuntime",
    "Mora Project",
    "",
    0,
    1 | 4,   // AddressLibraryPostAE | StructsPost629
    {0}, 0
};

extern "C" __declspec(dllexport)
bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SKSE::log::info("MoraRuntime v3 loaded");

    SKSE::GetMessagingInterface()->RegisterListener(message_handler);
    return true;
}

extern "C" __declspec(dllexport)
int __stdcall DllMain(void* hinstDLL, uint32_t fdwReason, void* lpvReserved) {
    (void)hinstDLL; (void)fdwReason; (void)lpvReserved;
    return 1;
}

#else
namespace mora_skyrim_runtime::rt {
[[maybe_unused]] static void plugin_entry_stub() {}
}
#endif
