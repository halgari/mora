#ifdef _WIN32

#include "mora/harness/tcp_listener.h"
#include "mora/harness/weapon_dumper.h"
#include "mora/harness/ini_reader.h"
#include "mora/rt/bst_hashmap.h"
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

// Debug: hash-based lookup matching bst_hashmap_lookup logic exactly
static void* debug_lookup(uint32_t formid) {
    if (!g_all_forms || !g_all_forms->entries || g_all_forms->capacity == 0)
        return nullptr;

    // BSCRC32: init=0, no final XOR
    static const uint32_t crc_table[] = {
        0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,
        0x0EDB8832,0x79DCB8A4,0xE0D5E91E,0x97D2D988,0x09B64C2B,0x7EB17CBD,0xE7B82D07,0x90BF1D91,
        0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
        0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,
        0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
        0x35B5A8FA,0x42B2986C,0xDBBBC9D6,0xACBCF940,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
        0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F4B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
        0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,
        0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
        0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0DBB,0x086D3D2D,0x91646C97,0xE6635C01,
        0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
        0x65B0D9C6,0x12B7E950,0x8BBEB8EA,0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
        0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,0xA4D1C46D,0xD3D6F4FB,
        0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7CC9,
        0x5005713C,0x270241AA,0xBE0B1010,0xC90C2086,0x5768B525,0x206F85B3,0xB966D409,0xCE61E49F,
        0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,0x59B33D17,0x2EB40D81,0xB7BD5C3B,0xC0BA6CAD,
        0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,0xEAD54739,0x9DD277AF,0x04DB2615,0x73DC1683,
        0xE3630B12,0x94643B84,0x0D6D6A3E,0x7A6A5AA8,0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,
        0xF00F9344,0x8708A3D2,0x1E01F268,0x6906C2FE,0xF762575D,0x806567CB,0x196C3671,0x6E6B06E7,
        0xFED41B76,0x89D32BE0,0x10DA7A5A,0x67DD4ACC,0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,
        0xD6D6A3E8,0xA1D1937E,0x38D8C2C4,0x4FDFF252,0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
        0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,0xDF60EFC3,0xA867DF55,0x316E8EEF,0x4669BE79,
        0xCB61B38C,0xBC66831A,0x256FD2A0,0x5268E236,0xCC0C7795,0xBB0B4703,0x220216B9,0x5505262F,
        0xC5BA3BBE,0xB2BD0B28,0x2BB45A92,0x5CB36A04,0xC2D7FFA7,0xB5D0CF31,0x2CD99E8B,0x5BDEAE1D,
        0x9B64C2B0,0xEC63F226,0x756AA39C,0x026D930A,0x9C0906A9,0xEB0E363F,0x72076785,0x05005713,
        0x95BF4A82,0xE2B87A14,0x7BB12BAE,0x0CB61B38,0x92D28E9B,0xE5D5BE0D,0x7CDCEFB7,0x0BDBDF21,
        0x86D3D2D4,0xF1D4E242,0x68DDB3F8,0x1FDA836E,0x81BE16CD,0xF6B9265B,0x6FB077E1,0x18B74777,
        0x88085AE6,0xFF0F6A70,0x66063BCA,0x11010B5C,0x8F659EFF,0xF862AE69,0x616BFFD3,0x166CCF45,
        0xA00AE278,0xD70DD2EE,0x4E048354,0x3903B3C2,0xA7672661,0xD06016F7,0x4969474D,0x3E6E77DB,
        0xAED16A4A,0xD9D65ADC,0x40DF0B66,0x37D83BF0,0xA9BCAE53,0xDEBB9EC5,0x47B2CF7F,0x30B5FFE9,
        0xBDBDF21C,0xCABAC28A,0x53B39330,0x24B4A3A6,0xBAD03605,0xCDD70693,0x54DE5729,0x23D967BF,
        0xB3667A2E,0xC4614AB8,0x5D681B02,0x2A6F2B94,0xB40BBE37,0xC30C8EA1,0x5A05DF1B,0x2D02EF8D,
    };
    uint32_t hash = 0;
    auto* bytes = reinterpret_cast<const uint8_t*>(&formid);
    for (int i = 0; i < 4; i++)
        hash = (hash >> 8) ^ crc_table[(hash ^ bytes[i]) & 0xFF];

    uint32_t idx = hash & (g_all_forms->capacity - 1);
    auto* entry = &g_all_forms->entries[idx];

    if (entry->next == nullptr) return nullptr;

    do {
        if (entry->key == formid) return entry->value;
        entry = entry->next;
    } while (entry != g_all_forms->sentinel);

    return nullptr;
}

static std::string handle_lookup(const std::string& cmd) {
    // "lookup 0x00012EB7"
    uint32_t formid = 0;
    if (cmd.size() > 7) {
        formid = static_cast<uint32_t>(std::stoul(cmd.substr(7), nullptr, 0));
    }
    if (formid == 0) return R"({"ok":false,"error":"usage: lookup 0xFORMID"})";

    void* form = debug_lookup(formid);
    std::ostringstream ss;
    ss << R"({"ok":true,"formid":"0x)" << std::hex << std::setfill('0') << std::setw(8) << formid << "\"";
    ss << R"(,"found":)" << (form ? "true" : "false");
    if (form) {
        uint8_t form_type = mora::rt::get_form_type(form);
        ss << R"(,"form_type":"0x)" << std::hex << std::setw(2) << (int)form_type << "\"";
        // Read formID from the form itself to verify
        uint32_t actual_id;
        std::memcpy(&actual_id, static_cast<const char*>(form) + 0x14, sizeof(actual_id));
        ss << R"(,"actual_formid":"0x)" << std::hex << std::setw(8) << actual_id << "\"";

        // Read TESFullName if this is an NPC (0x2B) or Weapon (0x29) or Armor (0x1A)
        size_t name_offset = 0;
        if (form_type == 0x2B) name_offset = 0x0D8 + 0x08;  // NPC
        else if (form_type == 0x29) name_offset = 0x030 + 0x08;  // Weapon
        else if (form_type == 0x1A) name_offset = 0x030 + 0x08;  // Armor
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

    resolve_all_forms();

    g_config.port = 9742;
    g_config.dump_path = "Data/MoraCache/dumps";

    g_listener = new mora::harness::TcpListener(g_config.port);
    g_listener->on("status", handle_status);
    g_listener->on("dump weapons", handle_dump_weapons);
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
namespace mora::harness { void harness_plugin_stub() {} }
#endif
