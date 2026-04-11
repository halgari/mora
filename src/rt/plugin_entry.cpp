// src/rt/plugin_entry.cpp
// This file is compiled to bitcode and LTO-linked with generated IR.
// The IR defines apply_all_patches(); this file provides the SKSE glue.

#ifdef _WIN32
#include <cstdint>

// Windows API import
extern "C" __declspec(dllimport) void* __stdcall GetModuleHandleW(const wchar_t*);

// Forward declaration -- defined by the IR emitter in the generated module
extern "C" void apply_all_patches(void* skyrim_base);

// Get the base address of SkyrimSE.exe at runtime
static void* get_skyrim_base() {
    return GetModuleHandleW(nullptr);
}

// Simple DataLoaded handler
static bool g_applied = false;

extern "C" __declspec(dllexport)
int __stdcall DllMain(void* hinstDLL, uint32_t fdwReason, void* lpvReserved) {
    (void)hinstDLL; (void)fdwReason; (void)lpvReserved;
    return 1; // TRUE
}

// SKSE Plugin Version data
struct SKSEPluginVersionData {
    uint32_t dataVersion;
    uint32_t pluginVersion;
    char name[256];
    char author[256];
    uint32_t addressIndependence;
    uint32_t structureCompatibility;
    uint32_t compatible[16];
    uint32_t seVersionRequired;
};

extern "C" __declspec(dllexport)
SKSEPluginVersionData SKSEPlugin_Version = {
    1,          // dataVersion
    1,          // pluginVersion (0.0.1)
    "MoraRuntime",
    "Mora Project",
    1,          // AddressIndependent
    2,          // StructureIndependent (layout-dependent on runtime)
    {0},
    0
};

// SKSE message types
constexpr uint32_t kSKSE_DataLoaded = 8;

struct SKSEMessage {
    const char* sender;
    uint32_t type;
    uint32_t dataLen;
    void* data;
};

using SKSEMessageCallback = void(*)(SKSEMessage*);

// SKSE Messaging interface (simplified)
struct SKSEMessagingInterface {
    uint32_t interfaceVersion;
    bool (*RegisterListener)(void* plugin, const char* sender, SKSEMessageCallback callback);
};

// SKSE Load interface
struct SKSEInterface {
    uint32_t skseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
    void* (*QueryInterface)(uint32_t id);
    uint32_t (*GetPluginHandle)();
    uint32_t (*GetReleaseIndex)();
};

static uint32_t g_plugin_handle = 0;

static void message_handler(SKSEMessage* msg) {
    if (msg->type == kSKSE_DataLoaded && !g_applied) {
        g_applied = true;
        void* base = get_skyrim_base();
        if (base) {
            apply_all_patches(base);
        }
    }
}

extern "C" __declspec(dllexport)
bool SKSEPlugin_Load(SKSEInterface* skse) {
    g_plugin_handle = skse->GetPluginHandle();

    // Query messaging interface (ID = 2)
    auto* messaging = static_cast<SKSEMessagingInterface*>(skse->QueryInterface(2));
    if (messaging) {
        messaging->RegisterListener(nullptr, "SKSE", message_handler);
    }

    return true;
}

#else
// Linux stub -- this file is only meaningful when cross-compiled for Windows
namespace mora::rt { void plugin_entry_stub() {} }
#endif
