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
    // Declare mora_rt_lookup_form(uint32_t formid) -> ptr
    llvm::Function* declare_lookup_form();

    // Declare a simple RT function: (ptr form, ptr target) -> void
    llvm::Function* declare_simple_rt(const char* name);

    // Declare mora_rt_write_name(ptr form, ptr name) -> void
    llvm::Function* declare_write_name();

    // Emit the apply_all_patches function body
    void emit_apply_function(const ResolvedPatchSet& patches, StringPool& pool);

    // Emit a single patch: form lookup + field write
    void emit_patch(llvm::IRBuilder<>& builder,
                    const ResolvedPatch& rp, const FieldPatch& fp);

    // Emit a form lookup via mora_rt_lookup_form
    llvm::Value* emit_form_lookup(llvm::IRBuilder<>& builder, uint32_t formid);

    // Emit a direct field write (GEP + store)
    void emit_field_write(llvm::IRBuilder<>& builder, llvm::Value* form_ptr,
                          uint8_t form_type, const FieldPatch& fp);

    // Emit a two-argument RT call (form, target) with null checks.
    void emit_simple_call(llvm::IRBuilder<>& builder,
                          uint32_t target_formid, uint32_t element_formid,
                          llvm::Function* rt_fn, const char* label);

    // Emit a name write: look up form, call mora_rt_write_name with string constant
    void emit_name_write(llvm::IRBuilder<>& builder,
                          uint32_t form_id, const char* name_str);

    llvm::LLVMContext& ctx_;
    llvm::Module& mod_;
    const AddressLibrary& addrlib_;  // still needed for scalar field offsets

    llvm::Function* lookup_form_fn_    = nullptr;
    llvm::Function* add_keyword_fn_    = nullptr;
    llvm::Function* remove_keyword_fn_ = nullptr;
    llvm::Function* add_spell_fn_      = nullptr;
    llvm::Function* add_perk_fn_       = nullptr;
    llvm::Function* add_faction_fn_    = nullptr;
    llvm::Function* write_name_fn_     = nullptr;
};

} // namespace mora
