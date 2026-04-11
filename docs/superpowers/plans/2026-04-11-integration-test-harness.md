# Integration Test Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a standalone SKSE test harness DLL with TCP command interface that dumps weapon form data from Skyrim's runtime, plus a Python test driver for snapshot comparison.

**Architecture:** Refactor `mora_rt` from raw `.bc` bitcode into a `.lib` static library (still LTO-capable). Build `MoraTestHarness.dll` as a standalone SKSE plugin linking that `.lib`. A Python script (`tools/test_harness.py`) connects via TCP to issue dump commands and diff snapshots.

**Tech Stack:** C++20 (clang-cl cross-compile), Winsock2, JSONL, Python 3, lld-link with LTO

**Spec:** `docs/superpowers/specs/2026-04-11-integration-test-harness-design.md`

---

## File Map

**New files:**
- `include/mora/harness/tcp_listener.h` — TCP server: accept connections, parse commands, dispatch handlers
- `include/mora/harness/weapon_dumper.h` — Read weapon form fields from memory, write JSONL
- `include/mora/harness/ini_reader.h` — Parse `MoraTestHarness.ini` for port/dump_path config
- `src/harness/tcp_listener.cpp` — TCP listener implementation (Winsock2)
- `src/harness/weapon_dumper.cpp` — Weapon dumper implementation
- `src/harness/ini_reader.cpp` — INI reader implementation
- `src/harness/harness_plugin.cpp` — SKSE entry point for MoraTestHarness.dll
- `scripts/build_rt_lib.sh` — Build `mora_rt.lib` (replaces `build_rt_bitcode.sh`)
- `scripts/build_test_harness.sh` — Build `MoraTestHarness.dll`
- `tools/test_harness.py` — Python test driver (capture, compare)
- `tests/ini_reader_test.cpp` — Tests for INI parsing
- `tests/weapon_dumper_test.cpp` — Tests for JSONL serialization (mock form data)

**Modified files:**
- `scripts/build_rt_bitcode.sh` — Replaced by `build_rt_lib.sh`
- `src/codegen/dll_builder.cpp` — Remove `lto_merge()` usage, link against `mora_rt.lib` instead
- `include/mora/codegen/dll_builder.h` — Remove `lto_merge()` declaration, add lib path param
- `src/main.cpp` — Pass `mora_rt.lib` path instead of `mora_rt.bc` path
- `xmake.lua` — Add `mora_test_harness` target (Windows), update `mora_rt` references

---

### Task 1: INI Reader

A minimal INI parser that reads `[General]` section key-value pairs. No dependency on Windows APIs so it's testable on Linux.

**Files:**
- Create: `include/mora/harness/ini_reader.h`
- Create: `src/harness/ini_reader.cpp`
- Create: `tests/ini_reader_test.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/ini_reader_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "mora/harness/ini_reader.h"
#include <fstream>
#include <filesystem>

TEST(IniReaderTest, ParsePortAndDumpPath) {
    auto path = std::filesystem::temp_directory_path() / "test_harness.ini";
    {
        std::ofstream f(path);
        f << "[General]\n"
          << "port=9742\n"
          << "dump_path=Data/MoraCache/dumps\n";
    }

    auto config = mora::harness::read_ini(path);
    EXPECT_EQ(config.port, 9742);
    EXPECT_EQ(config.dump_path, "Data/MoraCache/dumps");
    std::filesystem::remove(path);
}

TEST(IniReaderTest, DefaultsWhenMissing) {
    auto path = std::filesystem::temp_directory_path() / "empty_harness.ini";
    {
        std::ofstream f(path);
        f << "[General]\n";
    }

    auto config = mora::harness::read_ini(path);
    EXPECT_EQ(config.port, 9742);
    EXPECT_EQ(config.dump_path, "Data/MoraCache/dumps");
    std::filesystem::remove(path);
}

TEST(IniReaderTest, DefaultsWhenFileNotFound) {
    auto config = mora::harness::read_ini("/tmp/nonexistent_harness.ini");
    EXPECT_EQ(config.port, 9742);
    EXPECT_EQ(config.dump_path, "Data/MoraCache/dumps");
}

TEST(IniReaderTest, IgnoresComments) {
    auto path = std::filesystem::temp_directory_path() / "comment_harness.ini";
    {
        std::ofstream f(path);
        f << "; this is a comment\n"
          << "[General]\n"
          << "# another comment\n"
          << "port=1234\n";
    }

    auto config = mora::harness::read_ini(path);
    EXPECT_EQ(config.port, 1234);
    std::filesystem::remove(path);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build ini_reader_test && xmake run ini_reader_test`
Expected: Build fails — header doesn't exist yet.

- [ ] **Step 3: Write the header**

Create `include/mora/harness/ini_reader.h`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace mora::harness {

struct HarnessConfig {
    uint16_t port = 9742;
    std::string dump_path = "Data/MoraCache/dumps";
};

HarnessConfig read_ini(const std::filesystem::path& path);

} // namespace mora::harness
```

- [ ] **Step 4: Write the implementation**

Create `src/harness/ini_reader.cpp`:

```cpp
#include "mora/harness/ini_reader.h"
#include <fstream>
#include <string>

