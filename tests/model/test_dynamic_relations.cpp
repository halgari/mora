#include "mora/model/relations.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(DynamicRelations, PlayerGoldIsCountable) {
    const auto* r = find_relation("player", "gold", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type.ctor, TypeCtor::Countable);
    EXPECT_EQ(r->type.elem, ElemType::Int);
    EXPECT_EQ(r->apply_handler, HandlerId::PlayerAddGold);
}

TEST(DynamicRelations, PlayerNotificationIsList) {
    const auto* r = find_relation("player", "notification", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type.ctor, TypeCtor::List);
}

TEST(DynamicRelations, EventEnteredLocationIsEventSource) {
    const auto* r = find_relation("event", "entered_location", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->source, RelationSourceKind::Event);
}

TEST(DynamicRelations, WorldTimeOfDayIsConst) {
    const auto* r = find_relation("world", "time_of_day", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type.ctor, TypeCtor::Const);
    EXPECT_EQ(r->type.elem, ElemType::Float);
}

TEST(DynamicRelations, EventCombatStateChangedIsEventSource) {
    const auto* r = find_relation("event", "combat_state_changed", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->source, RelationSourceKind::Event);
}
