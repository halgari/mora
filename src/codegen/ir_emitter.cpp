#include "mora/codegen/ir_emitter.h"
#include "mora/data/form_constants.h"
#include "mora/rt/form_ops.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>

namespace mora {

namespace {

// Infer the expected form type from a FieldId. Returns 0 if the field
// could apply to multiple form types (runtime dispatch needed).
uint8_t infer_form_type(FieldId field) {
    switch (field) {
        case FieldId::Damage:      return form_type::kWeapon;
        case FieldId::ArmorRating: return form_type::kArmor;
        case FieldId::Spells:      return form_type::kNPC;
        case FieldId::Perks:       return form_type::kNPC;
        case FieldId::Factions:    return form_type::kNPC;
        default:                   return 0; // Keywords, GoldValue, Weight, etc.
    }
}

enum class StoreKind { Int16, Int32, Float, Unsupported };

StoreKind get_store_kind(FieldId field) {
    switch (field) {
        case FieldId::Damage:      return StoreKind::Int16;
        case FieldId::ArmorRating: return StoreKind::Int32;
        case FieldId::GoldValue:   return StoreKind::Int32;
        case FieldId::Weight:      return StoreKind::Float;
        default:                   return StoreKind::Unsupported;
    }
}

} // anonymous namespace

IREmitter::IREmitter(llvm::LLVMContext& ctx, llvm::Module& mod,
                     const AddressLibrary& addrlib)
    : ctx_(ctx), mod_(mod), addrlib_(addrlib) {}

llvm::Function* IREmitter::declare_lookup_form() {
    // void* mora_rt_lookup_form(uint32_t formid)
    auto* ptr_ty = llvm::PointerType::get(ctx_, 0);
    auto* i32_ty = llvm::Type::getInt32Ty(ctx_);
    auto* fn_ty = llvm::FunctionType::get(ptr_ty, {i32_ty}, false);
    auto* fn = llvm::Function::Create(fn_ty, llvm::GlobalValue::ExternalLinkage,
                                       "mora_rt_lookup_form", mod_);
    fn->setDoesNotThrow();
    return fn;
}

llvm::Function* IREmitter::declare_simple_rt(const char* name) {
    // void fn(void* form, void* target)
    auto* ptr_ty = llvm::PointerType::get(ctx_, 0);
    auto* void_ty = llvm::Type::getVoidTy(ctx_);
    auto* fn_ty = llvm::FunctionType::get(void_ty, {ptr_ty, ptr_ty}, false);
    auto* fn = llvm::Function::Create(fn_ty, llvm::GlobalValue::ExternalLinkage,
                                       name, mod_);
    fn->setDoesNotThrow();
    return fn;
}

llvm::Function* IREmitter::declare_write_name() {
    // void mora_rt_write_name(void* form, const char* name)
    auto* ptr_ty = llvm::PointerType::get(ctx_, 0);
    auto* void_ty = llvm::Type::getVoidTy(ctx_);
    auto* fn_ty = llvm::FunctionType::get(void_ty, {ptr_ty, ptr_ty}, false);
    auto* fn = llvm::Function::Create(fn_ty, llvm::GlobalValue::ExternalLinkage,
                                       "mora_rt_write_name", mod_);
    fn->setDoesNotThrow();
    return fn;
}

void IREmitter::emit(const ResolvedPatchSet& patches, StringPool& pool) {
    lookup_form_fn_    = declare_lookup_form();
    add_keyword_fn_    = declare_simple_rt("mora_rt_add_keyword");
    remove_keyword_fn_ = declare_simple_rt("mora_rt_remove_keyword");
    add_spell_fn_      = declare_simple_rt("mora_rt_add_spell");
    add_perk_fn_       = declare_simple_rt("mora_rt_add_perk");
    add_faction_fn_    = declare_simple_rt("mora_rt_add_faction");
    write_name_fn_     = declare_write_name();

    emit_apply_function(patches, pool);
}

void IREmitter::emit_apply_function(const ResolvedPatchSet& patches,
                                     StringPool& pool) {
    auto* void_ty = llvm::Type::getVoidTy(ctx_);

    // Emit patch count as a global constant for runtime introspection.
    auto* i32_ty = llvm::Type::getInt32Ty(ctx_);
    auto sorted = patches.all_patches_sorted();
    uint32_t total_patches = 0;
    for (const auto& rp : sorted) total_patches += rp.fields.size();
    new llvm::GlobalVariable(
        mod_, i32_ty, true, llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantInt::get(i32_ty, total_patches), "mora_patch_count");

    // Flatten into a list of (ResolvedPatch*, FieldPatch*) pairs
    struct PatchEntry { const ResolvedPatch* rp; const FieldPatch* fp; };
    std::vector<PatchEntry> all_entries;
    all_entries.reserve(total_patches);
    for (const auto& rp : sorted) {
        for (const auto& fp : rp.fields) {
            all_entries.push_back({&rp, &fp});
        }
    }

    // Split into chunks of ~1000 patches.
    // Each chunk is a separate internal function: _mora_chunk_N()
    // LLVM's backend is super-linear on basic block size, so many small functions
    // compile orders of magnitude faster than 1 giant function.
    constexpr size_t kChunkSize = 1000;
    auto* chunk_fn_ty = llvm::FunctionType::get(void_ty, {}, false);

    std::vector<llvm::Function*> chunk_fns;
    for (size_t start = 0; start < all_entries.size(); start += kChunkSize) {
        size_t end = std::min(start + kChunkSize, all_entries.size());
        std::string name = "_mora_chunk_" + std::to_string(chunk_fns.size());

        auto* chunk_fn = llvm::Function::Create(chunk_fn_ty,
            llvm::GlobalValue::InternalLinkage, name, mod_);
        chunk_fn->setDoesNotThrow();

        auto* bb = llvm::BasicBlock::Create(ctx_, "entry", chunk_fn);
        llvm::IRBuilder<> cb(bb);

        for (size_t i = start; i < end; i++) {
            auto& [rp, fp] = all_entries[i];
            if (fp->field == FieldId::Name && fp->op == FieldOp::Set) {
                if (fp->value.kind() == Value::Kind::String) {
                    auto str = pool.get(fp->value.as_string());
                    std::string s(str);
                    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
                        s = s.substr(1, s.size() - 2);
                    }
                    emit_name_write(cb, rp->target_formid, s.c_str());
                }
                continue;
            }
            emit_patch(cb, *rp, *fp);
        }

        cb.CreateRetVoid();
        chunk_fns.push_back(chunk_fn);
    }

