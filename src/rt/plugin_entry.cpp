// src/rt/plugin_entry.cpp
// SKSE plugin glue — loads mora_patches.bin and applies at DataLoaded.

#ifdef _WIN32

#include <SKSE/SKSE.h>
#include <chrono>
#include <filesystem>
#include <windows.h>

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
    auto patch_file = get_dll_directory() / "mora_patches.bin";

    uint32_t count = load_patches(patch_file);
    if (count == 0) {
        SKSE::log::info("[Mora] No patches loaded");
        return;
    }

    SKSE::log::info("[Mora] Loaded {} patches", count);

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
// Linux stub
namespace mora::rt { void plugin_entry_stub() {} }
#endif
