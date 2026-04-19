#include <gtest/gtest.h>

#include "mora_skyrim_compile/kid_compiler.h"

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <unordered_map>
#include <variant>

using namespace mora_skyrim_compile;

namespace {

class KidCompilerTest : public ::testing::Test {
protected:
    std::filesystem::path tmp_dir;

    void SetUp() override {
        namespace fs = std::filesystem;
        auto base = fs::temp_directory_path();
        std::random_device rd;
        std::mt19937 mt(rd());
        for (int i = 0; i < 20; ++i) {
            auto p = base / ("mora_kid_compiler_test_" +
                              std::to_string(::getpid()) + "_" +
                              std::to_string(mt()));
            std::error_code ec;
            if (fs::create_directory(p, ec)) {
                tmp_dir = p;
                return;
            }
        }
        FAIL() << "Couldn't create tmp dir";
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tmp_dir, ec);
    }

    void write(const std::string& filename, const std::string& contents) {
        std::ofstream f(tmp_dir / filename);
        f << contents;
    }
};

bool head_target_is(const mora::Rule& r, std::string_view edid_name,
                     const mora::StringPool& pool) {
    if (r.head_args.size() < 3) return false;
    if (auto const* eid = std::get_if<mora::EditorIdExpr>(&r.head_args[2].data)) {
        return pool.get(eid->name) == edid_name;
    }
    return false;
}

} // namespace

TEST_F(KidCompilerTest, EmptyDirIsNoop) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {{"K", 0x100}};

    KidCompileInputs in;
    in.data_dir   = tmp_dir;
    in.editor_ids = &edids;

    auto out = compile_kid_modules(in, pool, diags);
    EXPECT_TRUE(out.module.rules.empty());
    EXPECT_EQ(diags.warning_count(), 0u);
}

TEST_F(KidCompilerTest, MissingDataDirIsNoop) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;

    KidCompileInputs in;
    in.data_dir   = std::filesystem::path{"/nonexistent/path/please/dont/exist"};
    in.editor_ids = &edids;

    auto out = compile_kid_modules(in, pool, diags);
    EXPECT_TRUE(out.module.rules.empty());
}

TEST_F(KidCompilerTest, NoEditorIdsEmitsWarningAndSkips) {
    write("Test_KID.ini", "MyKW|Weapon|NONE|NONE|100\n");
    mora::StringPool pool;
    mora::DiagBag diags;

    KidCompileInputs in;
    in.data_dir   = tmp_dir;
    in.editor_ids = nullptr;  // not provided

    auto out = compile_kid_modules(in, pool, diags);
    EXPECT_TRUE(out.module.rules.empty());
    bool saw = false;
    for (auto& d : diags.all()) if (d.code == "kid-no-editor-ids") saw = true;
    EXPECT_TRUE(saw);
}

TEST_F(KidCompilerTest, EndToEndProducesRules) {
    write("Test_KID.ini",
        "; sample\n"
        "MyKW|Weapon|NONE|NONE|100\n"
        "OtherKW|Armor|FilterKW|NONE|100\n");
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"MyKW",     0x100},
        {"OtherKW",  0x101},
        {"FilterKW", 0x200},
    };

    KidCompileInputs in;
    in.data_dir   = tmp_dir;
    in.editor_ids = &edids;

    auto out = compile_kid_modules(in, pool, diags);
    EXPECT_EQ(out.lines_parsed, 2u);
    ASSERT_EQ(out.module.rules.size(), 2u);

    // One rule targets MyKW (no filter), one targets OtherKW (with FilterKW).
    bool saw_my = false;
    bool saw_other = false;
    for (const auto& r : out.module.rules) {
        if (head_target_is(r, "MyKW", pool))    saw_my = true;
        if (head_target_is(r, "OtherKW", pool)) saw_other = true;
    }
    EXPECT_TRUE(saw_my);
    EXPECT_TRUE(saw_other);
}

TEST_F(KidCompilerTest, FileSuffixMatchIsCaseInsensitive) {
    write("test_kid.ini", "MyKW|Weapon|NONE|NONE|100\n");
    write("OTHER_KID.INI", "OtherKW|Weapon|NONE|NONE|100\n");
    write("not_kid.txt", "ignored\n");
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"MyKW", 1}, {"OtherKW", 2},
    };

    KidCompileInputs in;
    in.data_dir   = tmp_dir;
    in.editor_ids = &edids;

    auto out = compile_kid_modules(in, pool, diags);
    EXPECT_EQ(out.lines_parsed, 2u);
    EXPECT_EQ(out.module.rules.size(), 2u);
}

TEST_F(KidCompilerTest, KidDirOverridesDataDir) {
    // Files under tmp_dir should be ignored when kid_dir points elsewhere.
    write("Test_KID.ini", "MyKW|Weapon|NONE|NONE|100\n");

    namespace fs = std::filesystem;
    fs::path other = tmp_dir / "elsewhere";
    fs::create_directory(other);
    {
        std::ofstream o(other / "Other_KID.ini");
        o << "OtherKW|Weapon|NONE|NONE|100\n";
    }

    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"MyKW", 1}, {"OtherKW", 2},
    };

    KidCompileInputs in;
    in.data_dir   = tmp_dir;
    in.kid_dir    = other;
    in.editor_ids = &edids;

    auto out = compile_kid_modules(in, pool, diags);
    // Only OtherKW gets compiled — MyKW under tmp_dir is ignored.
    EXPECT_EQ(out.lines_parsed, 1u);
    ASSERT_EQ(out.module.rules.size(), 1u);
}

TEST_F(KidCompilerTest, ProvidesNonEmptyModuleFilenameForDiagnostics) {
    write("Test_KID.ini", "MyKW|Weapon|NONE|NONE|100\n");
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {{"MyKW", 1}};

    KidCompileInputs in;
    in.data_dir   = tmp_dir;
    in.editor_ids = &edids;

    auto out = compile_kid_modules(in, pool, diags);
    EXPECT_FALSE(out.module.filename.empty());
}
