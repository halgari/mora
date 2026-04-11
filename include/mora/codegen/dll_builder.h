#pragma once

#include "mora/codegen/address_library.h"
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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

    // Full pipeline: patches -> DLL
    BuildResult build(const ResolvedPatchSet& patches,
                      StringPool& pool,
                      const std::filesystem::path& output_dir);

    // Generate the IR module with patch data
    std::unique_ptr<llvm::Module> generate_ir(const ResolvedPatchSet& patches,
                                              StringPool& pool,
                                              llvm::LLVMContext& ctx);

    // Compile a module to a Windows object file (.obj)
    bool compile_to_object(llvm::Module& mod,
                           const std::filesystem::path& obj_path,
                           std::string& error);

    // Link object files to DLL using lld-link
    bool link_dll(const std::vector<std::filesystem::path>& obj_files,
                  const std::filesystem::path& dll_path,
                  std::string& error);

private:
    const AddressLibrary& addrlib_;
};

} // namespace mora
