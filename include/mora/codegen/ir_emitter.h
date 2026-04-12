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

    // Emit an NPC-array mutation (keyword add/remove, spell/perk/faction add).
    // `rt_fn` is the mora_rt_* function to call with the shared
    // (skyrim_base, form, target, singleton, alloc, dealloc) signature.
    void emit_memmgr_call(llvm::IRBuilder<>& builder, llvm::Value* map_ptr,
                          uint32_t target_formid, uint32_t element_formid,
                          llvm::Function* rt_fn, const char* label);

    // Declare an extern "C" function with the standard MemoryManager RT
    // signature: (ptr, ptr, ptr, i64, i64, i64) -> void.
    llvm::Function* declare_memmgr_rt(const char* name);

    // Declare mora_rt_write_name (linked from mora_rt.lib)
    llvm::Function* declare_write_name();

    // Emit a name write: look up form, call mora_rt_write_name with string constant
    void emit_name_write(llvm::IRBuilder<>& builder, llvm::Value* map_ptr,
                          uint32_t form_id, const char* name_str);

    // Resolve the six MemoryManager/BSFixedString offsets once per module.
    // Called from emit() after declaring RT functions.
    void resolve_engine_offsets();

    llvm::LLVMContext& ctx_;
    llvm::Module& mod_;
    const AddressLibrary& addrlib_;

    llvm::Function* hashmap_lookup_fn_ = nullptr;
    llvm::Function* add_keyword_fn_    = nullptr;
    llvm::Function* remove_keyword_fn_ = nullptr;
    llvm::Function* add_spell_fn_      = nullptr;
    llvm::Function* add_perk_fn_       = nullptr;
    llvm::Function* add_faction_fn_    = nullptr;
    llvm::Function* write_name_fn_     = nullptr;

    // Address Library offsets resolved at emit time (0 if unresolved).
    uint64_t mm_singleton_off_  = 0;
    uint64_t mm_allocate_off_   = 0;
    uint64_t mm_deallocate_off_ = 0;
    uint64_t bs_ctor8_off_      = 0;
    uint64_t bs_release8_off_   = 0;
};

} // namespace mora
