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

    // Emit a keyword add: look up both form and keyword, call add_keyword_to_form
    void emit_keyword_add(llvm::IRBuilder<>& builder, llvm::Value* map_ptr,
                           uint32_t form_id, uint32_t keyword_formid);

    // Declare add_keyword_to_form (linked from mora_rt.bc)
    llvm::Function* declare_add_keyword();

    // Declare mora_rt_write_name (linked from mora_rt.lib)
    llvm::Function* declare_write_name();

    // Emit a name write: look up form, call mora_rt_write_name with string constant
    void emit_name_write(llvm::IRBuilder<>& builder, llvm::Value* map_ptr,
                          uint32_t form_id, const char* name_str);

    llvm::LLVMContext& ctx_;
    llvm::Module& mod_;
    const AddressLibrary& addrlib_;

    llvm::Function* hashmap_lookup_fn_ = nullptr;
    llvm::Function* add_keyword_fn_ = nullptr;
    llvm::Function* write_name_fn_ = nullptr;
};

} // namespace mora
