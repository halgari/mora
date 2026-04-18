#include "mora_skyrim_compile/types.h"

#include "mora/core/type.h"
#include "mora/ext/extension.h"

namespace mora_skyrim_compile {

void register_all_nominal_types(mora::ext::ExtensionContext& ctx) {
    // FormID is the base nominal; per-form-type subtypes layer over the
    // same physical (Int32). A later plan can formalize nominal
    // subtyping if needed; for now each is a peer alias.
    auto const* i32 = mora::types::int32();

    ctx.register_nominal_type("FormID",     i32);
    ctx.register_nominal_type("NpcID",      i32);
    ctx.register_nominal_type("WeaponID",   i32);
    ctx.register_nominal_type("ArmorID",    i32);
    ctx.register_nominal_type("KeywordID",  i32);
    ctx.register_nominal_type("FactionID",  i32);
    ctx.register_nominal_type("SpellID",    i32);
    ctx.register_nominal_type("PerkID",     i32);
    ctx.register_nominal_type("QuestID",    i32);
    ctx.register_nominal_type("LocationID", i32);
    ctx.register_nominal_type("CellID",     i32);
    ctx.register_nominal_type("RaceID",     i32);
}

} // namespace mora_skyrim_compile
