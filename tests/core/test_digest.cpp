#include "mora/core/digest.h"
#include <gtest/gtest.h>

using namespace mora;

TEST(Digest, DigestIsStableForSameInput) {
    EXPECT_EQ(compute_digest("hello"), compute_digest("hello"));
}

TEST(Digest, DifferentInputsDifferentDigests) {
    EXPECT_NE(compute_digest("hello"), compute_digest("world"));
}

TEST(Digest, DigestSizeIs32Bytes) {
    auto d = compute_digest("x");
    EXPECT_EQ(d.size(), 32u);
}

TEST(Digest, MatchesKnownSha256) {
    // SHA-256 of empty string:
    // e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    auto d = compute_digest("");
    EXPECT_EQ(d[0], 0xe3u);
    EXPECT_EQ(d[1], 0xb0u);
    EXPECT_EQ(d[2], 0xc4u);
    EXPECT_EQ(d[3], 0x42u);
    EXPECT_EQ(d[31], 0x55u);
}

TEST(Digest, SimpleInputMatchesKnownSha256) {
    // SHA-256("abc") =
    // ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    auto d = compute_digest("abc");
    EXPECT_EQ(d[0], 0xbau);
    EXPECT_EQ(d[1], 0x78u);
    EXPECT_EQ(d[31], 0xadu);
}
