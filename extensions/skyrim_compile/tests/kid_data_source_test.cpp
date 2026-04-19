#include <gtest/gtest.h>
#include "mora_skyrim_compile/kid_data_source.h"

#include "mora/core/string_pool.h"
#include "mora/data/schema_registry.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/fact_db.h"
#include "mora/ext/data_source.h"

#include <filesystem>
#include <fstream>

using namespace mora_skyrim_compile;

namespace {

class TempDir {
public:
    TempDir() {
        auto base = std::filesystem::temp_directory_path();
        for (int i = 0; i < 100; ++i) {
            path_ = base / ("mora_kid_ds_test_" + std::to_string(::getpid()) +
                            "_" + std::to_string(i));
            std::error_code ec;
            if (std::filesystem::create_directory(path_, ec)) return;
        }
        std::abort();
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& p, std::string_view content) {
    std::ofstream o(p);
    o << content;
}

class KidDataSourceTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag    diags;
    mora::FactDB     db{pool};
    std::unordered_map<std::string, uint32_t> editor_ids;

    mora::ext::LoadCtx make_ctx(const std::filesystem::path& data_dir) {
        return mora::ext::LoadCtx{
            pool,
            diags,
            data_dir,
            {},  // plugins_txt
            {},  // needed_relations
            &editor_ids,
            nullptr,
        };
    }

    void SetUp() override {
        // KID relations must be configured so the FactDB has indexed slots.
        mora::SchemaRegistry schema(pool);
        schema.register_defaults();
        schema.configure_fact_db(db);
    }
};

} // namespace

TEST_F(KidDataSourceTest, ProvidesKidRelations) {
    KidDataSource ds;
    auto p = ds.provides();
    ASSERT_EQ(p.size(), 4u);
    EXPECT_EQ(p[0], "ini/kid_dist");
    EXPECT_EQ(p[1], "ini/kid_filter");
    EXPECT_EQ(p[2], "ini/kid_exclude");
    EXPECT_EQ(p[3], "ini/kid_trait");
}

TEST_F(KidDataSourceTest, EmptyDirIsNoop) {
    TempDir td;
    auto ctx = make_ctx(td.path());
    KidDataSource ds;
    ds.load(ctx, db);
    EXPECT_EQ(diags.warning_count(), 0u);
    EXPECT_EQ(db.fact_count(pool.intern("ini/kid_dist")), 0u);
}

TEST_F(KidDataSourceTest, MissingDataDirIsNoop) {
    auto ctx = make_ctx("/nonexistent/path/for/mora_test");
    KidDataSource ds;
    ds.load(ctx, db);
    EXPECT_EQ(diags.warning_count(), 0u);
}

TEST_F(KidDataSourceTest, FileSuffixMatchIsCaseInsensitive) {
    TempDir td;
    write_file(td.path() / "A_KID.ini",
               "KW1|Weapon|NONE|NONE|100\n");
    write_file(td.path() / "lowercase_kid.ini",
               "KW2|Armor|NONE|NONE|100\n");
    write_file(td.path() / "NotKid.ini",
               "ignored\n");

    editor_ids["KW1"] = 0x10u;
    editor_ids["KW2"] = 0x20u;

    auto ctx = make_ctx(td.path());
    KidDataSource ds;
    ds.load(ctx, db);

    // One kid_dist per KID file, zero filters (NONE).
    EXPECT_EQ(db.fact_count(pool.intern("ini/kid_dist")), 2u);
    EXPECT_EQ(db.fact_count(pool.intern("ini/kid_filter")), 0u);
}

TEST_F(KidDataSourceTest, NoEditorIdsOutEmitsWarningAndSkips) {
    TempDir td;
    write_file(td.path() / "X_KID.ini",
               "KW|Weapon|NONE|NONE|100\n");

    mora::ext::LoadCtx ctx{pool, diags, td.path(), {}, {}, nullptr, nullptr};
    KidDataSource ds;
    ds.load(ctx, db);
    EXPECT_EQ(db.fact_count(pool.intern("ini/kid_dist")), 0u);
    ASSERT_EQ(diags.warning_count(), 1u);
    EXPECT_EQ(diags.all()[0].code, "kid-no-editor-ids");
}

TEST_F(KidDataSourceTest, EndToEndSingleFile) {
    TempDir td;
    write_file(td.path() / "Mod_KID.ini",
               "; a comment\n"
               "MyKW|Weapon|IronKW|NONE|100\n"
               "BogusKW|Armor|SteelKW|E|100\n");  // BogusKW unresolved, line dropped

    editor_ids["MyKW"]   = 0x100u;
    editor_ids["IronKW"] = 0x200u;
    editor_ids["SteelKW"]= 0x300u;

    auto ctx = make_ctx(td.path());
    KidDataSource ds;
    ds.load(ctx, db);

    EXPECT_EQ(db.fact_count(pool.intern("ini/kid_dist")),   1u);
    EXPECT_EQ(db.fact_count(pool.intern("ini/kid_filter")), 1u);
    // One warning for BogusKW unresolved.
    ASSERT_GE(diags.warning_count(), 1u);
    bool saw_unresolved = false;
    for (auto& d : diags.all()) {
        if (d.code == "kid-unresolved") saw_unresolved = true;
    }
    EXPECT_TRUE(saw_unresolved);
}
