#include "mora_skyrim_compile/register.h"
#include "mora/ext/extension.h"

#include <algorithm>
#include <vector>
#include <gtest/gtest.h>

namespace {

TEST(RegisterSkyrim, MirrorsSchemasIntoExtensionContext) {
    mora::ext::ExtensionContext ctx;
    mora_skyrim_compile::register_skyrim(ctx);

    // The default Skyrim schema set is non-trivial; exact count may
    // change as relations are added, but sanity-check it's in the
    // right order of magnitude and that a couple of canonical entries
    // are present.
    auto schemas = ctx.schemas();
    EXPECT_GT(schemas.size(), 10U)
        << "register_skyrim should bridge many relations, got "
        << schemas.size();

    // Spot-check well-known relations the Skyrim defaults always
    // register. If the set of default Skyrim relations ever narrows,
    // update this list — these are checked because they're
    // architectural landmarks.
    EXPECT_NE(ctx.find_schema("npc"), nullptr);
    EXPECT_NE(ctx.find_schema("weapon"), nullptr);
    EXPECT_NE(ctx.find_schema("plugin_exists"), nullptr);
}

TEST(RegisterSkyrim, RegistersExactlyThreeOutputRelations) {
    mora::ext::ExtensionContext ctx;
    mora_skyrim_compile::register_skyrim(ctx);

    std::vector<std::string> outputs;
    for (const auto& s : ctx.schemas()) {
        if (s.is_output) outputs.push_back(s.name);
    }
    std::sort(outputs.begin(), outputs.end());

    EXPECT_EQ(outputs,
              (std::vector<std::string>{"skyrim/add", "skyrim/remove", "skyrim/set"}));
}

} // namespace
