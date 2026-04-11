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
    EXPECT_EQ(func->arg_size(), 1u); // skyrim_base

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
