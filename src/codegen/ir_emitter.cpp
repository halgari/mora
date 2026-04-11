#include "mora/codegen/ir_emitter.h"
#include "mora/rt/form_ops.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>

namespace mora {

namespace {

// FormType constants (matching skyrim_abi.h)
constexpr uint8_t kWeapon = 0x29;
constexpr uint8_t kArmor  = 0x1A;

// Infer expected form type from a FieldId.
// Returns 0 if the field could apply to multiple form types (needs runtime check).
uint8_t infer_form_type(FieldId field) {
    switch (field) {
        case FieldId::Damage:      return kWeapon;
        case FieldId::ArmorRating: return kArmor;
        default:                   return 0; // GoldValue, Weight, etc.
    }
}

// Get the LLVM store type for a field
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

// Address library ID for the global form map pointer
constexpr uint64_t kFormMapAddrLibId = 514351;

} // anonymous namespace

IREmitter::IREmitter(llvm::LLVMContext& ctx, llvm::Module& mod,
                     const AddressLibrary& addrlib)
    : ctx_(ctx), mod_(mod), addrlib_(addrlib) {}

llvm::Function* IREmitter::declare_hashmap_lookup() {
    // void* bst_hashmap_lookup(void* map, uint32_t formid)
    auto* ptr_ty = llvm::PointerType::get(ctx_, 0);
    auto* i32_ty = llvm::Type::getInt32Ty(ctx_);
    auto* fn_ty = llvm::FunctionType::get(ptr_ty, {ptr_ty, i32_ty}, false);
    auto* fn = llvm::Function::Create(fn_ty, llvm::GlobalValue::ExternalLinkage,
                                       "bst_hashmap_lookup", mod_);
    fn->setDoesNotThrow();
    return fn;
}

void IREmitter::emit(const ResolvedPatchSet& patches, StringPool& pool) {
    hashmap_lookup_fn_ = declare_hashmap_lookup();
    emit_apply_function(patches, pool);
}

void IREmitter::emit_apply_function(const ResolvedPatchSet& patches,
                                     StringPool& /*pool*/) {
    auto* ptr_ty = llvm::PointerType::get(ctx_, 0);
    auto* void_ty = llvm::Type::getVoidTy(ctx_);

    // void apply_all_patches(void* skyrim_base)
    auto* fn_ty = llvm::FunctionType::get(void_ty, {ptr_ty}, false);
    auto* func = llvm::Function::Create(fn_ty, llvm::GlobalValue::ExternalLinkage,
                                         "apply_all_patches", mod_);
    func->setDoesNotThrow();

    auto* entry_bb = llvm::BasicBlock::Create(ctx_, "entry", func);
    llvm::IRBuilder<> builder(entry_bb);

    llvm::Value* skyrim_base = func->getArg(0);

    // Load the form map pointer: *(skyrim_base + map_offset)
    auto map_offset_opt = addrlib_.resolve(kFormMapAddrLibId);
    uint64_t map_offset = map_offset_opt.value_or(0);

    auto* i8_ty = llvm::Type::getInt8Ty(ctx_);
    auto* offset_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), map_offset);
    auto* map_addr = builder.CreateGEP(i8_ty, skyrim_base, offset_val, "map_addr");
    auto* map_ptr = builder.CreateLoad(ptr_ty, map_addr, "map_ptr");

    // Emit patches
    auto sorted = patches.all_patches_sorted();
    for (const auto& rp : sorted) {
        for (const auto& fp : rp.fields) {
            emit_patch(builder, map_ptr, rp, fp);
        }
    }

    builder.CreateRetVoid();
}

void IREmitter::emit_patch(llvm::IRBuilder<>& builder, llvm::Value* map_ptr,
                            const ResolvedPatch& rp, const FieldPatch& fp) {
    auto store_kind = get_store_kind(fp.field);
    if (store_kind == StoreKind::Unsupported) {
        return; // Skip unsupported fields (Name, Keywords, etc.)
    }

    uint8_t inferred_type = infer_form_type(fp.field);

    // For fields that could apply to multiple form types (GoldValue, Weight),
    // we need to emit both weapon and armor variants with a runtime type check.
    if (inferred_type != 0) {
        // Single known form type
        uint64_t offset = rt::get_field_offset(inferred_type,
                                                static_cast<uint16_t>(fp.field));
        if (offset == 0) return;

        llvm::Value* form_ptr = emit_form_lookup(builder, map_ptr, rp.target_formid);

        // Null check
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
        // GoldValue or Weight: need runtime form type check
        llvm::Value* form_ptr = emit_form_lookup(builder, map_ptr, rp.target_formid);

        auto* func = builder.GetInsertBlock()->getParent();
        auto* check_type_bb = llvm::BasicBlock::Create(ctx_, "check_type", func);
        auto* write_weapon_bb = llvm::BasicBlock::Create(ctx_, "write_weapon", func);
        auto* write_armor_bb = llvm::BasicBlock::Create(ctx_, "write_armor", func);
        auto* skip_bb = llvm::BasicBlock::Create(ctx_, "skip", func);

        // Null check
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
            llvm::ConstantInt::get(i8_ty, kWeapon),
            "is_weapon");
        builder.CreateCondBr(is_weapon, write_weapon_bb, write_armor_bb);

        // Weapon path
        builder.SetInsertPoint(write_weapon_bb);
        {
            uint64_t offset = rt::get_field_offset(kWeapon,
                                                    static_cast<uint16_t>(fp.field));
            if (offset != 0) {
                emit_field_write(builder, form_ptr, kWeapon, fp);
            }
        }
        builder.CreateBr(skip_bb);

        // Armor path (also serves as fallthrough for unknown types -> skip)
        builder.SetInsertPoint(write_armor_bb);
        {
            auto* is_armor = builder.CreateICmpEQ(
                form_type_val,
                llvm::ConstantInt::get(i8_ty, kArmor),
                "is_armor");
            auto* do_armor_bb = llvm::BasicBlock::Create(ctx_, "do_armor", func);
            builder.CreateCondBr(is_armor, do_armor_bb, skip_bb);

            builder.SetInsertPoint(do_armor_bb);
            uint64_t offset = rt::get_field_offset(kArmor,
                                                    static_cast<uint16_t>(fp.field));
            if (offset != 0) {
                emit_field_write(builder, form_ptr, kArmor, fp);
            }
            builder.CreateBr(skip_bb);
        }

        builder.SetInsertPoint(skip_bb);
    }
}

llvm::Value* IREmitter::emit_form_lookup(llvm::IRBuilder<>& builder,
                                          llvm::Value* map_ptr,
                                          uint32_t formid) {
    auto* i32_ty = llvm::Type::getInt32Ty(ctx_);
    auto* formid_val = llvm::ConstantInt::get(i32_ty, formid);
    return builder.CreateCall(hashmap_lookup_fn_, {map_ptr, formid_val}, "form_ptr");
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

} // namespace mora
