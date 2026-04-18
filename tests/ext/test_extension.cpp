#include "mora/ext/extension.h"
#include "mora/eval/fact_db.h"

#include <gtest/gtest.h>

namespace {

class StubSource : public mora::ext::DataSource {
public:
    StubSource(std::string_view name,
               std::vector<uint32_t> provides,
               std::size_t* invocation_counter)
        : name_(name), provides_(std::move(provides)),
          counter_(invocation_counter) {}

    std::string_view name() const override { return name_; }

    std::span<const uint32_t> provides() const override { return provides_; }

    void load(mora::ext::LoadCtx&, mora::FactDB&) override {
        ++*counter_;
    }

private:
    std::string name_;
    std::vector<uint32_t> provides_;
    std::size_t* counter_;
};

TEST(ExtensionContext, EmptyContextInvokesNothing) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);
    mora::ext::LoadCtx ctx{pool, diags, {}, {}, {}};

    mora::ext::ExtensionContext ec;
    auto invoked = ec.load_required(ctx, db);

    EXPECT_EQ(invoked, 0U);
    EXPECT_TRUE(ec.data_sources().empty());
}

TEST(ExtensionContext, RegisterPreservesInsertionOrder) {
    mora::ext::ExtensionContext ec;
    std::size_t counter = 0;
    ec.register_data_source(std::make_unique<StubSource>("a",
        std::vector<uint32_t>{1}, &counter));
    ec.register_data_source(std::make_unique<StubSource>("b",
        std::vector<uint32_t>{2}, &counter));

    auto sources = ec.data_sources();
    ASSERT_EQ(sources.size(), 2U);
    EXPECT_EQ(sources[0]->name(), "a");
    EXPECT_EQ(sources[1]->name(), "b");
}

TEST(ExtensionContext, LoadRequiredOnlyInvokesMatchingSources) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    mora::ext::ExtensionContext ec;
    std::size_t counter_a = 0;
    std::size_t counter_b = 0;
    std::size_t counter_c = 0;
    ec.register_data_source(std::make_unique<StubSource>("a",
        std::vector<uint32_t>{10, 20}, &counter_a));
    ec.register_data_source(std::make_unique<StubSource>("b",
        std::vector<uint32_t>{30},     &counter_b));
    ec.register_data_source(std::make_unique<StubSource>("c",
        std::vector<uint32_t>{40, 50}, &counter_c));

    // Ask for relations 20 (matches 'a') and 40 (matches 'c'); no ask
    // for anything 'b' provides.
    mora::ext::LoadCtx ctx{pool, diags, {}, {}, /*needed*/ {20U, 40U}};
    auto invoked = ec.load_required(ctx, db);

    EXPECT_EQ(invoked, 2U);
    EXPECT_EQ(counter_a, 1U);
    EXPECT_EQ(counter_b, 0U);
    EXPECT_EQ(counter_c, 1U);
}

TEST(ExtensionContext, DuplicateProvidesEmitsDiagnostic) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    mora::ext::ExtensionContext ec;
    std::size_t counter_a = 0;
    std::size_t counter_b = 0;
    // Both sources claim relation 42. When 42 is in needed_relations,
    // load_required must emit a diagnostic and refuse to invoke either
    // source.
    ec.register_data_source(std::make_unique<StubSource>("a",
        std::vector<uint32_t>{42}, &counter_a));
    ec.register_data_source(std::make_unique<StubSource>("b",
        std::vector<uint32_t>{42}, &counter_b));

    mora::ext::LoadCtx ctx{pool, diags, {}, {}, /*needed*/ {42U}};
    auto invoked = ec.load_required(ctx, db);

    EXPECT_EQ(invoked, 0U);
    EXPECT_EQ(counter_a, 0U);
    EXPECT_EQ(counter_b, 0U);
    EXPECT_TRUE(diags.has_errors());
    ASSERT_FALSE(diags.all().empty());
    EXPECT_EQ(diags.all().front().code, "data-source-conflict");
}

} // namespace
