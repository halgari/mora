#include "mora/model/handlers.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(HandlerIds, RefAddKeywordRegistered) {
    ASSERT_NE(find_handler(HandlerId::RefAddKeyword), nullptr);
}
TEST(HandlerIds, RefRemoveKeywordRegistered) {
    ASSERT_NE(find_handler(HandlerId::RefRemoveKeyword), nullptr);
}
TEST(HandlerIds, PlayerAddGoldRegistered) {
    ASSERT_NE(find_handler(HandlerId::PlayerAddGold), nullptr);
}
TEST(HandlerIds, RefReadCurrentLocationRegistered) {
    ASSERT_NE(find_handler(HandlerId::RefReadCurrentLocation), nullptr);
}