namespace mora::harness {

HarnessConfig read_ini(const std::filesystem::path& path) {
    HarnessConfig config;

    std::ifstream file(path);
    if (!file.is_open()) return config;

    std::string line;
    while (std::getline(file, line)) {
        // Trim leading whitespace
        auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        // Skip comments and section headers
        if (line[0] == ';' || line[0] == '#' || line[0] == '[') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        auto key = line.substr(0, eq);
        auto value = line.substr(eq + 1);

        // Trim trailing whitespace/CR from value
        auto end = value.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) value = value.substr(0, end + 1);

        if (key == "port") {
            config.port = static_cast<uint16_t>(std::stoi(value));
        } else if (key == "dump_path") {
            config.dump_path = value;
        }
    }

    return config;
}

} // namespace mora::harness
```

- [ ] **Step 5: Add to xmake.lua and run tests**

Add `src/harness/*.cpp` to the `mora_lib` target's `add_files` line in `xmake.lua`:

```lua
add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
          "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
          "src/cli/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp",
          "src/data/*.cpp", "src/esp/*.cpp", "src/import/*.cpp",
          "src/codegen/*.cpp", "src/rt/*.cpp", "src/harness/*.cpp")
```

Run: `xmake build ini_reader_test && xmake run ini_reader_test`
Expected: All 4 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add include/mora/harness/ini_reader.h src/harness/ini_reader.cpp tests/ini_reader_test.cpp xmake.lua
git commit -m "feat: INI reader for test harness configuration"
```

---

### Task 2: Weapon Dumper (JSONL Serializer)

Reads weapon form fields from memory and writes sorted JSONL. Testable on Linux using mock form data (raw byte buffers matching Skyrim's weapon layout).

**Files:**
- Create: `include/mora/harness/weapon_dumper.h`
- Create: `src/harness/weapon_dumper.cpp`
- Create: `tests/weapon_dumper_test.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/weapon_dumper_test.cpp`. This test constructs a fake weapon form as a byte buffer matching `skyrim_abi.h` weapon offsets, then verifies the dumper reads correct values and serializes to JSONL.

```cpp
#include <gtest/gtest.h>
#include "mora/harness/weapon_dumper.h"
#include <cstring>
#include <sstream>

// Build a fake weapon form matching skyrim_abi.h weapon_offsets
// total_size = 0x220
static std::vector<uint8_t> make_fake_weapon(uint32_t formid, uint16_t damage,
                                              int32_t value, float weight) {
    std::vector<uint8_t> buf(0x220, 0);

    // TESForm.formID at 0x14
    std::memcpy(buf.data() + 0x14, &formid, sizeof(formid));
    // TESForm.formType at 0x1A — Weapon = 0x29
    buf[0x1A] = 0x29;

    // TESAttackDamageForm at 0x0C0, member at +0x08
    std::memcpy(buf.data() + 0x0C0 + 0x08, &damage, sizeof(damage));
    // TESValueForm at 0x0A0, member at +0x08
    std::memcpy(buf.data() + 0x0A0 + 0x08, &value, sizeof(value));
    // TESWeightForm at 0x0B0, member at +0x08
    std::memcpy(buf.data() + 0x0B0 + 0x08, &weight, sizeof(weight));

    // BGSKeywordForm at 0x140: keywords=nullptr, numKeywords=0
    // (already zeroed)

    return buf;
}

TEST(WeaponDumperTest, SerializeSingleWeapon) {
    auto buf = make_fake_weapon(0x00012EB7, 7, 25, 9.0f);

    mora::harness::WeaponData data;
    mora::harness::read_weapon_fields(buf.data(), data);

    EXPECT_EQ(data.formid, 0x00012EB7u);
    EXPECT_EQ(data.damage, 7);
    EXPECT_EQ(data.value, 25);
    EXPECT_FLOAT_EQ(data.weight, 9.0f);
    EXPECT_TRUE(data.keyword_formids.empty());
}

TEST(WeaponDumperTest, JsonlFormat) {
    mora::harness::WeaponData data;
    data.formid = 0x00012EB7;
    data.name = "Iron Sword";
    data.damage = 7;
    data.value = 25;
    data.weight = 9.0f;
    data.keyword_formids = {0x0006BBE3, 0x0001E711};

    std::string line = mora::harness::weapon_to_jsonl(data);

    // Keywords should be sorted
    EXPECT_NE(line.find("\"formid\":\"0x00012EB7\""), std::string::npos);
    EXPECT_NE(line.find("\"damage\":7"), std::string::npos);
    EXPECT_NE(line.find("\"value\":25"), std::string::npos);
    EXPECT_NE(line.find("\"name\":\"Iron Sword\""), std::string::npos);
    // 0x0001E711 should appear before 0x0006BBE3 (sorted)
    auto pos1 = line.find("0x0001E711");
    auto pos2 = line.find("0x0006BBE3");
    EXPECT_LT(pos1, pos2);
}

TEST(WeaponDumperTest, SortedByFormId) {
    std::vector<mora::harness::WeaponData> weapons;

    mora::harness::WeaponData w1;
    w1.formid = 0x200;
    w1.damage = 10; w1.value = 50; w1.weight = 5.0f;
    weapons.push_back(w1);

    mora::harness::WeaponData w2;
    w2.formid = 0x100;
    w2.damage = 20; w2.value = 100; w2.weight = 10.0f;
    weapons.push_back(w2);

    std::stringstream ss;
    mora::harness::write_weapons_jsonl(weapons, ss);

    std::string line1, line2;
    std::getline(ss, line1);
    std::getline(ss, line2);

    // 0x100 should come first
    EXPECT_NE(line1.find("\"formid\":\"0x00000100\""), std::string::npos);
    EXPECT_NE(line2.find("\"formid\":\"0x00000200\""), std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build weapon_dumper_test && xmake run weapon_dumper_test`
Expected: Build fails — header doesn't exist.

- [ ] **Step 3: Write the header**

Create `include/mora/harness/weapon_dumper.h`:

```cpp
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace mora::harness {

struct WeaponData {
    uint32_t formid = 0;
    std::string name;
    uint16_t damage = 0;
    int32_t value = 0;
    float weight = 0.0f;
    std::vector<uint32_t> keyword_formids;
};

// Read weapon fields from a raw form pointer (Skyrim memory layout).
// form must point to the start of a TESObjectWEAP (0x220 bytes).
void read_weapon_fields(const void* form, WeaponData& out);

// Serialize a single WeaponData to a JSON line (no trailing newline).
std::string weapon_to_jsonl(const WeaponData& data);

// Write a vector of WeaponData as sorted JSONL to an output stream.
// Sorts by formid before writing.
void write_weapons_jsonl(std::vector<WeaponData>& weapons, std::ostream& out);

} // namespace mora::harness
```

- [ ] **Step 4: Write the implementation**

Create `src/harness/weapon_dumper.cpp`:

```cpp
#include "mora/harness/weapon_dumper.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace mora::harness {

// Offsets from skyrim_abi.h weapon_offsets
static constexpr size_t kFormIdOffset      = 0x14;
static constexpr size_t kFullNameOffset    = 0x030 + 0x08; // TESFullName.fullName
static constexpr size_t kValueOffset       = 0x0A0 + 0x08; // TESValueForm.value
static constexpr size_t kWeightOffset      = 0x0B0 + 0x08; // TESWeightForm.weight
static constexpr size_t kDamageOffset      = 0x0C0 + 0x08; // TESAttackDamageForm.attackDamage
static constexpr size_t kKeywordsArrayOffset = 0x140 + 0x08; // BGSKeywordForm.keywords
static constexpr size_t kKeywordsCountOffset = 0x140 + 0x10; // BGSKeywordForm.numKeywords

void read_weapon_fields(const void* form, WeaponData& out) {
    auto base = static_cast<const char*>(form);

    std::memcpy(&out.formid, base + kFormIdOffset, sizeof(out.formid));
    std::memcpy(&out.damage, base + kDamageOffset, sizeof(out.damage));
    std::memcpy(&out.value,  base + kValueOffset,  sizeof(out.value));
    std::memcpy(&out.weight, base + kWeightOffset, sizeof(out.weight));

    // Name: BSFixedString is a pointer to string data. In test mocks this is
    // nullptr; at runtime we read through the pointer.
    void* name_ptr = nullptr;
    std::memcpy(&name_ptr, base + kFullNameOffset, sizeof(name_ptr));
    if (name_ptr) {
        // BSFixedString: the char* is at the pointed-to location
        // In Skyrim's memory, BSFixedString stores a pointer to the string data.
        out.name = static_cast<const char*>(name_ptr);
    }

    // Keywords: read pointer to array and count
    void* kw_array = nullptr;
    uint32_t kw_count = 0;
    std::memcpy(&kw_array, base + kKeywordsArrayOffset, sizeof(kw_array));
    std::memcpy(&kw_count, base + kKeywordsCountOffset, sizeof(kw_count));

    out.keyword_formids.clear();
    if (kw_array && kw_count > 0) {
        auto** keywords = static_cast<const char**>(kw_array);
        for (uint32_t i = 0; i < kw_count; i++) {
            // Each BGSKeyword* — formID is at offset 0x14 in TESForm
            if (keywords[i]) {
                uint32_t kw_formid;
                std::memcpy(&kw_formid, keywords[i] + 0x14, sizeof(kw_formid));
                out.keyword_formids.push_back(kw_formid);
            }
        }
    }
}

static std::string format_hex(uint32_t val) {
    std::ostringstream ss;
    ss << "0x" << std::setfill('0') << std::setw(8) << std::uppercase << std::hex << val;
    return ss.str();
}

static std::string escape_json_string(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string weapon_to_jsonl(const WeaponData& data) {
    std::ostringstream ss;
    ss << "{\"formid\":\"" << format_hex(data.formid) << "\"";
    ss << ",\"name\":\"" << escape_json_string(data.name) << "\"";
    ss << ",\"damage\":" << data.damage;
    ss << ",\"value\":" << data.value;
    ss << ",\"weight\":" << data.weight;

    ss << ",\"keywords\":[";
    auto sorted_kw = data.keyword_formids;
    std::sort(sorted_kw.begin(), sorted_kw.end());
    for (size_t i = 0; i < sorted_kw.size(); i++) {
        if (i > 0) ss << ",";
        ss << "\"" << format_hex(sorted_kw[i]) << "\"";
    }
    ss << "]}";

    return ss.str();
}

void write_weapons_jsonl(std::vector<WeaponData>& weapons, std::ostream& out) {
    std::sort(weapons.begin(), weapons.end(),
              [](const WeaponData& a, const WeaponData& b) {
                  return a.formid < b.formid;
              });

    for (const auto& w : weapons) {
        out << weapon_to_jsonl(w) << "\n";
    }
}

} // namespace mora::harness
```

- [ ] **Step 5: Run tests**

Run: `xmake build weapon_dumper_test && xmake run weapon_dumper_test`
Expected: All 3 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add include/mora/harness/weapon_dumper.h src/harness/weapon_dumper.cpp tests/weapon_dumper_test.cpp
git commit -m "feat: weapon form dumper with JSONL serialization"
```

---

### Task 3: TCP Listener

A Winsock2-based TCP server that listens on a configurable port, accepts one connection at a time, reads newline-delimited commands, and dispatches to registered handlers. This is Windows-only code — tests will be limited to interface verification on Linux.

**Files:**
- Create: `include/mora/harness/tcp_listener.h`
- Create: `src/harness/tcp_listener.cpp`

- [ ] **Step 1: Write the header**

Create `include/mora/harness/tcp_listener.h`:

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace mora::harness {

// Command handler: receives the full command line (e.g. "dump weapons"),
// returns a JSON response string.
using CommandHandler = std::function<std::string(const std::string& command)>;

class TcpListener {
public:
    explicit TcpListener(uint16_t port);
    ~TcpListener();

    // Register a handler for a command prefix (e.g. "dump", "status", "quit").
    void on(const std::string& prefix, CommandHandler handler);

    // Start listening in a background thread. Non-blocking.
    bool start();

    // Stop the listener and close all sockets.
    void stop();

    bool running() const { return running_; }

private:
    void listen_loop();
    void handle_connection(void* client_socket);
    std::string dispatch(const std::string& command);

    uint16_t port_;
    bool running_ = false;

#ifdef _WIN32
    void* listen_socket_ = nullptr; // SOCKET, stored as void* to avoid winsock header leak
    void* thread_ = nullptr;        // std::thread*, stored opaque
#endif

    struct HandlerEntry {
        std::string prefix;
        CommandHandler handler;
    };
    std::vector<HandlerEntry> handlers_;
};

} // namespace mora::harness
```

- [ ] **Step 2: Write the implementation**

Create `src/harness/tcp_listener.cpp`:

```cpp
#include "mora/harness/tcp_listener.h"

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace mora::harness {

TcpListener::TcpListener(uint16_t port) : port_(port) {}

TcpListener::~TcpListener() {
    stop();
}

void TcpListener::on(const std::string& prefix, CommandHandler handler) {
    handlers_.push_back({prefix, std::move(handler)});
}

bool TcpListener::start() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) { WSACleanup(); return false; }

    // Allow port reuse
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // localhost only
    addr.sin_port = htons(port_);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return false;
    }

    if (listen(sock, 1) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return false;
    }

    listen_socket_ = reinterpret_cast<void*>(sock);
    running_ = true;
    thread_ = new std::thread(&TcpListener::listen_loop, this);

    return true;
}

void TcpListener::stop() {
    running_ = false;
    if (listen_socket_) {
        closesocket(reinterpret_cast<SOCKET>(listen_socket_));
        listen_socket_ = nullptr;
    }
    if (thread_) {
        auto* t = static_cast<std::thread*>(thread_);
        if (t->joinable()) t->join();
        delete t;
        thread_ = nullptr;
    }
    WSACleanup();
}

void TcpListener::listen_loop() {
    SOCKET sock = reinterpret_cast<SOCKET>(listen_socket_);
    while (running_) {
        // Use select with timeout so we can check running_ periodically
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        timeval tv{1, 0}; // 1 second timeout

        int ready = select(0, &fds, nullptr, nullptr, &tv);
        if (ready <= 0) continue;

        SOCKET client = accept(sock, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;

        handle_connection(reinterpret_cast<void*>(client));
    }
}

void TcpListener::handle_connection(void* client_socket) {
    SOCKET client = reinterpret_cast<SOCKET>(client_socket);
    std::string buffer;
    char chunk[1024];

    while (running_) {
        int n = recv(client, chunk, sizeof(chunk), 0);
        if (n <= 0) break;

        buffer.append(chunk, n);

        // Process complete lines
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            // Trim \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            std::string response = dispatch(line) + "\n";
            send(client, response.c_str(), static_cast<int>(response.size()), 0);

            // Check if handler signaled quit
            if (line == "quit") {
                closesocket(client);
                running_ = false;
                return;
            }
        }
    }

    closesocket(client);
}

std::string TcpListener::dispatch(const std::string& command) {
    for (const auto& entry : handlers_) {
        if (command == entry.prefix ||
            command.substr(0, entry.prefix.size() + 1) == entry.prefix + " ") {
            return entry.handler(command);
        }
    }
    return R"({"ok":false,"error":"unknown command"})";
}

} // namespace mora::harness

#else
// Linux stub — TCP listener is Windows-only (runs inside Skyrim)
namespace mora::harness {

TcpListener::TcpListener(uint16_t) {}
TcpListener::~TcpListener() {}
void TcpListener::on(const std::string&, CommandHandler) {}
bool TcpListener::start() { return false; }
void TcpListener::stop() {}

} // namespace mora::harness
#endif
```

- [ ] **Step 3: Verify it compiles on Linux**

Run: `xmake build mora_lib`
Expected: Compiles (Linux stub is used).

- [ ] **Step 4: Commit**

```bash
git add include/mora/harness/tcp_listener.h src/harness/tcp_listener.cpp
git commit -m "feat: TCP command listener for test harness (Winsock2)"
```

---

### Task 4: Harness SKSE Plugin Entry Point

The `MoraTestHarness.dll` entry point. On DataLoaded, reads INI config, wires up command handlers (status, dump weapons, quit), and starts the TCP listener.

**Files:**
- Create: `src/harness/harness_plugin.cpp`

- [ ] **Step 1: Write the plugin entry**

Create `src/harness/harness_plugin.cpp`:

```cpp
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

// ── Address Library form map resolution ─────────────────────────────
// At runtime, resolve the global allForms BSTHashMap.
// Address Library ID 514351 → offset from SkyrimSE.exe base.
// For now, this offset is hardcoded from the mock. In a real deployment,
// read the Address Library bin file.

static const mora::rt::BSTHashMapLayout* g_all_forms = nullptr;

static void resolve_all_forms() {
    // Get SkyrimSE.exe base
    void* base = GetModuleHandleW(nullptr);
    if (!base) return;

    // Address Library offset for allForms (ID 514351)
    // This should be read from the Address Library file at runtime.
    // For now, use the known SE offset.
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

    // Walk the entire BSTHashMap and collect weapon forms
    std::vector<mora::harness::WeaponData> weapons;

    for (uint32_t i = 0; i < g_all_forms->capacity; i++) {
        auto* entry = &g_all_forms->entries[i];
        if (entry->next == nullptr) continue; // empty slot

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

    // Write to file
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

    // Find INI file next to the DLL
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
```

- [ ] **Step 2: Verify it compiles on Linux**

Run: `xmake build mora_lib`
Expected: Compiles (Linux stubs used).

- [ ] **Step 3: Commit**

```bash
git add src/harness/harness_plugin.cpp
git commit -m "feat: MoraTestHarness SKSE plugin entry point"
```

---

### Task 5: Build Script — mora_rt.lib

Replace the bitcode build script with one that produces a `.lib` (still LTO-capable via `-flto`).

**Files:**
- Create: `scripts/build_rt_lib.sh`

- [ ] **Step 1: Write the build script**

Create `scripts/build_rt_lib.sh`:

```bash
#!/bin/bash
# Build mora_rt.lib — static library for LTO linking into generated DLLs
# Replaces build_rt_bitcode.sh. The .lib contains LTO bitcode objects,
# so lld-link performs LTO at link time (same optimization wins).
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/rt"
OUTPUT="$PROJECT_DIR/data/mora_rt.lib"

XWIN="$HOME/.xwin"

[ -d "$XWIN" ] || { echo "Error: xwin SDK not found at $XWIN"; exit 1; }
command -v clang-cl >/dev/null || { echo "Error: clang-cl not found"; exit 1; }
command -v llvm-lib >/dev/null || { echo "Error: llvm-lib not found"; exit 1; }

CLANG_FLAGS=(
    --target=x86_64-pc-windows-msvc
    /std:c++20
    /O2
    /DWIN32
    /D_WIN32
    /DNOMINMAX
    /EHsc
    -flto
    -imsvc "$XWIN/crt/include"
    -imsvc "$XWIN/sdk/include/ucrt"
    -imsvc "$XWIN/sdk/include/um"
    -imsvc "$XWIN/sdk/include/shared"
    -I "$PROJECT_DIR/include"
)

SOURCES=(
    src/rt/crc32.cpp
    src/rt/bst_hashmap.cpp
    src/rt/form_ops.cpp
    src/rt/plugin_entry.cpp
)

mkdir -p "$BUILD_DIR" "$(dirname "$OUTPUT")"

# Compile each source to .obj (with LTO bitcode embedded)
OBJ_FILES=()
for src in "${SOURCES[@]}"; do
    obj="$BUILD_DIR/$(basename "$src" .cpp).obj"
    echo "  CC $src"
    clang-cl "${CLANG_FLAGS[@]}" /c "/Fo$obj" "$PROJECT_DIR/$src"
    OBJ_FILES+=("$obj")
done

# Archive into .lib
echo "  LIB mora_rt.lib"
llvm-lib /out:"$OUTPUT" "${OBJ_FILES[@]}"

echo ""
echo "Done: $OUTPUT ($(du -h "$OUTPUT" | awk '{print $1}'))"
```

- [ ] **Step 2: Make executable and test**

Run:
```bash
chmod +x scripts/build_rt_lib.sh
./scripts/build_rt_lib.sh
```
Expected: Produces `data/mora_rt.lib`. Verify with `llvm-lib /list data/mora_rt.lib`.

- [ ] **Step 3: Commit**

```bash
git add scripts/build_rt_lib.sh
git commit -m "feat: build script for mora_rt.lib (replaces bitcode)"
```

---

### Task 6: Build Script — MoraTestHarness.dll

Cross-compile the test harness DLL, linking against `mora_rt.lib`.

**Files:**
- Create: `scripts/build_test_harness.sh`

- [ ] **Step 1: Write the build script**

Create `scripts/build_test_harness.sh`:

```bash
#!/bin/bash
# Build MoraTestHarness.dll — standalone SKSE plugin for integration testing
# Links against mora_rt.lib (must be built first via build_rt_lib.sh)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/harness"
RT_LIB="$PROJECT_DIR/data/mora_rt.lib"
OUTPUT="$BUILD_DIR/MoraTestHarness.dll"

XWIN="$HOME/.xwin"

[ -d "$XWIN" ] || { echo "Error: xwin SDK not found at $XWIN"; exit 1; }
[ -f "$RT_LIB" ] || { echo "Error: mora_rt.lib not found. Run build_rt_lib.sh first."; exit 1; }
command -v clang-cl >/dev/null || { echo "Error: clang-cl not found"; exit 1; }
command -v lld-link >/dev/null || { echo "Error: lld-link not found"; exit 1; }

CFLAGS=(
    --target=x86_64-pc-windows-msvc
    /std:c++20
    /O2
    /EHsc
    /DWIN32
    /D_WIN32
    /DNOMINMAX
    -flto
    -imsvc "$XWIN/crt/include"
    -imsvc "$XWIN/sdk/include/ucrt"
    -imsvc "$XWIN/sdk/include/um"
    -imsvc "$XWIN/sdk/include/shared"
    -I "$PROJECT_DIR/include"
)

SOURCES=(
    src/harness/harness_plugin.cpp
    src/harness/weapon_dumper.cpp
    src/harness/ini_reader.cpp
    src/harness/tcp_listener.cpp
)

mkdir -p "$BUILD_DIR/obj"

# Compile
OBJ_FILES=()
for src in "${SOURCES[@]}"; do
    obj="$BUILD_DIR/obj/$(basename "$src" .cpp).obj"
    echo "  CC $src"
    clang-cl "${CFLAGS[@]}" /c "/Fo$obj" "$PROJECT_DIR/$src"
    OBJ_FILES+=("$obj")
done

# Link
echo "  LINK MoraTestHarness.dll"
lld-link \
    /dll \
    /out:"$OUTPUT" \
    /libpath:"$XWIN/crt/lib/x86_64" \
    /libpath:"$XWIN/sdk/lib/um/x86_64" \
    /libpath:"$XWIN/sdk/lib/ucrt/x86_64" \
    "${OBJ_FILES[@]}" \
    "$RT_LIB" \
    msvcrt.lib ucrt.lib vcruntime.lib kernel32.lib ws2_32.lib

echo ""
echo "Done: $OUTPUT ($(du -h "$OUTPUT" | awk '{print $1}'))"
file "$OUTPUT"
```

- [ ] **Step 2: Make executable and test**

Run:
```bash
chmod +x scripts/build_test_harness.sh
./scripts/build_rt_lib.sh && ./scripts/build_test_harness.sh
```
Expected: Produces `build/harness/MoraTestHarness.dll`. Verify with `file build/harness/MoraTestHarness.dll` showing PE32+ executable (DLL).

- [ ] **Step 3: Commit**

```bash
git add scripts/build_test_harness.sh
git commit -m "feat: build script for MoraTestHarness.dll"
```

---

### Task 7: Update DLLBuilder to Link Against mora_rt.lib

Remove the explicit LTO merge step from `DLLBuilder`. Instead, pass `mora_rt.lib` to lld-link alongside the generated `.obj`. LTO happens at link time.

**Files:**
- Modify: `include/mora/codegen/dll_builder.h`
- Modify: `src/codegen/dll_builder.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Update the header**

In `include/mora/codegen/dll_builder.h`, change the `build()` signature to take a lib path instead of a bitcode path. Remove `lto_merge()`:

Replace the `build` method signature:
```cpp
    // Full in-process pipeline: patches → DLL on disk
    // rt_lib_path: path to mora_rt.lib (LTO-capable static library)
    //              if empty, links without runtime support (standalone IR only)
    BuildResult build(const ResolvedPatchSet& patches,
                      StringPool& pool,
                      const std::filesystem::path& output_dir,
                      const std::filesystem::path& rt_lib_path = {});
```

Remove the `lto_merge` declaration from private:
```cpp
    // REMOVED: bool lto_merge(llvm::Module& patch_mod, ...)
```

Update `link_dll_in_process` to accept an optional lib path:
```cpp
    bool link_dll_in_process(const std::filesystem::path& obj_path,
                              const std::filesystem::path& dll_path,
                              const std::filesystem::path& rt_lib_path,
                              std::string& error);
```

- [ ] **Step 2: Update the implementation**

In `src/codegen/dll_builder.cpp`:

Remove the entire `DLLBuilder::lto_merge()` method (lines 51-82).

Update `link_dll_in_process` to accept and use the lib path:

```cpp
bool DLLBuilder::link_dll_in_process(const std::filesystem::path& obj_path,
                                      const std::filesystem::path& dll_path,
                                      const std::filesystem::path& rt_lib_path,
                                      std::string& error) {
    const char* home_env = std::getenv("HOME");
    std::string xwin = std::string(home_env ? home_env : "") + "/.xwin";

    std::vector<std::string> arg_strings = {
        "lld-link",
        "/dll",
        "/nodefaultlib",
        "/out:" + dll_path.string(),
        "/libpath:" + xwin + "/crt/lib/x86_64",
        "/libpath:" + xwin + "/sdk/lib/um/x86_64",
        "/libpath:" + xwin + "/sdk/lib/ucrt/x86_64",
        "msvcrt.lib",
        "ucrt.lib",
        "vcruntime.lib",
        "kernel32.lib",
        obj_path.string(),
    };

    // Link against mora_rt.lib if provided
    if (!rt_lib_path.empty()) {
        arg_strings.push_back(rt_lib_path.string());
    }

    std::vector<const char*> args;
    for (auto& s : arg_strings) args.push_back(s.c_str());

    std::string lld_stdout_str, lld_stderr_str;
    llvm::raw_string_ostream lld_stdout(lld_stdout_str), lld_stderr(lld_stderr_str);
    lld::Result link_result = lld::lldMain(args, lld_stdout, lld_stderr,
                                            {{lld::WinLink, &lld::coff::link}});

    if (link_result.retCode != 0) {
        error = "LLD linking failed: " + lld_stderr_str;
        if (error.back() == '\n') error.pop_back();
        return false;
    }

    return true;
}
```

Update the `build()` method — remove the LTO merge step, pass lib path through to link:

```cpp
DLLBuilder::BuildResult DLLBuilder::build(
        const ResolvedPatchSet& patches,
        StringPool& pool,
        const std::filesystem::path& output_dir,
        const std::filesystem::path& rt_lib_path) {
    BuildResult result;
    std::filesystem::create_directories(output_dir);

    // Step 1: Generate IR
    auto t0 = std::chrono::steady_clock::now();
    llvm::LLVMContext ctx;
    auto mod = generate_ir(patches, pool, ctx);
    if (!mod) {
        result.error = "Failed to generate IR module";
        return result;
    }

    {
        std::string verify_err;
        llvm::raw_string_ostream vs(verify_err);
        if (llvm::verifyModule(*mod, &vs)) {
            result.error = "IR verification failed: " + verify_err;
            return result;
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    result.ir_gen_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Step 2: Optimize the generated IR
    optimize(*mod);

    auto t2 = std::chrono::steady_clock::now();
    result.lto_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    // Step 3: Compile to object
    auto obj_path = output_dir / "mora_patches.obj";
    if (!compile_to_object(*mod, obj_path, result.error)) {
        return result;
    }

    auto t3 = std::chrono::steady_clock::now();
    result.compile_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    // Step 4: Link DLL — lld-link handles LTO between .obj and .lib
    auto dll_path = output_dir / "MoraRuntime.dll";
    if (!link_dll_in_process(obj_path, dll_path, rt_lib_path, result.error)) {
        return result;
    }

    std::filesystem::remove(obj_path);
    auto lib_artifact = output_dir / "MoraRuntime.lib";
    if (std::filesystem::exists(lib_artifact)) std::filesystem::remove(lib_artifact);

    auto t4 = std::chrono::steady_clock::now();
    result.link_ms = std::chrono::duration<double, std::milli>(t4 - t3).count();

    result.success = true;
    result.output_path = dll_path;
    result.patch_count = patches.patch_count();
    return result;
}
```

Remove the `#include <llvm/Linker/Linker.h>` and `#include <llvm/Bitcode/BitcodeReader.h>` includes since `lto_merge` is gone.

- [ ] **Step 3: Update main.cpp**

In `src/main.cpp`, change the `mora_rt.bc` search to look for `mora_rt.lib` instead. Around line 427:

Replace:
```cpp
    // Find mora_rt.bc — look next to the mora binary, then in data/
    fs::path rt_bc_path;
    {
        auto exe_dir = fs::canonical("/proc/self/exe").parent_path();
        std::vector<fs::path> candidates = {
            exe_dir / "mora_rt.bc",
            exe_dir / "../data/mora_rt.bc",
            exe_dir / "../../data/mora_rt.bc",
            exe_dir / "../../../data/mora_rt.bc",
            exe_dir / "../../../../data/mora_rt.bc",
            fs::path("data/mora_rt.bc"),
        };
        for (auto& p : candidates) {
            if (fs::exists(p)) { rt_bc_path = fs::canonical(p); break; }
        }
    }

    auto build_result = builder.build(final_resolved, cr.pool, out_path, rt_bc_path);
```

With:
```cpp
    // Find mora_rt.lib — look next to the mora binary, then in data/
    fs::path rt_lib_path;
    {
        auto exe_dir = fs::canonical("/proc/self/exe").parent_path();
        std::vector<fs::path> candidates = {
            exe_dir / "mora_rt.lib",
            exe_dir / "../data/mora_rt.lib",
            exe_dir / "../../data/mora_rt.lib",
            exe_dir / "../../../data/mora_rt.lib",
            exe_dir / "../../../../data/mora_rt.lib",
            fs::path("data/mora_rt.lib"),
        };
        for (auto& p : candidates) {
            if (fs::exists(p)) { rt_lib_path = fs::canonical(p); break; }
        }
    }

    auto build_result = builder.build(final_resolved, cr.pool, out_path, rt_lib_path);
```

- [ ] **Step 4: Run existing tests**

Run: `xmake build && xmake test`
Expected: All existing tests pass. The `DLLBuilderTest` tests call `generate_ir()` and `compile_to_object()` directly — they don't go through `build()` or `lto_merge()`, so they're unaffected.

- [ ] **Step 5: Commit**

```bash
git add include/mora/codegen/dll_builder.h src/codegen/dll_builder.cpp src/main.cpp
git commit -m "refactor: DLLBuilder links mora_rt.lib instead of LTO-merging bitcode"
```

---

### Task 8: Python Test Driver

The CLI tool that connects to the TCP harness, sends commands, saves dumps, and compares snapshots.

**Files:**
- Create: `tools/test_harness.py`

- [ ] **Step 1: Write the test driver**

Create `tools/test_harness.py`:

```python
#!/usr/bin/env python3
"""Mora Integration Test Harness — CLI driver.

Connects to MoraTestHarness.dll's TCP port inside a running Skyrim instance,
sends dump commands, and compares snapshots.

Usage:
    python tools/test_harness.py capture --tag skypatcher --commands "dump weapons"
    python tools/test_harness.py capture --tag mora --commands "dump weapons"
    python tools/test_harness.py compare --expected skypatcher --actual mora
    python tools/test_harness.py status
"""

import argparse
import difflib
import json
import os
import shutil
import socket
import sys
import time
from pathlib import Path

DEFAULT_HOST = "localhost"
DEFAULT_PORT = 9742
SNAPSHOT_DIR = Path(__file__).parent.parent / "test_data" / "snapshots"
TIMEOUT = 30  # seconds


def send_command(host: str, port: int, command: str) -> dict:
    """Send a command to the harness and return the JSON response."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(TIMEOUT)
        sock.connect((host, port))
        sock.sendall((command + "\n").encode())
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk
        return json.loads(data.decode().strip())


def wait_for_ready(host: str, port: int, retries: int = 30, delay: float = 1.0):
    """Poll until the harness responds to status."""
    for i in range(retries):
        try:
            resp = send_command(host, port, "status")
            if resp.get("ok"):
                print(f"Harness ready (attempt {i + 1})")
                return True
        except (ConnectionRefusedError, socket.timeout, OSError):
            pass
        time.sleep(delay)
    print("Harness not responding after retries", file=sys.stderr)
    return False


def cmd_status(args):
    resp = send_command(args.host, args.port, "status")
    print(json.dumps(resp, indent=2))


def cmd_capture(args):
    if not wait_for_ready(args.host, args.port):
        sys.exit(1)

    SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)

    for command in args.commands:
        print(f"Sending: {command}")
        resp = send_command(args.host, args.port, command)
        print(f"Response: {json.dumps(resp)}")

        if not resp.get("ok"):
            print(f"Command failed: {resp.get('error', 'unknown')}", file=sys.stderr)
            sys.exit(1)

        # Copy the dump file to snapshots
        remote_file = resp.get("file")
        if remote_file:
            # Derive snapshot name from command + tag
            name = command.replace(" ", "_")
            snapshot = SNAPSHOT_DIR / f"{name}_{args.tag}.jsonl"
            src = Path(remote_file)
            if src.exists():
                shutil.copy2(src, snapshot)
                print(f"Saved: {snapshot}")
            else:
                print(f"Warning: dump file not found locally: {remote_file}")
                print(f"(File may be on remote machine — copy manually)")

    # Send quit to clean up
    try:
        send_command(args.host, args.port, "quit")
    except (ConnectionRefusedError, socket.timeout, OSError):
        pass


def cmd_compare(args):
    SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)

    # Find matching snapshot files
    expected_files = sorted(SNAPSHOT_DIR.glob(f"*_{args.expected}.jsonl"))
    actual_files = sorted(SNAPSHOT_DIR.glob(f"*_{args.actual}.jsonl"))

    if not expected_files:
        print(f"No snapshots found for tag '{args.expected}'", file=sys.stderr)
        sys.exit(1)
    if not actual_files:
        print(f"No snapshots found for tag '{args.actual}'", file=sys.stderr)
        sys.exit(1)

    all_match = True

    for exp_file in expected_files:
        # Find corresponding actual file
        prefix = exp_file.name.replace(f"_{args.expected}.jsonl", "")
        act_file = SNAPSHOT_DIR / f"{prefix}_{args.actual}.jsonl"

        if not act_file.exists():
            print(f"MISSING: {act_file.name} (no matching actual for {exp_file.name})")
            all_match = False
            continue

        with open(exp_file) as f:
            expected_lines = f.readlines()
        with open(act_file) as f:
            actual_lines = f.readlines()

        diff = list(difflib.unified_diff(
            expected_lines, actual_lines,
            fromfile=f"{args.expected}/{exp_file.name}",
            tofile=f"{args.actual}/{act_file.name}",
            lineterm=""
        ))

        if diff:
            all_match = False
            print(f"DIFF: {prefix}")
            for line in diff:
                print(line)
            print()
        else:
            print(f"MATCH: {prefix}")

    if all_match:
        print("\nAll snapshots match.")
        sys.exit(0)
    else:
        print("\nSnapshots differ.", file=sys.stderr)
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description="Mora Integration Test Harness")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)

    sub = parser.add_subparsers(dest="action", required=True)

    p_status = sub.add_parser("status", help="Check harness status")
    p_status.set_defaults(func=cmd_status)

    p_capture = sub.add_parser("capture", help="Capture form dumps")
    p_capture.add_argument("--tag", required=True, help="Snapshot tag (e.g. skypatcher, mora)")
    p_capture.add_argument("--commands", nargs="+", default=["dump weapons"],
                           help="Commands to send")
    p_capture.set_defaults(func=cmd_capture)

    p_compare = sub.add_parser("compare", help="Compare snapshots")
    p_compare.add_argument("--expected", required=True, help="Expected snapshot tag")
    p_compare.add_argument("--actual", required=True, help="Actual snapshot tag")
    p_compare.set_defaults(func=cmd_compare)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Verify it runs (help output)**

Run: `python3 tools/test_harness.py --help`
Expected: Prints usage with `status`, `capture`, `compare` subcommands.

Run: `python3 tools/test_harness.py compare --expected nonexistent --actual also_nonexistent`
Expected: Prints "No snapshots found" and exits with code 1.

- [ ] **Step 3: Commit**

```bash
git add tools/test_harness.py
git commit -m "feat: Python test driver for integration snapshot testing"
```

---

### Task 9: End-to-End Verification

Run the full xmake test suite to confirm nothing is broken, and verify both build scripts produce valid outputs.

- [ ] **Step 1: Run all tests**

Run: `xmake build && xmake test`
Expected: All tests pass (31+ existing tests plus the 2 new test targets).

- [ ] **Step 2: Build mora_rt.lib**

Run: `./scripts/build_rt_lib.sh`
Expected: Produces `data/mora_rt.lib`.

- [ ] **Step 3: Build MoraTestHarness.dll**

Run: `./scripts/build_test_harness.sh`
Expected: Produces `build/harness/MoraTestHarness.dll`, verified as PE32+ DLL.

- [ ] **Step 4: Verify the Python driver**

Run: `python3 tools/test_harness.py --help`
Expected: Clean help output.

- [ ] **Step 5: Final commit with any fixups**

If any fixups were needed, commit them:
```bash
git add -A
git commit -m "fix: end-to-end verification fixups"
```
