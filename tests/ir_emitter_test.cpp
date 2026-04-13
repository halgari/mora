#include <gtest/gtest.h>
#include "mora/codegen/ir_emitter.h"
#include "mora/codegen/address_library.h"
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

TEST(IREmitterTest, EmitSimplePatch) {
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("test", ctx);

    auto addrlib = mora::AddressLibrary::mock({
        {514351, 0x1EEBE10},
    });

    mora::IREmitter emitter(ctx, *mod, addrlib);

    mora::PatchSet ps;
    mora::StringPool pool;
    ps.add_patch(0x12EB7, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(42), pool.intern("test"), 0);
    auto resolved = ps.resolve();

    emitter.emit(resolved, pool);

    // Verify the module has the function
    auto* func = mod->getFunction("apply_all_patches");
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->arg_size(), 0u); // no params — CommonLib handles resolution

    // Verify it's valid IR
    std::string err;
    llvm::raw_string_ostream err_stream(err);
    EXPECT_FALSE(llvm::verifyModule(*mod, &err_stream)) << err;
}

TEST(IREmitterTest, EmitMultiplePatches) {
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("test", ctx);

    auto addrlib = mora::AddressLibrary::mock({{514351, 0x1EEBE10}});
    mora::IREmitter emitter(ctx, *mod, addrlib);

    mora::PatchSet ps;
    mora::StringPool pool;
    // Multiple patches on different forms
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(25), pool.intern("t"), 0);
    ps.add_patch(0x200, mora::FieldId::GoldValue, mora::FieldOp::Set,
                 mora::Value::make_int(100), pool.intern("t"), 0);
    ps.add_patch(0x300, mora::FieldId::Weight, mora::FieldOp::Set,
                 mora::Value::make_float(5.0), pool.intern("t"), 0);
    auto resolved = ps.resolve();

    emitter.emit(resolved, pool);

    auto* func = mod->getFunction("apply_all_patches");
    ASSERT_NE(func, nullptr);

    std::string err;
    llvm::raw_string_ostream err_stream(err);
    EXPECT_FALSE(llvm::verifyModule(*mod, &err_stream)) << err;
}

TEST(IREmitterTest, EmptyPatchSet) {
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("test", ctx);

    auto addrlib = mora::AddressLibrary::mock({{514351, 0x1EEBE10}});
    mora::IREmitter emitter(ctx, *mod, addrlib);

    mora::ResolvedPatchSet empty;
    mora::StringPool pool;
    emitter.emit(empty, pool);

    auto* func = mod->getFunction("apply_all_patches");
    ASSERT_NE(func, nullptr);

    std::string err;
    llvm::raw_string_ostream err_stream(err);
    EXPECT_FALSE(llvm::verifyModule(*mod, &err_stream)) << err;
}

// ── MemoryManager RT ops ───────────────────────────────────────────

static mora::AddressLibrary make_full_mock() {
    return mora::AddressLibrary::mock({
        {514351, 0x1EEBE10},  // form map (SE)
        {11045, 0x100000},    // MemMgr GetSingleton (SE)
        {66859, 0x100100},    // MemMgr Allocate (SE)
        {66861, 0x100200},    // MemMgr Deallocate (SE)
        {67819, 0x100300},    // BSFixedString ctor8 (SE)
        {67847, 0x100400},    // BSFixedString release8 (SE)
    });
}

TEST(IREmitterTest, EmitKeywordAdd) {
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("test_kw_add", ctx);

    auto addrlib = make_full_mock();
    mora::IREmitter emitter(ctx, *mod, addrlib);

    mora::PatchSet ps;
    mora::StringPool pool;
    ps.add_patch(0x100, mora::FieldId::Keywords, mora::FieldOp::Add,
                 mora::Value::make_formid(0xABCD), pool.intern("t"), 0);
    auto resolved = ps.resolve();

    emitter.emit(resolved, pool);

    ASSERT_NE(mod->getFunction("apply_all_patches"), nullptr);
    ASSERT_NE(mod->getFunction("mora_rt_add_keyword"), nullptr);

    std::string err;
    llvm::raw_string_ostream err_stream(err);
    EXPECT_FALSE(llvm::verifyModule(*mod, &err_stream)) << err;
}

TEST(IREmitterTest, EmitKeywordRemove) {
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("test_kw_rem", ctx);

    auto addrlib = make_full_mock();
    mora::IREmitter emitter(ctx, *mod, addrlib);

    mora::PatchSet ps;
    mora::StringPool pool;
    ps.add_patch(0x100, mora::FieldId::Keywords, mora::FieldOp::Remove,
                 mora::Value::make_formid(0xABCD), pool.intern("t"), 0);
    auto resolved = ps.resolve();

    emitter.emit(resolved, pool);

    ASSERT_NE(mod->getFunction("apply_all_patches"), nullptr);
    ASSERT_NE(mod->getFunction("mora_rt_remove_keyword"), nullptr);

    std::string err;
    llvm::raw_string_ostream err_stream(err);
    EXPECT_FALSE(llvm::verifyModule(*mod, &err_stream)) << err;
}

