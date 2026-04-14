#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
#include "mora/diag/diagnostic.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace mora;

static std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Fixtures that are intentionally broken (used as negative test inputs
// elsewhere). They must parse, but they are expected to produce
// name-resolution / type-checking errors.
static bool is_negative_fixture(const std::string& stem) {
    return stem == "errors";
}

TEST(V2Fixtures, AllTestDataMoraFilesTypecheck) {
    // Locate test_data relative to project root. `xmake run <test>` uses the
    // project root as cwd; fall back to a parent lookup just in case.
    // Search upward from cwd for a "test_data" directory. `xmake run`
    // executes tests from the build output directory, so we may need to
    // walk up several levels to find the project root.
    std::filesystem::path dir;
    {
        std::filesystem::path cwd = std::filesystem::current_path();
        for (std::filesystem::path p = cwd; !p.empty(); p = p.parent_path()) {
            auto candidate = p / "test_data";
            if (std::filesystem::exists(candidate) &&
                std::filesystem::is_directory(candidate)) {
                dir = candidate;
                break;
            }
            if (p == p.parent_path()) break;
        }
    }
    ASSERT_FALSE(dir.empty())
        << "test_data/ directory not found searching upward from cwd: "
        << std::filesystem::current_path();
    ASSERT_TRUE(std::filesystem::exists(dir))
        << "test_data/ directory not found. cwd: "
        << std::filesystem::current_path();

    size_t checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".mora") continue;
        SCOPED_TRACE(entry.path().string());

        std::string src = read_file(entry.path());
        StringPool pool;
        DiagBag diags;
        Lexer lex(src, entry.path().string(), pool, diags);
        Parser p(lex, pool, diags);
        Module mod = p.parse_module();

        NameResolver nr(pool, diags);
        nr.resolve(mod);
        TypeChecker tc(pool, diags, nr);
        tc.check(mod);

        const std::string stem = entry.path().stem().string();
        if (is_negative_fixture(stem)) {
            EXPECT_GT(diags.error_count(), 0u)
                << "negative fixture '" << stem
                << "' expected to produce errors but produced none";
        } else {
            // Collect error messages for diagnostic output.
            std::string msgs;
            for (const auto& d : diags.all()) {
                if (d.level == DiagLevel::Error) {
                    msgs += d.message;
                    msgs += '\n';
                }
            }
            EXPECT_EQ(diags.error_count(), 0u)
                << "errors in " << entry.path().string() << ":\n" << msgs;
        }
        ++checked;
    }
    EXPECT_GT(checked, 0u) << "no .mora fixtures found under " << dir;
}