    // Entry function: apply_all_patches() — no parameters
    auto* fn_ty = llvm::FunctionType::get(void_ty, {}, false);
    auto* func = llvm::Function::Create(fn_ty, llvm::GlobalValue::ExternalLinkage,
                                         "apply_all_patches", mod_);
    func->setDoesNotThrow();

    auto* entry_bb = llvm::BasicBlock::Create(ctx_, "entry", func);
    llvm::IRBuilder<> builder(entry_bb);

    // Call each chunk
    for (auto* chunk_fn : chunk_fns) {
        builder.CreateCall(chunk_fn, {});
    }

    builder.CreateRetVoid();
}

void IREmitter::emit_patch(llvm::IRBuilder<>& builder,
                            const ResolvedPatch& rp, const FieldPatch& fp) {
    // NPC-array mutations require the value to be a resolved FormID.
    if (fp.field == FieldId::Keywords) {
        if (fp.value.kind() != Value::Kind::FormID) return;
        if (fp.op == FieldOp::Add) {
            emit_simple_call(builder, rp.target_formid,
                             fp.value.as_formid(), add_keyword_fn_, "kw_add");
        } else if (fp.op == FieldOp::Remove) {
            emit_simple_call(builder, rp.target_formid,
                             fp.value.as_formid(), remove_keyword_fn_, "kw_rem");
        }
        return;
    }
    if (fp.field == FieldId::Spells && fp.op == FieldOp::Add) {
        if (fp.value.kind() != Value::Kind::FormID) return;
        emit_simple_call(builder, rp.target_formid,
                         fp.value.as_formid(), add_spell_fn_, "spell_add");
        return;
    }
    if (fp.field == FieldId::Perks && fp.op == FieldOp::Add) {
        if (fp.value.kind() != Value::Kind::FormID) return;
        emit_simple_call(builder, rp.target_formid,
                         fp.value.as_formid(), add_perk_fn_, "perk_add");
        return;
    }
    if (fp.field == FieldId::Factions && fp.op == FieldOp::Add) {
        if (fp.value.kind() != Value::Kind::FormID) return;
        emit_simple_call(builder, rp.target_formid,
                         fp.value.as_formid(), add_faction_fn_, "faction_add");
        return;
    }

    auto store_kind = get_store_kind(fp.field);
    if (store_kind == StoreKind::Unsupported) {
        return; // Items, Level, Race, EditorId — not yet supported
    }

    uint8_t inferred_type = infer_form_type(fp.field);

    // Unambiguous form type — single path.
    if (inferred_type != 0) {
        uint64_t offset = rt::get_field_offset(inferred_type,
                                                static_cast<uint16_t>(fp.field));
        if (offset == 0) return;

        llvm::Value* form_ptr = emit_form_lookup(builder, rp.target_formid);

        auto* func = builder.GetInsertBlock()->getParent();
        auto* do_write_bb = llvm::BasicBlock::Create(ctx_, "do_write", func);
        auto* skip_bb = llvm::BasicBlock::Create(ctx_, "skip", func);

        auto* null_val = llvm::ConstantPointerNull::get(
            llvm::PointerType::get(ctx_, 0));
        auto* not_null = builder.CreateICmpNE(form_ptr, null_val, "not_null");
        builder.CreateCondBr(not_null, do_write_bb, skip_bb);

        builder.SetInsertPoint(do_write_bb);
        emit_field_write(builder, form_ptr, inferred_type, fp);
        builder.CreateBr(skip_bb);

        builder.SetInsertPoint(skip_bb);
    } else {
        // GoldValue or Weight: runtime form type check (Weapon vs Armor).
        llvm::Value* form_ptr = emit_form_lookup(builder, rp.target_formid);

        auto* func = builder.GetInsertBlock()->getParent();
        auto* check_type_bb = llvm::BasicBlock::Create(ctx_, "check_type", func);
        auto* write_weapon_bb = llvm::BasicBlock::Create(ctx_, "write_weapon", func);
        auto* write_armor_bb = llvm::BasicBlock::Create(ctx_, "write_armor", func);
        auto* skip_bb = llvm::BasicBlock::Create(ctx_, "skip", func);

        auto* null_val = llvm::ConstantPointerNull::get(
            llvm::PointerType::get(ctx_, 0));
        auto* not_null = builder.CreateICmpNE(form_ptr, null_val, "not_null");
        builder.CreateCondBr(not_null, check_type_bb, skip_bb);

        // Read form type byte at offset 0x1A
        builder.SetInsertPoint(check_type_bb);
        auto* i8_ty = llvm::Type::getInt8Ty(ctx_);
        auto* type_offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), 0x1A);
        auto* type_addr = builder.CreateGEP(i8_ty, form_ptr, type_offset, "type_addr");
        auto* form_type_val = builder.CreateLoad(i8_ty, type_addr, "form_type");

        auto* is_weapon = builder.CreateICmpEQ(
            form_type_val,
            llvm::ConstantInt::get(i8_ty, form_type::kWeapon),
            "is_weapon");
        builder.CreateCondBr(is_weapon, write_weapon_bb, write_armor_bb);

        // Weapon path
        builder.SetInsertPoint(write_weapon_bb);
        {
            uint64_t offset = rt::get_field_offset(form_type::kWeapon,
                                                    static_cast<uint16_t>(fp.field));
            if (offset != 0) {
                emit_field_write(builder, form_ptr, form_type::kWeapon, fp);
            }
        }
        builder.CreateBr(skip_bb);

        // Armor path (also serves as fallthrough for unknown types -> skip)
        builder.SetInsertPoint(write_armor_bb);
        {
            auto* is_armor = builder.CreateICmpEQ(
                form_type_val,
                llvm::ConstantInt::get(i8_ty, form_type::kArmor),
                "is_armor");
            auto* do_armor_bb = llvm::BasicBlock::Create(ctx_, "do_armor", func);
            builder.CreateCondBr(is_armor, do_armor_bb, skip_bb);

            builder.SetInsertPoint(do_armor_bb);
            uint64_t offset = rt::get_field_offset(form_type::kArmor,
                                                    static_cast<uint16_t>(fp.field));
            if (offset != 0) {
                emit_field_write(builder, form_ptr, form_type::kArmor, fp);
            }
            builder.CreateBr(skip_bb);
        }

        builder.SetInsertPoint(skip_bb);
    }
}

