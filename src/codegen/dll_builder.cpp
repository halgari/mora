#include "mora/codegen/dll_builder.h"
#include "mora/codegen/ir_emitter.h"

// LLVM core
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>

// LLVM target
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>

// LLVM support
#include <llvm/Support/raw_ostream.h>

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

    // Force-include and export SKSE plugin symbols from mora_rt.lib
    arg_strings.push_back("/INCLUDE:SKSEPlugin_Version");
    arg_strings.push_back("/INCLUDE:SKSEPlugin_Load");
    arg_strings.push_back("/EXPORT:SKSEPlugin_Version,DATA");
    arg_strings.push_back("/EXPORT:SKSEPlugin_Load");

    std::vector<const char*> args;
    for (auto& s : arg_strings) args.push_back(s.c_str());

    // Call LLD's COFF linker in-process
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

// ── Full Pipeline ──────────────────────────────────────────────────

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

} // namespace mora
