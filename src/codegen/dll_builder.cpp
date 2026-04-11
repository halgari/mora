#include "mora/codegen/dll_builder.h"
#include "mora/codegen/ir_emitter.h"

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>

#include <chrono>
#include <cstdlib>
#include <fstream>

namespace mora {

DLLBuilder::DLLBuilder(const AddressLibrary& addrlib)
    : addrlib_(addrlib) {}

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

bool DLLBuilder::compile_to_object(llvm::Module& mod,
                                    const std::filesystem::path& obj_path,
                                    std::string& error) {
    // Initialize x86 target
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();

    auto triple = llvm::Triple("x86_64-pc-windows-msvc");
    mod.setTargetTriple(triple);

    std::string target_error;
    auto* target = llvm::TargetRegistry::lookupTarget(triple, target_error);
    if (!target) {
        error = target_error;
        return false;
    }

    auto target_machine = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(triple, "x86-64", "", {},
                                    llvm::Reloc::PIC_, llvm::CodeModel::Small,
                                    llvm::CodeGenOptLevel::Aggressive));
    if (!target_machine) {
        error = "Failed to create target machine";
        return false;
    }

    mod.setDataLayout(target_machine->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream out(obj_path.string(), ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    llvm::legacy::PassManager pm;
    if (target_machine->addPassesToEmitFile(pm, out, nullptr,
                                            llvm::CodeGenFileType::ObjectFile)) {
        error = "Target machine cannot emit object files";
        return false;
    }

    pm.run(mod);
    out.flush();
    return true;
}

bool DLLBuilder::link_dll(const std::vector<std::filesystem::path>& obj_files,
                           const std::filesystem::path& dll_path,
                           std::string& error) {
    std::string cmd = "lld-link /dll /nodefaultlib";
    cmd += " /out:" + dll_path.string();

    // Windows SDK libs via xwin
    const char* home_env = std::getenv("HOME");
    std::string xwin = std::string(home_env ? home_env : "") + "/.xwin";
    cmd += " /libpath:" + xwin + "/crt/lib/x86_64";
    cmd += " /libpath:" + xwin + "/sdk/lib/um/x86_64";
    cmd += " /libpath:" + xwin + "/sdk/lib/ucrt/x86_64";
    cmd += " msvcrt.lib ucrt.lib kernel32.lib";

    for (const auto& obj : obj_files) {
        cmd += " " + obj.string();
    }

    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        error = "lld-link failed with code " + std::to_string(ret);
        return false;
    }
    return true;
}

DLLBuilder::BuildResult DLLBuilder::build(
        const ResolvedPatchSet& patches,
        StringPool& pool,
        const std::filesystem::path& output_dir) {
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

    // Verify the generated IR
    {
        std::string verify_err;
        llvm::raw_string_ostream verify_stream(verify_err);
        if (llvm::verifyModule(*mod, &verify_stream)) {
            result.error = "IR verification failed: " + verify_err;
            return result;
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    result.ir_gen_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Step 2: Compile IR to object file
    auto ir_obj_path = output_dir / "mora_patches.obj";
    if (!compile_to_object(*mod, ir_obj_path, result.error)) {
        return result;
    }

    auto t2 = std::chrono::steady_clock::now();
    result.compile_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    // Step 3: Link to DLL
    auto dll_path = output_dir / "MoraRuntime.dll";
    std::vector<std::filesystem::path> obj_files = {ir_obj_path};

    if (!link_dll(obj_files, dll_path, result.error)) {
        return result;
    }

    auto t3 = std::chrono::steady_clock::now();
    result.link_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    result.success = true;
    result.output_path = dll_path;
    result.patch_count = patches.patch_count();
    return result;
}

} // namespace mora
