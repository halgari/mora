// src/rt/plugin_entry.cpp
// SKSE plugin glue — compiled into mora_rt.lib with CommonLibSSE-NG.
// The IR defines apply_all_patches(); this file provides the SKSE entry.

#ifdef _WIN32

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <chrono>

// Forward declaration -- defined by the IR emitter in the generated module
extern "C" void apply_all_patches();

// Patch count -- defined by the IR emitter as a global constant
extern "C" uint32_t mora_patch_count;

namespace {

void on_data_loaded() {
    uint32_t count = &mora_patch_count ? mora_patch_count : 0;

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
