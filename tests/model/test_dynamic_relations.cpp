#include "mora/model/relations.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(DynamicRelations, PlayerGoldIsCountable) {
    const auto* r = find_relation("player", "gold", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->cardinality, Cardinality::Countable);
    EXPECT_EQ(r->value_type, RelValueType::Int);
    EXPECT_EQ(r->apply_handler, HandlerId::PlayerAddGold);
}

TEST(DynamicRelations, PlayerNotificationIsSet) {
    const auto* r = find_relation("player", "notification", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->cardinality, Cardinality::Set);
}

TEST(DynamicRelations, EventEnteredLocationIsEventSource) {
    const auto* r = find_relation("event", "entered_location", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->source, RelationSourceKind::Event);
}

TEST(DynamicRelations, WorldTimeOfDayExists) {
    const auto* r = find_relation("world", "time_of_day", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
}

TEST(DynamicRelations, EventCombatStateChangedIsEventSource) {
    const auto* r = find_relation("event", "combat_state_changed", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->source, RelationSourceKind::Event);
}
