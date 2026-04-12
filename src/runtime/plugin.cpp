// Mora SKSE Runtime Plugin
// Entry point for the Mora runtime DLL, loaded by SKSE into Skyrim.
// All static patches are baked into the DLL via LLVM codegen (apply_all_patches).
// Dynamic rules are loaded from .mora.rt files at DataLoaded.

#ifdef MORA_HAS_COMMONLIB

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include "mora/runtime/dynamic_runner.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <filesystem>

namespace {

void on_data_loaded() {
    mora::StringPool pool;
    mora::DiagBag diags;

    // Load dynamic rules (.mora.rt files)
    auto cache_dir = std::filesystem::path("Data/MoraCache");
    if (!std::filesystem::exists(cache_dir)) {
        return;
    }

    mora::DynamicRunner runner(pool, diags);
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir)) {
        if (entry.path().extension() == ".rt" &&
            entry.path().stem().extension() == ".mora") {
            SKSE::log::info("Loading dynamic rules: {}", entry.path().string());
            if (runner.load(entry.path())) {
                SKSE::log::info("  {} rules loaded", runner.rules_loaded());
            } else {
                SKSE::log::error("  Failed to load: {}", entry.path().string());
            }
        }
    }

    runner.on_data_loaded();
}

} // anonymous namespace

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SKSE::log::info("Mora Runtime v0.1.0 loaded");

    SKSE::GetMessagingInterface()->RegisterListener(
        [](SKSE::MessagingInterface::Message* msg) {
            if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
                on_data_loaded();
            }
        });

    return true;
}

#else // !MORA_HAS_COMMONLIB

#ifdef _WIN32
#include <windows.h>

extern "C" __declspec(dllexport) BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}

extern "C" __declspec(dllexport) const char* mora_runtime_version() {
    return "0.1.0";
}

#else
// Non-Windows stub for compilation testing
namespace mora { void runtime_stub() {} }
#endif

#endif // MORA_HAS_COMMONLIB
