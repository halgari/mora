#include <gtest/gtest.h>

#include "mora_skyrim_compile/form_reader.h"

#include "mora/ast/ast.h"
#include "mora/core/source_location.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include "mora/ext/extension.h"
#include "mora/ext/runtime_index.h"

#include <cstdint>
#include <unordered_map>
#include <variant>

namespace {

mora::SourceSpan span_at(uint32_t line) {
    mora::SourceSpan s{};
    s.file = "test.mora";
    s.start_line = line;
    s.end_line = line;
    return s;
}

} // namespace

TEST(FormReaderTest, HexPluginPayloadGlobalizesToFormIdLiteral) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;
    std::unordered_map<std::string, uint32_t> plugins = {
        {"mymod.esp", 0x42u},
    };
    mora::ext::ReaderContext rctx{pool, diags, &edids, &plugins};

    mora::Expr e = mora_skyrim_compile::form_reader(
        rctx, "0x12AB@MyMod.esp", span_at(3));
    EXPECT_EQ(diags.error_count(), 0u);

    const auto* fl = std::get_if<mora::FormIdLiteral>(&e.data);
    ASSERT_NE(fl, nullptr);
    EXPECT_EQ(fl->value, 0x420012ABu);
}

TEST(FormReaderTest, EslPluginUsesLightEncoding) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;
    std::unordered_map<std::string, uint32_t> plugins = {
        {"lightmod.esl", 0x003u | mora::ext::kRuntimeIdxEsl},
    };
    mora::ext::ReaderContext rctx{pool, diags, &edids, &plugins};

    mora::Expr e = mora_skyrim_compile::form_reader(
        rctx, "0x7F@LightMod.esl", span_at(1));
    EXPECT_EQ(diags.error_count(), 0u);

    const auto* fl = std::get_if<mora::FormIdLiteral>(&e.data);
    ASSERT_NE(fl, nullptr);
    EXPECT_EQ(fl->value, 0xFE00307Fu);
}

TEST(FormReaderTest, EditorIdPayloadEmitsEditorIdExpr) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::ext::ReaderContext rctx{pool, diags, nullptr, nullptr};

    mora::Expr e = mora_skyrim_compile::form_reader(
        rctx, "MyKeyword", span_at(1));
    EXPECT_EQ(diags.error_count(), 0u);

    const auto* eid = std::get_if<mora::EditorIdExpr>(&e.data);
    ASSERT_NE(eid, nullptr);
    EXPECT_EQ(pool.get(eid->name), "MyKeyword");
}

TEST(FormReaderTest, MissingPluginEmitsDiag) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;
    std::unordered_map<std::string, uint32_t> plugins;  // empty
    mora::ext::ReaderContext rctx{pool, diags, &edids, &plugins};

    (void)mora_skyrim_compile::form_reader(
        rctx, "0x01@Unknown.esp", span_at(1));

    bool saw = false;
    for (auto& d : diags.all()) {
        if (d.code == "reader-form-missing-plugin") saw = true;
    }
    EXPECT_TRUE(saw);
}

TEST(FormReaderTest, MalformedHexEmitsDiag) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;
    std::unordered_map<std::string, uint32_t> plugins = {
        {"mymod.esp", 0x42u},
    };
    mora::ext::ReaderContext rctx{pool, diags, &edids, &plugins};

    (void)mora_skyrim_compile::form_reader(
        rctx, "0xNOPE@MyMod.esp", span_at(1));

    bool saw = false;
    for (auto& d : diags.all()) {
        if (d.code == "reader-form-malformed") saw = true;
    }
    EXPECT_TRUE(saw);
}

TEST(FormReaderTest, NoPluginIndexEmitsDiagForHexPayload) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::ext::ReaderContext rctx{pool, diags, nullptr, nullptr};

    (void)mora_skyrim_compile::form_reader(
        rctx, "0x12@Mod.esp", span_at(1));

    bool saw = false;
    for (auto& d : diags.all()) {
        if (d.code == "reader-form-no-data") saw = true;
    }
    EXPECT_TRUE(saw);
}
