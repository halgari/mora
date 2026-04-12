#include <gtest/gtest.h>
#include "mora/data/value.h"

using mora::Value;

TEST(ValueTest, ListCreation) {
    auto list = Value::make_list({
        Value::make_formid(0x100),
        Value::make_formid(0x200),
        Value::make_formid(0x300),
    });
    EXPECT_EQ(list.kind(), Value::Kind::List);
    const auto& items = list.as_list();
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].as_formid(), 0x100u);
    EXPECT_EQ(items[1].as_formid(), 0x200u);
    EXPECT_EQ(items[2].as_formid(), 0x300u);
}

TEST(ValueTest, ListContains) {
    auto list = Value::make_list({
        Value::make_formid(0xAAA),
        Value::make_formid(0xBBB),
    });
    EXPECT_TRUE(list.list_contains(Value::make_formid(0xAAA)));
    EXPECT_TRUE(list.list_contains(Value::make_formid(0xBBB)));
    EXPECT_FALSE(list.list_contains(Value::make_formid(0xCCC)));
}

TEST(ValueTest, ListEquality) {
    auto a = Value::make_list({Value::make_formid(0x1), Value::make_formid(0x2)});
    auto b = Value::make_list({Value::make_formid(0x1), Value::make_formid(0x2)});
    auto c = Value::make_list({Value::make_formid(0x1), Value::make_formid(0x9)});
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(ValueTest, ListHash) {
    auto a = Value::make_list({Value::make_formid(0x1), Value::make_formid(0x2)});
    auto b = Value::make_list({Value::make_formid(0x1), Value::make_formid(0x2)});
    EXPECT_EQ(a.hash(), b.hash());
}

TEST(ValueTest, ListMatchesVar) {
    auto list = Value::make_list({Value::make_formid(0x42)});
    auto var  = Value::make_var();
    EXPECT_TRUE(list.matches(var));
    EXPECT_TRUE(var.matches(list));
}

TEST(ValueTest, EmptyList) {
    auto list = Value::make_list({});
    EXPECT_EQ(list.kind(), Value::Kind::List);
    EXPECT_EQ(list.as_list().size(), 0u);
    EXPECT_FALSE(list.list_contains(Value::make_formid(0x1)));
}
