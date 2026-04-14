#include "mora/model/handlers.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(Handlers, NoneIsFirst) {
    EXPECT_EQ(static_cast<int>(HandlerId::None), 0);
}

TEST(Handlers, RegistryHasNoneEntry) {
    bool found = false;
    for (const auto& h : kHandlers) {
        if (h.id == HandlerId::None) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST(Handlers, FindHandlerById) {
    const HandlerEntry* e = find_handler(HandlerId::None);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->id, HandlerId::None);
}
