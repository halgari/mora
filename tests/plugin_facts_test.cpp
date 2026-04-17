#include <gtest/gtest.h>
#include "mora/data/plugin_facts.h"
#include "mora/data/schema_registry.h"
#include "mora/data/value.h"
#include "mora/esp/load_order.h"
#include "mora/esp/plugin_index.h"
#include "mora/esp/record_types.h"
#include "mora/eval/fact_db.h"

static mora::PluginInfo make_info(const std::string& filename,
                                  std::vector<std::string> masters = {},
                                  uint32_t flags = 0,
                                  float version = 1.0f) {
    mora::PluginInfo info;
    info.filename = filename;
    info.masters  = std::move(masters);
    info.flags    = flags;
    info.version  = version;
    return info;
}

static bool has_fact(mora::FactDB& db, mora::StringPool& pool,
                     const std::string& rel_name,
                     const mora::Tuple& expected) {
    auto& tuples = db.get_relation(pool.intern(rel_name));
    for (auto& t : tuples) {
        if (t.size() != expected.size()) continue;
        bool eq = true;
        for (size_t i = 0; i < t.size(); i++) {
            if (!(t[i] == expected[i])) { eq = false; break; }
        }
        if (eq) return true;
    }
    return false;
}

class PluginFactsTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::SchemaRegistry schema{pool};
    mora::FactDB db{pool};

    void SetUp() override {
        schema.register_defaults();
        schema.configure_fact_db(db);
    }
};

TEST_F(PluginFactsTest, ExistsAndLoadIndexPopulated) {
    mora::LoadOrder lo;
    lo.plugins = {"/data/Skyrim.esm", "/data/Mod.esp"};
    std::vector<mora::PluginInfo> infos = {
        make_info("Skyrim.esm", {}, mora::RecordFlags::ESM),
        make_info("Mod.esp"),
    };
    mora::RuntimeIndexMap rtmap;
    rtmap.index["skyrim.esm"] = 0;
    rtmap.index["mod.esp"]    = 1;

    mora::populate_plugin_facts(db, pool, lo, infos, rtmap);

    // `plugin_exists` holds one row per loaded plugin. `plugin_load_index`
    // gives its runtime high-byte (regular) or light-space index.
    EXPECT_TRUE(has_fact(db, pool, "plugin_exists",
                         {mora::Value::make_string(pool.intern("skyrim.esm"))}));
    EXPECT_TRUE(has_fact(db, pool, "plugin_exists",
                         {mora::Value::make_string(pool.intern("mod.esp"))}));
    EXPECT_TRUE(has_fact(db, pool, "plugin_load_index",
                         {mora::Value::make_string(pool.intern("skyrim.esm")),
                          mora::Value::make_int(0)}));
    EXPECT_TRUE(has_fact(db, pool, "plugin_load_index",
                         {mora::Value::make_string(pool.intern("mod.esp")),
                          mora::Value::make_int(1)}));
}

TEST_F(PluginFactsTest, MasterFlagAndLightFlagSeparately) {
    // ESM flag → plugin_is_master. ESL flag → plugin_is_light. A
    // plugin with both (ESMFESL — a master ALSO flagged as light)
    // appears in both relations.
    mora::LoadOrder lo;
    lo.plugins = {"/data/PureMaster.esm", "/data/PureLight.esl",
                  "/data/EspFEsl.esp",   "/data/Both.esm"};
    std::vector<mora::PluginInfo> infos = {
        make_info("PureMaster.esm", {}, mora::RecordFlags::ESM),
        make_info("PureLight.esl",  {}, mora::RecordFlags::ESL),
        make_info("EspFEsl.esp",    {}, mora::RecordFlags::ESL),
        make_info("Both.esm",       {}, mora::RecordFlags::ESM | mora::RecordFlags::ESL),
    };
    mora::RuntimeIndexMap rtmap;
    rtmap.index["puremaster.esm"] = 0;
    rtmap.index["both.esm"]       = 1;
    rtmap.index["purelight.esl"]  = 0;
    rtmap.light.insert("purelight.esl");
    rtmap.index["espfesl.esp"]    = 1;
    rtmap.light.insert("espfesl.esp");

    mora::populate_plugin_facts(db, pool, lo, infos, rtmap);

    EXPECT_TRUE (has_fact(db, pool, "plugin_is_master",
                          {mora::Value::make_string(pool.intern("puremaster.esm"))}));
    EXPECT_FALSE(has_fact(db, pool, "plugin_is_master",
                          {mora::Value::make_string(pool.intern("purelight.esl"))}));
    EXPECT_FALSE(has_fact(db, pool, "plugin_is_master",
                          {mora::Value::make_string(pool.intern("espfesl.esp"))}));
    EXPECT_TRUE (has_fact(db, pool, "plugin_is_master",
                          {mora::Value::make_string(pool.intern("both.esm"))}));

    EXPECT_FALSE(has_fact(db, pool, "plugin_is_light",
                          {mora::Value::make_string(pool.intern("puremaster.esm"))}));
    EXPECT_TRUE (has_fact(db, pool, "plugin_is_light",
                          {mora::Value::make_string(pool.intern("purelight.esl"))}));
    EXPECT_TRUE (has_fact(db, pool, "plugin_is_light",
                          {mora::Value::make_string(pool.intern("espfesl.esp"))}));
    EXPECT_TRUE (has_fact(db, pool, "plugin_is_light",
                          {mora::Value::make_string(pool.intern("both.esm"))}));
}

