#include "mora/rt/handler_registry.h"
#include <gtest/gtest.h>

using namespace mora;
using namespace mora::model;

TEST(HandlerRegistry, DefaultsToUnbound) {
    rt::HandlerRegistry r;
    EXPECT_FALSE(r.has_impl(HandlerId::RefAddKeyword));
}

TEST(HandlerRegistry, BindAndInvokeEffect) {
    rt::HandlerRegistry r;
    int calls = 0;
    std::vector<uint32_t> got;
    r.bind_effect(HandlerId::RefAddKeyword,
        [&](const rt::EffectArgs& a) {
            ++calls;
            got = a.args;
            return rt::EffectHandle{42};
        });

    EXPECT_TRUE(r.has_impl(HandlerId::RefAddKeyword));
    rt::EffectArgs ea{.args = {0xAAu, 0xBBu}};
    auto h = r.invoke_effect(HandlerId::RefAddKeyword, ea);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(h.id, 42u);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0], 0xAAu);
}

TEST(HandlerRegistry, BindAndInvokeRetract) {
    rt::HandlerRegistry r;
    int calls = 0;
    r.bind_retract(HandlerId::RefRemoveKeyword, [&](rt::EffectHandle) { ++calls; });
    r.invoke_retract(HandlerId::RefRemoveKeyword, rt::EffectHandle{7});
    EXPECT_EQ(calls, 1);
}