llvm::Value* IREmitter::emit_form_lookup(llvm::IRBuilder<>& builder,
                                          uint32_t formid) {
    auto* i32_ty = llvm::Type::getInt32Ty(ctx_);
    auto* formid_val = llvm::ConstantInt::get(i32_ty, formid);
    return builder.CreateCall(lookup_form_fn_, {formid_val}, "form_ptr");
}

void IREmitter::emit_field_write(llvm::IRBuilder<>& builder,
                                  llvm::Value* form_ptr,
                                  uint8_t form_type,
                                  const FieldPatch& fp) {
    uint64_t offset = rt::get_field_offset(form_type,
                                            static_cast<uint16_t>(fp.field));
    if (offset == 0) return;

    auto* i8_ty = llvm::Type::getInt8Ty(ctx_);
    auto* offset_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), offset);
    auto* field_addr = builder.CreateGEP(i8_ty, form_ptr, offset_val, "field_ptr");

    auto store_kind = get_store_kind(fp.field);
    switch (store_kind) {
        case StoreKind::Int16: {
            auto* i16_ty = llvm::Type::getInt16Ty(ctx_);
            auto* val = llvm::ConstantInt::get(i16_ty,
                                                static_cast<uint16_t>(fp.value.as_int()));
            builder.CreateStore(val, field_addr);
            break;
        }
        case StoreKind::Int32: {
            auto* i32_ty = llvm::Type::getInt32Ty(ctx_);
            auto* val = llvm::ConstantInt::get(i32_ty,
                                                static_cast<uint32_t>(fp.value.as_int()));
            builder.CreateStore(val, field_addr);
            break;
        }
        case StoreKind::Float: {
            auto* f32_ty = llvm::Type::getFloatTy(ctx_);
            auto* val = llvm::ConstantFP::get(f32_ty,
                                               static_cast<double>(fp.value.as_float()));
            builder.CreateStore(val, field_addr);
            break;
        }
        case StoreKind::Unsupported:
            break;
    }
}

