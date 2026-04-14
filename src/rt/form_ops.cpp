// Minimal runtime helpers — only get_form_type and get_field_offset remain.
// All patch application is now in patch_walker.cpp using typed CommonLibSSE-NG access.

#include "mora/rt/form_ops.h"
#include "mora/data/form_model.h"

using namespace mora;

namespace mora::rt {

uint64_t get_field_offset(uint8_t ft, uint16_t field_id) {
    return model::field_offset_for(ft, static_cast<FieldId>(field_id));
}

} // namespace mora::rt
