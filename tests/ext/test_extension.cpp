#include "mora/ext/extension.h"
#include "mora/eval/fact_db.h"

#include <gtest/gtest.h>

namespace {

class StubSource : public mora::ext::DataSource {
public:
    StubSource(std::string_view name,
               std::vector<std::string> provides,
               std::size_t* invocation_counter)
        : name_(name), provides_(std::move(provides)),
          counter_(invocation_counter) {}

    std::string_view name() const override { return name_; }

    std::span<const std::string> provides() const override { return provides_; }

    void load(mora::ext::LoadCtx&, mora::FactDB&) override {
        ++*counter_;
    }

private:
    std::string name_;
    std::vector<std::string> provides_;
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
        std::vector<std::string>{"rel.one"}, &counter));
    ec.register_data_source(std::make_unique<StubSource>("b",
        std::vector<std::string>{"rel.two"}, &counter));

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
        std::vector<std::string>{"rel.alpha", "rel.beta"}, &counter_a));
    ec.register_data_source(std::make_unique<StubSource>("b",
        std::vector<std::string>{"rel.gamma"},             &counter_b));
    ec.register_data_source(std::make_unique<StubSource>("c",
        std::vector<std::string>{"rel.delta", "rel.epsilon"}, &counter_c));

    // Ask for rel.beta (matches 'a') and rel.delta (matches 'c'); no
    // ask for anything 'b' provides.
    auto id_beta  = pool.intern("rel.beta").index;
    auto id_delta = pool.intern("rel.delta").index;
    mora::ext::LoadCtx ctx{pool, diags, {}, {}, {id_beta, id_delta}};
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
    // Both sources claim the same relation. When it's in
    // needed_relations, load_required must emit a diagnostic and refuse
    // to invoke either source.
    ec.register_data_source(std::make_unique<StubSource>("a",
        std::vector<std::string>{"rel.collide"}, &counter_a));
    ec.register_data_source(std::make_unique<StubSource>("b",
        std::vector<std::string>{"rel.collide"}, &counter_b));

    auto id_collide = pool.intern("rel.collide").index;
    mora::ext::LoadCtx ctx{pool, diags, {}, {}, {id_collide}};
    auto invoked = ec.load_required(ctx, db);

    EXPECT_EQ(invoked, 0U);
    EXPECT_EQ(counter_a, 0U);
    EXPECT_EQ(counter_b, 0U);
    EXPECT_TRUE(diags.has_errors());
    ASSERT_FALSE(diags.all().empty());
    EXPECT_EQ(diags.all().front().code, "data-source-conflict");
}

} // namespace