void IREmitter::emit_simple_call(llvm::IRBuilder<>& builder,
                                  uint32_t target_formid, uint32_t element_formid,
                                  llvm::Function* rt_fn, const char* label) {
    auto* ptr_ty = llvm::PointerType::get(ctx_, 0);

    llvm::Value* form_ptr = emit_form_lookup(builder, target_formid);
    llvm::Value* elem_ptr = emit_form_lookup(builder, element_formid);

    // Null-check both
    auto* null_val = llvm::ConstantPointerNull::get(ptr_ty);
    auto* form_ok = builder.CreateICmpNE(form_ptr, null_val, "form_ok");
    auto* elem_ok = builder.CreateICmpNE(elem_ptr, null_val, "elem_ok");
    auto* both_ok = builder.CreateAnd(form_ok, elem_ok, "both_ok");

    auto* func = builder.GetInsertBlock()->getParent();
    auto* do_bb = llvm::BasicBlock::Create(ctx_, std::string("do_") + label, func);
    auto* skip_bb = llvm::BasicBlock::Create(ctx_, std::string("skip_") + label, func);
    builder.CreateCondBr(both_ok, do_bb, skip_bb);

    builder.SetInsertPoint(do_bb);
    builder.CreateCall(rt_fn, {form_ptr, elem_ptr});
    builder.CreateBr(skip_bb);

    builder.SetInsertPoint(skip_bb);
}

void IREmitter::emit_name_write(llvm::IRBuilder<>& builder,
                                 uint32_t form_id, const char* name_str) {
    auto* ptr_ty = llvm::PointerType::get(ctx_, 0);

    llvm::Value* form_ptr = emit_form_lookup(builder, form_id);

    auto* null_val = llvm::ConstantPointerNull::get(ptr_ty);
    auto* not_null = builder.CreateICmpNE(form_ptr, null_val, "name_form_ok");

    auto* func = builder.GetInsertBlock()->getParent();
    auto* do_write_bb = llvm::BasicBlock::Create(ctx_, "do_name_write", func);
    auto* skip_bb = llvm::BasicBlock::Create(ctx_, "skip_name", func);
    builder.CreateCondBr(not_null, do_write_bb, skip_bb);

    builder.SetInsertPoint(do_write_bb);
    auto* name_const = builder.CreateGlobalString(name_str, "name_str");
    builder.CreateCall(write_name_fn_, {form_ptr, name_const});
    builder.CreateBr(skip_bb);

    builder.SetInsertPoint(skip_bb);
}

} // namespace mora
