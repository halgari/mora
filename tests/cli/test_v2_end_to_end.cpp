#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/wait.h>

// Smoke test: the `mora` CLI binary must at least run and respond to --help
// without crashing.
//
// We invoke the built binary directly (rather than via `xmake run mora`) to
// avoid xmake lock contention when this test itself is run under `xmake test`.
TEST(V2EndToEnd, CliBinaryRunsHelp) {
    // Search upward from cwd for the build output tree, so we find the
    // `mora` binary regardless of whether we were launched from the
    // project root or from a build directory.
    std::filesystem::path bin;
    const char* modes[] = {"release", "releasedbg", "debug"};
    for (std::filesystem::path p = std::filesystem::current_path();
         !p.empty(); p = p.parent_path()) {
        for (const char* mode : modes) {
            auto candidate = p / "build" / "linux" / "x86_64" / mode / "mora";
            if (std::filesystem::exists(candidate)) {
                bin = candidate;
                break;
            }
        }
        if (!bin.empty()) break;
        if (p == p.parent_path()) break;
    }
    ASSERT_FALSE(bin.empty())
        << "mora binary not found under build/linux/x86_64/release/";

    std::string cmd = bin.string() + " --help > /dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    ASSERT_NE(rc, -1) << "failed to invoke shell";
    ASSERT_TRUE(WIFEXITED(rc))
        << "mora --help did not exit normally (possibly killed by signal)";
    int exit_code = WEXITSTATUS(rc);
    EXPECT_NE(exit_code, 127) << "mora binary could not be executed";
}
