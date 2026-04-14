#include "mora/model/relations.h"

namespace mora::model {

// Relations are added incrementally. See Task 5 for the initial seed.
const RelationEntry kRelations[] = {
    { .namespace_ = "__sentinel__", .name = "__sentinel__", .arg_count = 0 },
};
const size_t kRelationCount = 0;

} // namespace mora::model
