#pragma once
//
// Shared test harness for unit tests that read vanilla Skyrim.esm.
//
// Resolves the Skyrim data directory in this order:
//   1. $MORA_SKYRIM_DATA environment variable
//   2. /skyrim-base/Data (CI runner image)
//   3. ~/.local/share/Steam/steamapps/common/Skyrim Special Edition/Data
//      (dev-box GOG/Steam default)
//
// Skyrim.esm is loaded exactly once per test binary (each test file is its
// own binary under `xmake.lua`'s per-file target rule), and every
// SkyrimTest-derived gtest case gets access to the same FactDB + StringPool.

#include <gtest/gtest.h>

#include "mora/core/string_pool.h"
#include "mora/data/schema_registry.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora_skyrim_compile/esp/esp_reader.h"
#include "mora/eval/fact_db.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace mora::test {

inline std::filesystem::path skyrim_data_dir() {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (const char* env = std::getenv("MORA_SKYRIM_DATA"); env && *env) {
        fs::path p = env;
        if (fs::exists(p / "Skyrim.esm", ec)) return p;
    }
    fs::path ci = "/skyrim-base/Data";
    if (fs::exists(ci / "Skyrim.esm", ec)) return ci;
    if (const char* home = std::getenv("HOME"); home && *home) {
        fs::path dev = fs::path(home) /
            ".local/share/Steam/steamapps/common/Skyrim Special Edition/Data";
        if (fs::exists(dev / "Skyrim.esm", ec)) return dev;
    }
    return {};
}

inline std::filesystem::path skyrim_esm_path() {
    auto dir = skyrim_data_dir();
    if (dir.empty()) return {};
    return dir / "Skyrim.esm";
}

// Per-process singleton: loads Skyrim.esm once and hands out refs.
//
// The loader aborts the test binary if Skyrim.esm can't be located — it's
// a hard prerequisite, not an optional fixture. See tests/skyrim_fixture.h
// header comment for the search order.
class SkyrimEnv {
public:
    static SkyrimEnv& instance() {
        static SkyrimEnv env;
        return env;
    }

    StringPool& pool() { load(); return pool_; }
    FactDB&     db()   { load(); return db_; }

    // Filter `rel_name`'s rows to those whose first column is a FormID
    // equal to `form_id`.
    std::vector<Tuple> rows_for(const std::string& rel_name, uint32_t form_id) {
        load();
        auto rel = pool_.intern(rel_name);
        auto& all = db_.get_relation(rel);
        std::vector<Tuple> out;
        for (auto& t : all) {
            if (t.empty()) continue;
            if (t[0].kind() == Value::Kind::FormID && t[0].as_formid() == form_id) {
                out.push_back(t);
            }
        }
        return out;
    }

    bool has_fact_for(const std::string& rel_name, uint32_t form_id) {
        return !rows_for(rel_name, form_id).empty();
    }

private:
    SkyrimEnv() = default;

    void load() {
        if (loaded_) return;
        auto path = skyrim_esm_path();
        if (path.empty()) {
            std::fprintf(stderr,
                "[skyrim_fixture] Skyrim.esm not found. Set MORA_SKYRIM_DATA "
                "to a Skyrim SE Data/ directory, or install at "
                "/skyrim-base/Data/Skyrim.esm, or the Steam default path.\n");
            std::abort();
        }
        schema_ = std::make_unique<SchemaRegistry>(pool_);
        schema_->register_defaults();
        schema_->configure_fact_db(db_);
        EspReader reader(pool_, diags_, *schema_);
        reader.read_plugin(path, db_);
        loaded_ = true;
    }

    bool loaded_ = false;
    StringPool pool_;
    DiagBag    diags_;
    FactDB     db_{pool_};
    std::unique_ptr<SchemaRegistry> schema_;
};

// gtest fixture wrapping the shared SkyrimEnv.
class SkyrimTest : public ::testing::Test {
protected:
    StringPool& pool() { return SkyrimEnv::instance().pool(); }
    FactDB&     db()   { return SkyrimEnv::instance().db(); }

    std::vector<Tuple> rows_for(const std::string& rel, uint32_t fid) {
        return SkyrimEnv::instance().rows_for(rel, fid);
    }
    bool has_fact_for(const std::string& rel, uint32_t fid) {
        return SkyrimEnv::instance().has_fact_for(rel, fid);
    }

    // Unique scalar fact: exactly one row for `rel` keyed by `fid`, arity >= 2.
    // Returns column 1 via `out`. ASSERT_* friendly.
    ::testing::AssertionResult scalar_fact(const std::string& rel, uint32_t fid, Value& out) {
        auto rows = rows_for(rel, fid);
        if (rows.empty()) return ::testing::AssertionFailure()
            << "no rows for relation '" << rel << "' with form_id "
            << hex_id(fid);
        if (rows.size() > 1) return ::testing::AssertionFailure()
            << "expected one row for relation '" << rel << "' / "
            << hex_id(fid) << " but got " << rows.size();
        if (rows[0].size() < 2) return ::testing::AssertionFailure()
            << "relation '" << rel << "' has arity " << rows[0].size()
            << " — expected >= 2";
        out = rows[0][1];
        return ::testing::AssertionSuccess();
    }

    // Collect all column-1 FormIDs for a list-valued relation keyed by `fid`.
    std::vector<uint32_t> list_formids(const std::string& rel, uint32_t fid) {
        std::vector<uint32_t> out;
        for (auto& t : rows_for(rel, fid)) {
            if (t.size() >= 2 && t[1].kind() == Value::Kind::FormID) {
                out.push_back(t[1].as_formid());
            }
        }
        return out;
    }

private:
    static std::string hex_id(uint32_t id) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", id);
        return buf;
    }
};

} // namespace mora::test
