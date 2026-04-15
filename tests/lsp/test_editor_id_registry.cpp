#include <gtest/gtest.h>
#include "mora/lsp/editor_id_registry.h"

using mora::lsp::EditorIdRegistry;

TEST(EditorIdRegistry, EmptyByDefault) {
    EditorIdRegistry r;
    EXPECT_FALSE(r.has_data());
    EXPECT_FALSE(r.lookup("BanditFaction").has_value());
}

TEST(EditorIdRegistry, ScanWithMissingDirIsNoOp) {
    EditorIdRegistry r;
    r.scan("/nonexistent/path/xyz");
    EXPECT_FALSE(r.has_data());
    EXPECT_FALSE(r.lookup("BanditFaction").has_value());
}

TEST(EditorIdRegistry, ScanWithEmptyPathIsNoOp) {
    EditorIdRegistry r;
    r.scan(std::filesystem::path{});
    EXPECT_FALSE(r.has_data());
}
