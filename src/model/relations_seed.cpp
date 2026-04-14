#include "mora/model/relations.h"
#include "mora/model/validate.h"

namespace mora::model {

// Relations are added incrementally. See Task 5 for the initial seed.
constexpr RelationEntry kRelations[] = {
    { .namespace_ = "__sentinel__", .name = "__sentinel__", .arg_count = 0 },
};
const size_t kRelationCount = 0;

static_assert(validate_all(kRelations, sizeof(kRelations) / sizeof(kRelations[0])),
              "kRelations fails validation — see individual helper checks");

} // namespace mora::model
