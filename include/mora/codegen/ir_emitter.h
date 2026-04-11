#pragma once

#include "mora/codegen/address_library.h"
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>

namespace mora {

class IREmitter {
public:
    IREmitter(llvm::LLVMContext& ctx, llvm::Module& mod,
              const AddressLibrary& addrlib);

    // Emit the main patch function + static data
    void emit(const ResolvedPatchSet& patches, StringPool& pool);

private:
    // Emit declaration of bst_hashmap_lookup (linked from mora_rt.bc)
    llvm::Function* declare_hashmap_lookup();

    // Emit the apply_all_patches function body
    void emit_apply_function(const ResolvedPatchSet& patches, StringPool& pool);

    // Emit a single patch: form lookup + field write
    void emit_patch(llvm::IRBuilder<>& builder, llvm::Value* map_ptr,
                    const ResolvedPatch& rp, const FieldPatch& fp);

    // Emit a form lookup via bst_hashmap_lookup
    llvm::Value* emit_form_lookup(llvm::IRBuilder<>& builder,
                                   llvm::Value* map_ptr, uint32_t formid);

    // Emit a direct field write (GEP + store)
    void emit_field_write(llvm::IRBuilder<>& builder, llvm::Value* form_ptr,
                          uint8_t form_type, const FieldPatch& fp);

    llvm::LLVMContext& ctx_;
    llvm::Module& mod_;
    const AddressLibrary& addrlib_;

    llvm::Function* hashmap_lookup_fn_ = nullptr;
};

} // namespace mora
