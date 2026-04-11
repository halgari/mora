#include "mora/codegen/dll_builder.h"
#include "mora/codegen/ir_emitter.h"

// LLVM core
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Passes/PassBuilder.h>

// LLVM target
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>

// LLVM bitcode
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/SmallVectorMemoryBuffer.h>

// LLD library API
#include <lld/Common/Driver.h>
LLD_HAS_DRIVER(coff)

#include <chrono>
#include <cstdlib>

namespace mora {

DLLBuilder::DLLBuilder(const AddressLibrary& addrlib)
    : addrlib_(addrlib) {}

// ── IR Generation ──────────────────────────────────────────────────

std::unique_ptr<llvm::Module> DLLBuilder::generate_ir(
        const ResolvedPatchSet& patches,
        StringPool& pool,
        llvm::LLVMContext& ctx) {
    auto mod = std::make_unique<llvm::Module>("mora_patches", ctx);
    mod->setTargetTriple(llvm::Triple("x86_64-pc-windows-msvc"));

    IREmitter emitter(ctx, *mod, addrlib_);
    emitter.emit(patches, pool);

    return mod;
}

// ── LTO Merge ──────────────────────────────────────────────────────

bool DLLBuilder::lto_merge(llvm::Module& patch_mod,
                            const std::filesystem::path& rt_bc_path,
                            std::string& error) {
    // Load the bitcode file into a memory buffer
    auto buf_or_err = llvm::MemoryBuffer::getFile(rt_bc_path.string());
    if (!buf_or_err) {
        error = "Failed to read " + rt_bc_path.string() + ": " +
                buf_or_err.getError().message();
        return false;
    }

    // Parse the bitcode into a Module
    auto mod_or_err = llvm::parseBitcodeFile(
        buf_or_err.get()->getMemBufferRef(), patch_mod.getContext());
    if (!mod_or_err) {
        error = "Failed to parse bitcode: " +
                llvm::toString(mod_or_err.takeError());
        return false;
    }

    auto rt_mod = std::move(*mod_or_err);
    rt_mod->setTargetTriple(patch_mod.getTargetTriple());
    rt_mod->setDataLayout(patch_mod.getDataLayout());

    // Link mora_rt into the patch module (LTO merge)
    if (llvm::Linker::linkModules(patch_mod, std::move(rt_mod))) {
        error = "LTO link failed";
        return false;
    }

    return true;
}

// ── Optimization ───────────────────────────────────────────────────

void DLLBuilder::optimize(llvm::Module& mod) {
    // Use the new pass manager for optimization
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;

    llvm::PassBuilder pb;
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);

    // O2 optimization level — good balance of speed and compile time
    auto mpm = pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    mpm.run(mod, mam);
}

// ── Compile to Object ──────────────────────────────────────────────

bool DLLBuilder::compile_to_object(llvm::Module& mod,
                                    const std::filesystem::path& obj_path,
                                    std::string& error) {
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();

    auto triple = llvm::Triple("x86_64-pc-windows-msvc");
    mod.setTargetTriple(triple);

    std::string target_error;
    auto* target = llvm::TargetRegistry::lookupTarget(triple, target_error);
    if (!target) { error = target_error; return false; }

    auto tm = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(triple, "x86-64", "", {},
                                    llvm::Reloc::PIC_, llvm::CodeModel::Small,
                                    llvm::CodeGenOptLevel::Aggressive));
    if (!tm) { error = "Failed to create target machine"; return false; }

    mod.setDataLayout(tm->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream out(obj_path.string(), ec);
    if (ec) { error = ec.message(); return false; }

    llvm::legacy::PassManager pm;
    if (tm->addPassesToEmitFile(pm, out, nullptr,
                                llvm::CodeGenFileType::ObjectFile)) {
        error = "Target machine cannot emit object files";
        return false;
    }

    pm.run(mod);
    out.flush();
    return true;
}

// ── Link DLL (in-process via LLD library) ──────────────────────────

bool DLLBuilder::link_dll_in_process(const std::filesystem::path& obj_path,
                                      const std::filesystem::path& dll_path,
                                      std::string& error) {
    const char* home_env = std::getenv("HOME");
    std::string xwin = std::string(home_env ? home_env : "") + "/.xwin";

    // Build lld-link command line arguments
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

    // Convert to const char* array for LLD
    std::vector<const char*> args;
    for (auto& s : arg_strings) args.push_back(s.c_str());

    // Call LLD's COFF linker in-process
    llvm::raw_null_ostream null_out;
    lld::Result link_result = lld::lldMain(args, null_out, null_out,
                                            {{lld::WinLink, &lld::coff::link}});

    if (link_result.retCode != 0) {
        error = "LLD linking failed";
        return false;
    }

    return true;
}

// ── Full Pipeline ──────────────────────────────────────────────────

DLLBuilder::BuildResult DLLBuilder::build(
        const ResolvedPatchSet& patches,
        StringPool& pool,
        const std::filesystem::path& output_dir,
        const std::filesystem::path& rt_bitcode_path) {
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

    // Verify IR
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

    // Step 2: LTO merge with mora_rt.bc (if provided)
    if (!rt_bitcode_path.empty() && std::filesystem::exists(rt_bitcode_path)) {
        if (!lto_merge(*mod, rt_bitcode_path, result.error)) {
            return result;
        }
    }

    // Step 3: Optimize (LTO inlining, constant folding, dead code elimination)
    optimize(*mod);

    auto t2 = std::chrono::steady_clock::now();
    result.lto_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    // Step 4: Compile to object
    auto obj_path = output_dir / "mora_patches.obj";
    if (!compile_to_object(*mod, obj_path, result.error)) {
        return result;
    }

    auto t3 = std::chrono::steady_clock::now();
    result.compile_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    // Step 5: Link DLL (in-process via LLD)
    auto dll_path = output_dir / "MoraRuntime.dll";
    if (!link_dll_in_process(obj_path, dll_path, result.error)) {
        return result;
    }

    // Clean up intermediate .obj
    std::filesystem::remove(obj_path);
    // Also remove .lib if lld created one
    auto lib_path = output_dir / "MoraRuntime.lib";
    if (std::filesystem::exists(lib_path)) std::filesystem::remove(lib_path);

    auto t4 = std::chrono::steady_clock::now();
    result.link_ms = std::chrono::duration<double, std::milli>(t4 - t3).count();

    result.success = true;
    result.output_path = dll_path;
    result.patch_count = patches.patch_count();
    return result;
}

} // namespace mora
