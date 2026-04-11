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

// SKSE plugin version struct — must be 0x350 bytes matching CommonLibSSE layout.
struct SKSEPluginVersionData {
    uint32_t dataVersion;               // 0x000
    uint32_t pluginVersion;             // 0x004
    char     pluginName[256];           // 0x008
    char     author[256];               // 0x108
    char     supportEmail[252];         // 0x208
    uint32_t versionIndependenceEx;     // 0x304
    uint32_t versionIndependence;       // 0x308
    uint32_t compatibleVersions[16];    // 0x30C
    uint32_t seVersionRequired;         // 0x34C
};

extern "C" __declspec(dllexport)
SKSEPluginVersionData SKSEPlugin_Version = {
    1,          // dataVersion
    1,          // pluginVersion (0.0.1)
    "MoraRuntime",
    "Mora Project",
    "",         // supportEmail
    0,          // versionIndependenceEx
    1 | 4,      // versionIndependence: AddressLibraryPostAE | StructsPost629
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

// SKSE Messaging interface — must match SKSE's actual struct layout
struct SKSEMessagingInterface {
    uint32_t interfaceVersion;
    bool (*RegisterListener)(uint32_t pluginHandle, const char* sender, SKSEMessageCallback handler);
    bool (*Dispatch)(uint32_t pluginHandle, uint32_t messageType, void* data, uint32_t dataLen, const char* receiver);
    void* (*GetEventDispatcher)(uint32_t dispatcherId);
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

    // Query messaging interface (ID = 5 = kInterface_Messaging)
    auto* messaging = static_cast<SKSEMessagingInterface*>(skse->QueryInterface(5));
    if (messaging) {
        messaging->RegisterListener(g_plugin_handle, "SKSE", message_handler);
    }

    return true;
}

#else
// Linux stub -- this file is only meaningful when cross-compiled for Windows
namespace mora::rt { void plugin_entry_stub() {} }
#endif