TEST(IREmitterTest, EmitNameWrite) {
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("test_name", ctx);

    auto addrlib = make_full_mock();
    mora::IREmitter emitter(ctx, *mod, addrlib);

    mora::PatchSet ps;
    mora::StringPool pool;
    // Name values come through the string pool with surrounding quotes
    // (the lexer keeps them). emit_apply_function strips them.
    auto name_id = pool.intern("\"Dragonbane\"");
    ps.add_patch(0x100, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(name_id), pool.intern("t"), 0);
    auto resolved = ps.resolve();

    emitter.emit(resolved, pool);

    ASSERT_NE(mod->getFunction("apply_all_patches"), nullptr);
    ASSERT_NE(mod->getFunction("mora_rt_write_name"), nullptr);

    std::string err;
    llvm::raw_string_ostream err_stream(err);
    EXPECT_FALSE(llvm::verifyModule(*mod, &err_stream)) << err;
}

TEST(IREmitterTest, EmitSpellAdd) {
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("test_spell", ctx);

    auto addrlib = make_full_mock();
    mora::IREmitter emitter(ctx, *mod, addrlib);

    mora::PatchSet ps;
    mora::StringPool pool;
    ps.add_patch(0x100, mora::FieldId::Spells, mora::FieldOp::Add,
                 mora::Value::make_formid(0x2FE),  // Flames spell
                 pool.intern("t"), 0);
    auto resolved = ps.resolve();

    emitter.emit(resolved, pool);

    ASSERT_NE(mod->getFunction("apply_all_patches"), nullptr);
    ASSERT_NE(mod->getFunction("mora_rt_add_spell"), nullptr);

    std::string err;
    llvm::raw_string_ostream err_stream(err);
    EXPECT_FALSE(llvm::verifyModule(*mod, &err_stream)) << err;
}

TEST(IREmitterTest, EmitPerkAdd) {
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("test_perk", ctx);

    auto addrlib = make_full_mock();
    mora::IREmitter emitter(ctx, *mod, addrlib);

    mora::PatchSet ps;
    mora::StringPool pool;
    ps.add_patch(0x100, mora::FieldId::Perks, mora::FieldOp::Add,
                 mora::Value::make_formid(0xBABE), pool.intern("t"), 0);
    auto resolved = ps.resolve();

    emitter.emit(resolved, pool);

    ASSERT_NE(mod->getFunction("apply_all_patches"), nullptr);
    ASSERT_NE(mod->getFunction("mora_rt_add_perk"), nullptr);

    std::string err;
    llvm::raw_string_ostream err_stream(err);
    EXPECT_FALSE(llvm::verifyModule(*mod, &err_stream)) << err;
}

TEST(IREmitterTest, EmitFactionAdd) {
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("test_faction", ctx);

    auto addrlib = make_full_mock();
    mora::IREmitter emitter(ctx, *mod, addrlib);

    mora::PatchSet ps;
    mora::StringPool pool;
    ps.add_patch(0x100, mora::FieldId::Factions, mora::FieldOp::Add,
                 mora::Value::make_formid(0xCAFE), pool.intern("t"), 0);
    auto resolved = ps.resolve();

    emitter.emit(resolved, pool);

    ASSERT_NE(mod->getFunction("apply_all_patches"), nullptr);
    ASSERT_NE(mod->getFunction("mora_rt_add_faction"), nullptr);

    std::string err;
    llvm::raw_string_ostream err_stream(err);
    EXPECT_FALSE(llvm::verifyModule(*mod, &err_stream)) << err;
}

TEST(IREmitterTest, MixedRtAndScalarPatches) {
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("test_mixed", ctx);

    auto addrlib = make_full_mock();
    mora::IREmitter emitter(ctx, *mod, addrlib);

    mora::PatchSet ps;
    mora::StringPool pool;
    // Scalar (weapon damage) + NPC keyword + NPC spell + armor rating
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(42), pool.intern("t"), 0);
    ps.add_patch(0x200, mora::FieldId::Keywords, mora::FieldOp::Add,
                 mora::Value::make_formid(0xAAAA), pool.intern("t"), 0);
    ps.add_patch(0x300, mora::FieldId::Spells, mora::FieldOp::Add,
                 mora::Value::make_formid(0xBBBB), pool.intern("t"), 0);
    ps.add_patch(0x400, mora::FieldId::ArmorRating, mora::FieldOp::Set,
                 mora::Value::make_int(100), pool.intern("t"), 0);
    auto resolved = ps.resolve();

    emitter.emit(resolved, pool);

    ASSERT_NE(mod->getFunction("apply_all_patches"), nullptr);

    std::string err;
    llvm::raw_string_ostream err_stream(err);
    EXPECT_FALSE(llvm::verifyModule(*mod, &err_stream)) << err;
}
