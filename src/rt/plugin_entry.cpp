// src/rt/plugin_entry.cpp
// SKSE plugin glue — loads mora_patches.bin and applies at DataLoaded.

#ifdef _WIN32

#include <SKSE/SKSE.h>
#include <chrono>
#include <filesystem>
#include <windows.h>

#include "mora/rt/skse_hooks.h"

// Defined in patch_walker.cpp
uint32_t load_patches(const std::filesystem::path& patch_file);
void apply_all_patches();

namespace {

// Get the directory containing this DLL
std::filesystem::path get_dll_directory() {
    HMODULE hmod = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&get_dll_directory),
        &hmod);
    char path[MAX_PATH];
    GetModuleFileNameA(hmod, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

void on_data_loaded() {
    auto dll_dir = get_dll_directory();
    SKSE::log::info("[Mora] DLL directory: {}", dll_dir.string());
    auto patch_file = dll_dir / "mora_patches.bin";
    SKSE::log::info("[Mora] Looking for patches at: {}", patch_file.string());

    uint32_t count = load_patches(patch_file);
    if (count == 0) {
        SKSE::log::info("[Mora] No patches loaded from {}", patch_file.string());
        return;
    }

    SKSE::log::info("[Mora] Loaded {} patches", count);

    auto start = std::chrono::steady_clock::now();
    apply_all_patches();
    auto end = std::chrono::steady_clock::now();

    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    SKSE::log::info("[Mora] Applied {} patches in {:.2f} ms", count, ms);

    // Install SKSE event sinks so gameplay events inject deltas into the DAG.
    mora::rt::register_skse_hooks(mora::rt::get_global_dag_runtime());
    SKSE::log::info("[Mora] SKSE event hooks registered");
}

void message_handler(SKSE::MessagingInterface::Message* msg) {
    if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
        on_data_loaded();
    }
}

} // anonymous namespace

// SKSE plugin version struct — must match CommonLibSSE layout (0x350 bytes)
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
    1 | 4,  // AddressLibraryPostAE | StructsPost629
    {0}, 0
};

extern "C" __declspec(dllexport)
bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SKSE::log::info("MoraRuntime v0.1.0 loaded");

    SKSE::GetMessagingInterface()->RegisterListener(message_handler);
    return true;
}

extern "C" __declspec(dllexport)
int __stdcall DllMain(void* hinstDLL, uint32_t fdwReason, void* lpvReserved) {
    (void)hinstDLL; (void)fdwReason; (void)lpvReserved;
    return 1;
}

#else
// Linux stub
namespace mora::rt { void plugin_entry_stub() {} }
#endif
