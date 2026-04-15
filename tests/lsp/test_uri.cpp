#include <gtest/gtest.h>
#include "mora/lsp/uri.h"

using mora::lsp::uri_from_path;
using mora::lsp::path_from_uri;

TEST(LspUri, RoundTripPosixAbsolute) {
    EXPECT_EQ(uri_from_path("/home/user/foo.mora"),
              "file:///home/user/foo.mora");
    EXPECT_EQ(path_from_uri("file:///home/user/foo.mora"),
              "/home/user/foo.mora");
}

TEST(LspUri, PercentDecodesSpaces) {
    EXPECT_EQ(path_from_uri("file:///home/u%20s/foo%20bar.mora"),
              "/home/u s/foo bar.mora");
}

TEST(LspUri, PercentEncodesSpaces) {
    EXPECT_EQ(uri_from_path("/home/u s/foo bar.mora"),
              "file:///home/u%20s/foo%20bar.mora");
}

TEST(LspUri, WindowsDriveLetter) {
    // Windows file:/// URIs include the drive letter as the first
    // path segment. We don't run on Windows in tests but we accept
    // and produce the canonical form.
    EXPECT_EQ(path_from_uri("file:///C:/Users/u/foo.mora"),
              "C:/Users/u/foo.mora");
    EXPECT_EQ(uri_from_path("C:/Users/u/foo.mora"),
              "file:///C:/Users/u/foo.mora");
}

TEST(LspUri, RejectsNonFileScheme) {
    EXPECT_EQ(path_from_uri("https://example.com/foo"), "");
    EXPECT_EQ(path_from_uri("untitled:Untitled-1"), "");
}

TEST(LspUri, EmptyInput) {
    EXPECT_EQ(uri_from_path(""), "");
    EXPECT_EQ(path_from_uri(""), "");
}