TEST_F(PluginFactsTest, MasterOfEdgesFromMastList) {
    // MAST list in the plugin header → one `plugin_master_of` edge
    // per parent. Rules can compute the full dependency graph in
    // Datalog from these edges.
    mora::LoadOrder lo;
    lo.plugins = {"/data/Skyrim.esm", "/data/Update.esm", "/data/Dawnguard.esm"};
    std::vector<mora::PluginInfo> infos = {
        make_info("Skyrim.esm",    {},                             mora::RecordFlags::ESM),
        make_info("Update.esm",    {"Skyrim.esm"},                 mora::RecordFlags::ESM),
        make_info("Dawnguard.esm", {"Skyrim.esm", "Update.esm"},   mora::RecordFlags::ESM),
    };
    mora::RuntimeIndexMap rtmap;
    rtmap.index["skyrim.esm"]    = 0;
    rtmap.index["update.esm"]    = 1;
    rtmap.index["dawnguard.esm"] = 2;

    mora::populate_plugin_facts(db, pool, lo, infos, rtmap);

    // Skyrim has no masters — absence should show up as zero edges.
    EXPECT_FALSE(has_fact(db, pool, "plugin_master_of",
                          {mora::Value::make_string(pool.intern("skyrim.esm")),
                           mora::Value::make_string(pool.intern(""))}));

    EXPECT_TRUE(has_fact(db, pool, "plugin_master_of",
                         {mora::Value::make_string(pool.intern("update.esm")),
                          mora::Value::make_string(pool.intern("skyrim.esm"))}));
    EXPECT_TRUE(has_fact(db, pool, "plugin_master_of",
                         {mora::Value::make_string(pool.intern("dawnguard.esm")),
                          mora::Value::make_string(pool.intern("skyrim.esm"))}));
    EXPECT_TRUE(has_fact(db, pool, "plugin_master_of",
                         {mora::Value::make_string(pool.intern("dawnguard.esm")),
                          mora::Value::make_string(pool.intern("update.esm"))}));
}

TEST_F(PluginFactsTest, VersionAndExtensionRecorded) {
    mora::LoadOrder lo;
    lo.plugins = {"/data/Some.esm", "/data/Other.esp", "/data/Light.esl"};
    std::vector<mora::PluginInfo> infos = {
        make_info("Some.esm",  {}, mora::RecordFlags::ESM, 1.7f),
        make_info("Other.esp", {}, 0,                     0.95f),
        make_info("Light.esl", {}, mora::RecordFlags::ESL, 1.0f),
    };
    mora::RuntimeIndexMap rtmap;
    rtmap.index["some.esm"]  = 0;
    rtmap.index["other.esp"] = 1;
    rtmap.index["light.esl"] = 0;
    rtmap.light.insert("light.esl");

    mora::populate_plugin_facts(db, pool, lo, infos, rtmap);

    EXPECT_TRUE(has_fact(db, pool, "plugin_extension",
                         {mora::Value::make_string(pool.intern("some.esm")),
                          mora::Value::make_string(pool.intern(".esm"))}));
    EXPECT_TRUE(has_fact(db, pool, "plugin_extension",
                         {mora::Value::make_string(pool.intern("other.esp")),
                          mora::Value::make_string(pool.intern(".esp"))}));
    EXPECT_TRUE(has_fact(db, pool, "plugin_extension",
                         {mora::Value::make_string(pool.intern("light.esl")),
                          mora::Value::make_string(pool.intern(".esl"))}));

    // Version is stored as a Float value. We don't round-trip the
    // exact binary representation — the test just checks the facts
    // exist, one per loaded plugin.
    auto& tuples = db.get_relation(pool.intern("plugin_version"));
    EXPECT_GE(tuples.size(), 3u);
}
