#pragma once

#include "mora/codegen/address_library.h"
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <filesystem>
#include <memory>
#include <string>

namespace mora {

class DLLBuilder {
public:
    explicit DLLBuilder(const AddressLibrary& addrlib);

    struct BuildResult {
        bool success = false;
        std::string error;
        std::filesystem::path output_path;
        size_t patch_count = 0;
        double ir_gen_ms = 0;
        double lto_ms = 0;
        double compile_ms = 0;
        double link_ms = 0;
    };

    // Full in-process pipeline: patches → DLL on disk
    // rt_lib_path: path to mora_rt.lib (LTO-capable static library)
    //              if empty, links without runtime support (standalone IR only)
    BuildResult build(const ResolvedPatchSet& patches,
                      StringPool& pool,
                      const std::filesystem::path& output_dir,
                      const std::filesystem::path& rt_lib_path = {});

    // For testing: generate IR module only
    std::unique_ptr<llvm::Module> generate_ir(const ResolvedPatchSet& patches,
                                              StringPool& pool,
                                              llvm::LLVMContext& ctx);

    // For testing: compile module to object in memory, write to file
    bool compile_to_object(llvm::Module& mod,
                           const std::filesystem::path& obj_path,
                           std::string& error);

private:
    // Run LLVM optimization passes (in-memory)
    void optimize(llvm::Module& mod);

    // Link .obj → .dll using LLD library API (in-process)
    bool link_dll_in_process(const std::filesystem::path& obj_path,
                              const std::filesystem::path& dll_path,
                              const std::filesystem::path& rt_lib_path,
                              std::string& error);

    const AddressLibrary& addrlib_;
};

} // namespace mora
