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

TEST(ExtensionContext, DuplicateProvidesEmitsDiagnostic_GivenEmptyBag) {
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

class StubSink : public mora::ext::Sink {
public:
    StubSink(std::string_view name, std::size_t* invocation_counter)
        : name_(name), counter_(invocation_counter) {}

    std::string_view name() const override { return name_; }

    void emit(mora::ext::EmitCtx&, const mora::FactDB&) override {
        ++*counter_;
    }

private:
    std::string  name_;
    std::size_t* counter_;
};

TEST(ExtensionContext, SinkRegistrationPreservesOrder) {
    mora::ext::ExtensionContext ec;
    std::size_t counter = 0;
    ec.register_sink(std::make_unique<StubSink>("a", &counter));
    ec.register_sink(std::make_unique<StubSink>("b", &counter));

    auto sinks = ec.sinks();
    ASSERT_EQ(sinks.size(), 2U);
    EXPECT_EQ(sinks[0]->name(), "a");
    EXPECT_EQ(sinks[1]->name(), "b");
}

TEST(ExtensionContext, SinksAccessorReturnsRegisteredSinks) {
    mora::ext::ExtensionContext ec;
    EXPECT_TRUE(ec.sinks().empty());

    std::size_t counter = 0;
    ec.register_sink(std::make_unique<StubSink>("parquet.snapshot", &counter));

    auto sinks = ec.sinks();
    ASSERT_EQ(sinks.size(), 1U);
    EXPECT_EQ(sinks[0]->name(), "parquet.snapshot");
}

TEST(ExtensionContext, LoadRequiredToleratesPreexistingErrorsInDiagBag) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    // Simulate an earlier pipeline stage having already emitted an
    // error (e.g. sema reported a type mismatch). load_required must
    // still invoke matching sources without treating that pre-existing
    // error as a collision.
    diags.error("preexisting", "earlier-stage error",
                mora::SourceSpan{}, "");
    ASSERT_TRUE(diags.has_errors());

    mora::ext::ExtensionContext ec;
    std::size_t counter_a = 0;
    ec.register_data_source(std::make_unique<StubSource>("a",
        std::vector<std::string>{"rel.one"}, &counter_a));

    auto id_one = pool.intern("rel.one").index;
    mora::ext::LoadCtx ctx{pool, diags, {}, {}, {id_one}};
    auto invoked = ec.load_required(ctx, db);

    EXPECT_EQ(invoked, 1U);
    EXPECT_EQ(counter_a, 1U);
}

TEST(ExtensionContext, RegisterRelationPreservesInsertionOrder) {
    mora::ext::ExtensionContext ec;

    mora::ext::RelationSchema a{.name = "a", .columns = {{"col0", false}}};
    mora::ext::RelationSchema b{.name = "b", .columns = {{"col0", true}}};
    ec.register_relation(a);
    ec.register_relation(b);

    auto schemas = ec.schemas();
    ASSERT_EQ(schemas.size(), 2U);
    EXPECT_EQ(schemas[0].name, "a");
    EXPECT_EQ(schemas[1].name, "b");
    EXPECT_FALSE(schemas[0].columns[0].indexed);
    EXPECT_TRUE(schemas[1].columns[0].indexed);
}

TEST(ExtensionContext, FindSchemaByName) {
    mora::ext::ExtensionContext ec;
    ec.register_relation(mora::ext::RelationSchema{
        .name = "form/npc",
        .columns = {{"form_id", true}, {"race", false}},
        .is_output = false,
    });
    ec.register_relation(mora::ext::RelationSchema{
        .name = "skyrim/set",
        .columns = {{"entity", true}, {"field", false}, {"value", false}},
        .is_output = true,
    });

    const auto* npc = ec.find_schema("form/npc");
    ASSERT_NE(npc, nullptr);
    EXPECT_EQ(npc->name, "form/npc");
    EXPECT_FALSE(npc->is_output);

    const auto* setr = ec.find_schema("skyrim/set");
    ASSERT_NE(setr, nullptr);
    EXPECT_TRUE(setr->is_output);

    EXPECT_EQ(ec.find_schema("does/not/exist"), nullptr);
}

TEST(ExtensionContext, RegisterRelationOverwritesDuplicateName) {
    mora::ext::ExtensionContext ec;

    ec.register_relation(mora::ext::RelationSchema{
        .name = "form/npc",
        .columns = {{"col0", false}},
    });
    ec.register_relation(mora::ext::RelationSchema{
        .name = "form/npc",
        .columns = {{"col0", true}, {"col1", false}},
    });

    EXPECT_EQ(ec.schemas().size(), 1U);
    const auto* s = ec.find_schema("form/npc");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->columns.size(), 2U);
    EXPECT_TRUE(s->columns[0].indexed);
}

} // namespace
