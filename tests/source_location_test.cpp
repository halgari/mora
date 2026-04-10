#include <gtest/gtest.h>
#include "mora/core/source_location.h"

TEST(SourceLocationTest, SpanContainsPosition) {
    mora::SourceSpan span{"test.mora", 5, 10, 5, 25};
    EXPECT_EQ(span.file, "test.mora");
    EXPECT_EQ(span.start_line, 5u);
    EXPECT_EQ(span.start_col, 10u);
    EXPECT_EQ(span.end_line, 5u);
    EXPECT_EQ(span.end_col, 25u);
}

TEST(SourceLocationTest, MergeSpans) {
    mora::SourceSpan a{"test.mora", 5, 1, 5, 10};
    mora::SourceSpan b{"test.mora", 7, 1, 7, 20};
    auto merged = mora::merge_spans(a, b);
    EXPECT_EQ(merged.start_line, 5u);
    EXPECT_EQ(merged.start_col, 1u);
    EXPECT_EQ(merged.end_line, 7u);
    EXPECT_EQ(merged.end_col, 20u);
}
