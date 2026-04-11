// Mora SKSE Runtime Plugin
// Entry point for the Mora runtime DLL, loaded by SKSE into Skyrim.

#ifdef MORA_HAS_COMMONLIB

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include "mora/runtime/patch_applier.h"
#include "mora/runtime/dynamic_runner.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <filesystem>

namespace {

void on_data_loaded() {
    mora::StringPool pool;
    mora::DiagBag diags;

    // Look for MoraCache directory next to the DLL
    auto cache_dir = std::filesystem::path("Data/MoraCache");
    if (!std::filesystem::exists(cache_dir)) {
        SKSE::log::warn("MoraCache directory not found, skipping patch application");
        return;
    }

    // Apply all .mora.patch files
    mora::PatchApplier applier(pool);
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir)) {
        if (entry.path().extension() == ".patch" &&
            entry.path().stem().extension() == ".mora") {
            SKSE::log::info("Applying patch: {}", entry.path().string());
            auto result = applier.apply(entry.path());
            SKSE::log::info("  {} patches applied, {} failed, {} forms modified ({:.1f}ms)",
                            result.patches_applied, result.patches_failed,
                            result.forms_modified, result.elapsed_ms);
        }
    }

    // Load dynamic rules (.mora.rt files)
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
#include "mora/runtime/patch_applier.h"
#include "mora/core/string_pool.h"

extern "C" __declspec(dllexport) BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}

extern "C" __declspec(dllexport) const char* mora_runtime_version() {
    return "0.1.0";
}

extern "C" __declspec(dllexport) int mora_runtime_test_apply(const char* patch_path) {
    // Test: load a .mora.patch file and report how many patches it contains
    mora::StringPool pool;
    mora::PatchApplier applier(pool);
    auto result = applier.apply(patch_path);
    return static_cast<int>(result.patches_applied);
}

#else
// Non-Windows stub for compilation testing
namespace mora { void runtime_stub() {} }
#endif

#endif // MORA_HAS_COMMONLIB
