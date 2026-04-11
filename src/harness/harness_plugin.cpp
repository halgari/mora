#ifdef _WIN32

#include "mora/harness/tcp_listener.h"
#include "mora/harness/weapon_dumper.h"
#include "mora/harness/ini_reader.h"
#include "mora/rt/bst_hashmap.h"
#include "mora/rt/form_ops.h"
#include "mora/codegen/address_library.h"

#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ── SKSE types (same as plugin_entry.cpp) ───────────────────────────

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

struct SKSEMessage {
    const char* sender;
    uint32_t type;
    uint32_t dataLen;
    void* data;
};

using EventCallback = void(*)(SKSEMessage*);
using PluginHandle = uint32_t;

struct SKSEMessagingInterface {
    uint32_t interfaceVersion;
    bool (*RegisterListener)(PluginHandle listener, const char* sender, EventCallback handler);
    bool (*Dispatch)(PluginHandle sender, uint32_t messageType, void* data, uint32_t dataLen, const char* receiver);
    void* (*GetEventDispatcher)(uint32_t dispatcherId);
};

struct SKSEInterface {
    uint32_t skseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
    void* (*QueryInterface)(uint32_t id);
    uint32_t (*GetPluginHandle)();
    uint32_t (*GetReleaseIndex)();
};

constexpr uint32_t kSKSE_DataLoaded = 8;

// ── Globals ─────────────────────────────────────────────────────────

static mora::harness::TcpListener* g_listener = nullptr;
static mora::harness::HarnessConfig g_config;

static const mora::rt::BSTHashMapLayout* g_all_forms = nullptr;

static void resolve_all_forms() {
    void* base = GetModuleHandleW(nullptr);
    if (!base) return;

    // Hardcoded AE 1.6.1170 offset for allForms (Address Library ID 400507)
    // TODO: load Address Library at runtime for version independence
    constexpr uint64_t kAllFormsOffset_AE = 0x20FBB88;

    auto* ptr = reinterpret_cast<const void* const*>(
        static_cast<const char*>(base) + kAllFormsOffset_AE);
    g_all_forms = static_cast<const mora::rt::BSTHashMapLayout*>(*ptr);
}

// ── Command handlers ────────────────────────────────────────────────

static std::string handle_status(const std::string&) {
    bool ready = (g_all_forms != nullptr);
    std::ostringstream ss;
    ss << R"({"ok":true,"forms_loaded":)" << (ready ? "true" : "false");
    if (ready) {
        ss << R"(,"capacity":)" << g_all_forms->capacity;
        ss << R"(,"entries_ptr":"0x)" << std::hex
           << reinterpret_cast<uintptr_t>(g_all_forms->entries) << "\"";
        ss << R"(,"sentinel_ptr":"0x)"
           << reinterpret_cast<uintptr_t>(g_all_forms->sentinel) << "\"";
    }
    ss << "}";
    return ss.str();
}

static std::string handle_dump_weapons(const std::string&) {
    if (!g_all_forms) {
        return R"({"ok":false,"error":"forms not loaded"})";
    }

    std::vector<mora::harness::WeaponData> weapons;

    for (uint32_t i = 0; i < g_all_forms->capacity; i++) {
        auto* entry = &g_all_forms->entries[i];
        if (entry->next == nullptr) continue;

        while (entry != g_all_forms->sentinel) {
            if (entry->value) {
                uint8_t form_type = mora::rt::get_form_type(entry->value);
                if (form_type == 0x29) { // Weapon
                    mora::harness::WeaponData data;
                    mora::harness::read_weapon_fields(entry->value, data);
                    weapons.push_back(std::move(data));
                }
            }
            entry = entry->next;
            if (!entry) break;
        }
    }

    fs::path dump_dir = g_config.dump_path;
    fs::create_directories(dump_dir);
    fs::path dump_file = dump_dir / "weapons.jsonl";

    {
        std::ofstream out(dump_file);
        mora::harness::write_weapons_jsonl(weapons, out);
    }

    return std::string(R"({"ok":true,"file":")") + dump_file.string() +
           R"(","count":)" + std::to_string(weapons.size()) + "}";
}

static std::string handle_quit(const std::string&) {
    return R"({"ok":true})";
}

// ── SKSE entry points ───────────────────────────────────────────────

static void message_handler(SKSEMessage* msg) {
    if (msg->type != kSKSE_DataLoaded) return;

    resolve_all_forms();

    g_config.port = 9742;
    g_config.dump_path = "Data/MoraCache/dumps";

    g_listener = new mora::harness::TcpListener(g_config.port);
    g_listener->on("status", handle_status);
    g_listener->on("dump weapons", handle_dump_weapons);
    g_listener->on("quit", handle_quit);
    g_listener->start();
}

extern "C" __declspec(dllexport)
SKSEPluginVersionData SKSEPlugin_Version = {
    1, 1,
    "MoraTestHarness",
    "Mora Project",
    "",         // supportEmail
    0,          // versionIndependenceEx
    1 | 4,      // versionIndependence: AddressLibraryPostAE | StructsPost629
    {0}, 0
};

extern "C" __declspec(dllexport)
int __stdcall DllMain(void* hinstDLL, uint32_t fdwReason, void* lpvReserved) {
    (void)hinstDLL; (void)fdwReason; (void)lpvReserved;
    return 1;
}

static uint32_t g_plugin_handle = 0;

extern "C" __declspec(dllexport)
bool SKSEPlugin_Load(SKSEInterface* skse) {
    g_plugin_handle = skse->GetPluginHandle();
    constexpr uint32_t kInterface_Messaging = 5;
    auto* messaging = static_cast<SKSEMessagingInterface*>(skse->QueryInterface(kInterface_Messaging));
    if (!messaging) return true;

    messaging->RegisterListener(g_plugin_handle, "SKSE", message_handler);
    return true;
}

#else
namespace mora::harness { void harness_plugin_stub() {} }
#endif
