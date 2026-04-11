#ifdef _WIN32

#include "mora/harness/tcp_listener.h"
#include "mora/harness/weapon_dumper.h"
#include "mora/harness/ini_reader.h"
#include "mora/rt/bst_hashmap.h"
#include "mora/rt/form_ops.h"

#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ── SKSE types (same as plugin_entry.cpp) ───────────────────────────

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

struct SKSEMessage {
    const char* sender;
    uint32_t type;
    uint32_t dataLen;
    void* data;
};

using SKSEMessageCallback = void(*)(SKSEMessage*);

struct SKSEMessagingInterface {
    uint32_t interfaceVersion;
    bool (*RegisterListener)(void* plugin, const char* sender, SKSEMessageCallback callback);
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

    constexpr uint64_t kAllFormsOffset = 0x1EEBE10;

    auto* ptr = reinterpret_cast<const void* const*>(
        static_cast<const char*>(base) + kAllFormsOffset);
    g_all_forms = static_cast<const mora::rt::BSTHashMapLayout*>(*ptr);
}

// ── Command handlers ────────────────────────────────────────────────

static std::string handle_status(const std::string&) {
    bool ready = (g_all_forms != nullptr);
    return std::string(R"({"ok":true,"forms_loaded":)") +
           (ready ? "true" : "false") + "}";
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

    char dll_path[MAX_PATH];
    HMODULE hm = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&message_handler), &hm);
    GetModuleFileNameA(hm, dll_path, MAX_PATH);
    fs::path ini_path = fs::path(dll_path).parent_path() / "MoraTestHarness.ini";

    g_config = mora::harness::read_ini(ini_path);

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
    1, 2,
    {0}, 0
};

extern "C" __declspec(dllexport)
int __stdcall DllMain(void* hinstDLL, uint32_t fdwReason, void* lpvReserved) {
    (void)hinstDLL; (void)fdwReason; (void)lpvReserved;
    return 1;
}

extern "C" __declspec(dllexport)
bool SKSEPlugin_Load(SKSEInterface* skse) {
    auto* messaging = static_cast<SKSEMessagingInterface*>(skse->QueryInterface(2));
    if (messaging) {
        messaging->RegisterListener(nullptr, "SKSE", message_handler);
    }
    return true;
}

#else
namespace mora::harness { void harness_plugin_stub() {} }
#endif
