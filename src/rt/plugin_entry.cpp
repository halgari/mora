// src/rt/plugin_entry.cpp
// SKSE plugin glue — compiled into MoraRuntime.dll with CommonLibSSE-NG.
// Loads mora_patches.bin from the SKSE/Plugins directory and applies at DataLoaded.

#ifdef _WIN32

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <chrono>
#include <filesystem>

// Defined in patch_walker.cpp
uint32_t load_patches(const std::filesystem::path& patch_file);
void apply_all_patches();

namespace {

void on_data_loaded() {
    // Find mora_patches.bin next to this DLL
    auto plugin_dir = std::filesystem::path(SKSE::GetPluginFolder());
    auto patch_file = plugin_dir / "mora_patches.bin";

    uint32_t count = load_patches(patch_file);
    if (count == 0) {
        SKSE::log::info("[Mora] No patches loaded ({})", patch_file.string());
        return;
    }

    SKSE::log::info("[Mora] Loaded {} patches from {}", count, patch_file.string());

    auto start = std::chrono::steady_clock::now();
    apply_all_patches();
    auto end = std::chrono::steady_clock::now();

    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    SKSE::log::info("[Mora] Applied {} patches in {:.2f} ms", count, ms);
}

void message_handler(SKSE::MessagingInterface::Message* msg) {
    if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
        on_data_loaded();
    }
}

} // anonymous namespace

extern "C" __declspec(dllexport)
bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SKSE::log::info("MoraRuntime v0.1.0 loaded");

    SKSE::GetMessagingInterface()->RegisterListener(message_handler);
    return true;
}

#else
// Linux stub -- this file is only meaningful when cross-compiled for Windows
namespace mora::rt { void plugin_entry_stub() {} }
#endif
