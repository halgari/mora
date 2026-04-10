#include <gtest/gtest.h>
#include "mora/emit/rt_writer.h"
#include "mora/eval/phase_classifier.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include <sstream>

class RtWriterTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::Module parse(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        auto mod = parser.parse_module();
        mora::NameResolver resolver(pool, diags);
        resolver.resolve(mod);
        return mod;
    }
};

TEST_F(RtWriterTest, EmptyOutput) {
    std::ostringstream out;
    mora::RtWriter writer(pool);
    writer.write(out, {});
    auto data = out.str();
    EXPECT_GE(data.size(), 4u);
    EXPECT_EQ(data.substr(0, 4), "MORT");
}

TEST_F(RtWriterTest, WriteDynamicRules) {
    auto mod = parse(
        "dynamic_rule(NPC):\n"
        "    npc(NPC)\n"
        "    current_location(NPC, Loc)\n"
        "    => add_item(NPC, :Goods)\n"
    );
    mora::PhaseClassifier classifier(pool);
    auto classifications = classifier.classify_module(mod);
    ASSERT_EQ(classifications[0].phase, mora::Phase::Dynamic);

    std::vector<const mora::Rule*> dynamic_rules;
    for (size_t i = 0; i < mod.rules.size(); i++) {
        if (classifications[i].phase == mora::Phase::Dynamic)
            dynamic_rules.push_back(&mod.rules[i]);
    }

    std::ostringstream out;
    mora::RtWriter writer(pool);
    writer.write(out, dynamic_rules);
    EXPECT_GT(out.str().size(), 8u);
}

TEST_F(RtWriterTest, MagicAndVersion) {
    std::ostringstream out;
    mora::RtWriter writer(pool);
    writer.write(out, {});
    auto data = out.str();
    EXPECT_EQ(data[0], 'M');
    EXPECT_EQ(data[1], 'O');
    EXPECT_EQ(data[2], 'R');
    EXPECT_EQ(data[3], 'T');
}
