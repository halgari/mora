#include <gtest/gtest.h>
#include "mora/codegen/dll_builder.h"
#include "mora/codegen/address_library.h"
#include "mora/codegen/ir_emitter.h"
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <filesystem>
#include <fstream>

TEST(DLLBuilderTest, GenerateIR) {
    auto addrlib = mora::AddressLibrary::mock({{514351, 0x1EEBE10}});
    mora::DLLBuilder builder(addrlib);

    mora::PatchSet ps;
    mora::StringPool pool;
    ps.add_patch(0x12EB7, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(42), pool.intern("t"), 0);
    auto resolved = ps.resolve();

    llvm::LLVMContext ctx;
    auto mod = builder.generate_ir(resolved, pool, ctx);

    ASSERT_NE(mod, nullptr);
    EXPECT_EQ(mod->getTargetTriple().str(), "x86_64-pc-windows-msvc");

    auto* func = mod->getFunction("apply_all_patches");
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->arg_size(), 1u);

    std::string err;
    llvm::raw_string_ostream err_stream(err);
    EXPECT_FALSE(llvm::verifyModule(*mod, &err_stream)) << err;
}

TEST(DLLBuilderTest, CompileToObject) {
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("test", ctx);

    auto addrlib = mora::AddressLibrary::mock({{514351, 0x1EEBE10}});
    mora::IREmitter emitter(ctx, *mod, addrlib);

    mora::PatchSet ps;
    mora::StringPool pool;
    ps.add_patch(0x12EB7, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(42), pool.intern("t"), 0);
    auto resolved = ps.resolve();
    emitter.emit(resolved, pool);

    mora::DLLBuilder builder(addrlib);
    std::string error;
    auto obj_path = std::filesystem::temp_directory_path() / "mora_test.obj";
    bool ok = builder.compile_to_object(*mod, obj_path, error);
    EXPECT_TRUE(ok) << error;
    EXPECT_TRUE(std::filesystem::exists(obj_path));

    // Verify it's a COFF object (x86-64 COFF magic: 0x8664)
    if (ok) {
        std::ifstream f(obj_path, std::ios::binary);
        char magic[2];
        f.read(magic, 2);
        EXPECT_EQ(static_cast<uint8_t>(magic[0]), 0x64);
        EXPECT_EQ(static_cast<uint8_t>(magic[1]), 0x86);
        std::filesystem::remove(obj_path);
    }
}

TEST(DLLBuilderTest, CompileEmptyPatchSet) {
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("test_empty", ctx);

    auto addrlib = mora::AddressLibrary::mock({{514351, 0x1EEBE10}});
    mora::IREmitter emitter(ctx, *mod, addrlib);

    mora::ResolvedPatchSet empty;
    mora::StringPool pool;
    emitter.emit(empty, pool);

    mora::DLLBuilder builder(addrlib);
    std::string error;
    auto obj_path = std::filesystem::temp_directory_path() / "mora_test_empty.obj";
    bool ok = builder.compile_to_object(*mod, obj_path, error);
    EXPECT_TRUE(ok) << error;

    if (ok) {
        EXPECT_TRUE(std::filesystem::exists(obj_path));
        EXPECT_GT(std::filesystem::file_size(obj_path), 0u);
        std::filesystem::remove(obj_path);
    }
}

TEST(DLLBuilderTest, CompileMultiplePatches) {
    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("test_multi", ctx);

    auto addrlib = mora::AddressLibrary::mock({{514351, 0x1EEBE10}});
    mora::IREmitter emitter(ctx, *mod, addrlib);

    mora::PatchSet ps;
    mora::StringPool pool;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(25), pool.intern("t"), 0);
    ps.add_patch(0x200, mora::FieldId::GoldValue, mora::FieldOp::Set,
                 mora::Value::make_int(100), pool.intern("t"), 0);
    ps.add_patch(0x300, mora::FieldId::Weight, mora::FieldOp::Set,
                 mora::Value::make_float(5.0), pool.intern("t"), 0);
    auto resolved = ps.resolve();
    emitter.emit(resolved, pool);

    mora::DLLBuilder builder(addrlib);
    std::string error;
    auto obj_path = std::filesystem::temp_directory_path() / "mora_test_multi.obj";
    bool ok = builder.compile_to_object(*mod, obj_path, error);
    EXPECT_TRUE(ok) << error;

    if (ok) {
        EXPECT_TRUE(std::filesystem::exists(obj_path));
        // Should be larger than the empty case since it has actual patch code
        EXPECT_GT(std::filesystem::file_size(obj_path), 100u);
        std::filesystem::remove(obj_path);
    }
}
