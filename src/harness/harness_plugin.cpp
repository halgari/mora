#ifdef _WIN32

#include "mora/harness/tcp_listener.h"
#include "mora/harness/weapon_dumper.h"
#include "mora/harness/npc_dumper.h"
#include "mora/harness/ini_reader.h"
#include "mora/rt/form_ops.h"
#include "mora/codegen/address_library.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
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
static bool g_data_loaded = false;

// ── Command handlers ────────────────────────────────────────────────

static std::string handle_status(const std::string&) {
    std::ostringstream ss;
    ss << R"({"ok":true,"forms_loaded":)" << (g_data_loaded ? "true" : "false") << "}";
    return ss.str();
}

static void weapon_collector(void* form, void* ctx) {
    auto* out = static_cast<std::vector<mora::harness::WeaponData>*>(ctx);
    mora::harness::WeaponData data;
    mora::harness::read_weapon_fields(form, data);
    out->push_back(std::move(data));
}

static std::string handle_dump_weapons(const std::string&) {
    if (!g_data_loaded) return R"({"ok":false,"error":"forms not loaded"})";

    std::vector<mora::harness::WeaponData> weapons;
    mora::rt::for_each_form_of_type(0x29, weapon_collector, &weapons);

    fs::path dump_dir = g_config.dump_path;
    fs::create_directories(dump_dir);
    fs::path dump_file = dump_dir / "weapons.jsonl";
    {
        std::ofstream out(dump_file);
        mora::harness::write_weapons_jsonl(weapons, out);
    }
    // generic_string() forces forward-slashes so the JSON value is valid
    // under JSON escape rules (backslashes in a path would otherwise read
    // as escape sequences — e.g. \n becomes a newline).
    return std::string(R"({"ok":true,"file":")") + dump_file.generic_string() +
           R"(","count":)" + std::to_string(weapons.size()) + "}";
}

static void npc_collector(void* form, void* ctx) {
    auto* out = static_cast<std::vector<mora::harness::NpcData>*>(ctx);
    mora::harness::NpcData data;
    mora::harness::read_npc_fields(form, data);
    out->push_back(std::move(data));
}

static std::string handle_dump_npcs(const std::string&) {
    if (!g_data_loaded) return R"({"ok":false,"error":"forms not loaded"})";

    std::vector<mora::harness::NpcData> npcs;
    mora::rt::for_each_form_of_type(0x2B, npc_collector, &npcs);

    fs::path dump_dir = g_config.dump_path;
    fs::create_directories(dump_dir);
    fs::path dump_file = dump_dir / "npcs.jsonl";
    {
        std::ofstream out(dump_file);
        mora::harness::write_npcs_jsonl(npcs, out);
    }
    return std::string(R"({"ok":true,"file":")") + dump_file.generic_string() +
           R"(","count":)" + std::to_string(npcs.size()) + "}";
}

static std::string handle_lookup(const std::string& cmd) {
    uint32_t formid = 0;
    if (cmd.size() > 7) {
        try {
            formid = static_cast<uint32_t>(std::stoul(cmd.substr(7), nullptr, 0));
        } catch (...) { formid = 0; }
    }
    if (formid == 0) return R"({"ok":false,"error":"usage: lookup 0xFORMID"})";

    void* form = mora::rt::lookup_form_by_id(formid);
    std::ostringstream ss;
    ss << R"({"ok":true,"formid":"0x)" << std::hex << std::setfill('0') << std::setw(8) << formid << "\"";
    ss << R"(,"found":)" << (form ? "true" : "false");
    if (form) {
        uint8_t form_type = mora::rt::get_form_type(form);
        ss << R"(,"form_type":"0x)" << std::hex << std::setw(2) << (int)form_type << "\"";
        size_t name_offset = 0;
        if (form_type == 0x2B) name_offset = 0x0D8 + 0x08;
        else if (form_type == 0x29) name_offset = 0x030 + 0x08;
        else if (form_type == 0x1A) name_offset = 0x030 + 0x08;
        if (name_offset > 0) {
            const char* name_ptr = nullptr;
            std::memcpy(&name_ptr, static_cast<const char*>(form) + name_offset, sizeof(name_ptr));
            if (name_ptr) {
                ss << R"(,"name":")" << name_ptr << "\"";
            }
        }
    }
    ss << "}";
    return ss.str();
}


static std::string handle_quit(const std::string&) {
    return R"({"ok":true})";
}

// ── SKSE entry points ───────────────────────────────────────────────

static void message_handler(SKSEMessage* msg) {
    if (msg->type != kSKSE_DataLoaded) return;

    g_data_loaded = true;

    g_config.port = 9742;
    g_config.dump_path = "Data/MoraCache/dumps";

    g_listener = new mora::harness::TcpListener(g_config.port);
    g_listener->on("status", handle_status);
    g_listener->on("dump weapons", handle_dump_weapons);
    g_listener->on("dump npcs", handle_dump_npcs);
    g_listener->on("lookup", handle_lookup);
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
namespace mora::harness { [[maybe_unused]] static void harness_plugin_stub() {} }
#endif
