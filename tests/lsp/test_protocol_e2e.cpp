#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace {

// Find the mora binary in build/linux/x86_64/{release,releasedbg,debug}/.
// Mirrors the pattern from tests/cli/test_v2_end_to_end.cpp.
std::string find_mora() {
    namespace fs = std::filesystem;
    const char* modes[] = {"release", "releasedbg", "debug"};
    for (fs::path p = fs::current_path(); !p.empty(); p = p.parent_path()) {
        for (const char* m : modes) {
            auto cand = p / "build" / "linux" / "x86_64" / m / "mora";
            if (fs::exists(cand)) return cand.string();
        }
        if (p == p.parent_path()) break;
    }
    return "";
}

std::string frame(std::string_view body) {
    std::string out = "Content-Length: ";
    out += std::to_string(body.size());
    out += "\r\n\r\n";
    out += body;
    return out;
}

// Run `mora lsp`, send the concatenated frames on stdin, return stdout.
std::string run_lsp(const std::string& bin, std::string_view stdin_data) {
    std::string pid = std::to_string(::getpid());
    std::string in_path  = "/tmp/mora-lsp-test-" + pid + ".in";
    std::string out_path = "/tmp/mora-lsp-test-" + pid + ".out";
    {
        std::ofstream f(in_path, std::ios::binary);
        f.write(stdin_data.data(), static_cast<std::streamsize>(stdin_data.size()));
    }
    std::string cmd = bin + " lsp < " + in_path + " > " + out_path + " 2>/dev/null";
    int rc = std::system(cmd.c_str());
    (void)rc;
    std::ifstream f(out_path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::remove(in_path.c_str());
    std::remove(out_path.c_str());
    return ss.str();
}

// Parse one framed message starting at `pos` in `out`.
// Returns the JSON body string and advances `pos` past it.
// Returns empty string on failure.
std::string skip_one_message(const std::string& out, size_t& pos) {
    size_t hdr_end = out.find("\r\n\r\n", pos);
    if (hdr_end == std::string::npos) return "";
    std::string hdrs = out.substr(pos, hdr_end - pos);
    size_t cl = hdrs.find("Content-Length:");
    if (cl == std::string::npos) return "";
    size_t length = std::stoul(hdrs.substr(cl + 15));
    std::string body = out.substr(hdr_end + 4, length);
    pos = hdr_end + 4 + length;
    return body;
}

} // namespace

TEST(LspE2e, InitializeRoundTrip) {
    std::string bin = find_mora();
    ASSERT_FALSE(bin.empty()) << "mora binary not found";

    auto in = frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})")
            + frame(R"({"jsonrpc":"2.0","method":"initialized","params":{}})")
            + frame(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})")
            + frame(R"({"jsonrpc":"2.0","method":"exit"})");

    std::string out = run_lsp(bin, in);

    size_t p = 0;
    std::string init = skip_one_message(out, p);
    ASSERT_FALSE(init.empty()) << "no first message in output";
    auto j = nlohmann::json::parse(init);
    EXPECT_EQ(j["id"], 1);
    EXPECT_TRUE(j["result"]["capabilities"].contains("textDocumentSync"));

    std::string sd = skip_one_message(out, p);
    ASSERT_FALSE(sd.empty()) << "no shutdown response in output";
    auto j2 = nlohmann::json::parse(sd);
    EXPECT_EQ(j2["id"], 2);
}

TEST(LspE2e, DidOpenEmitsPublishDiagnostics) {
    std::string bin = find_mora();
    ASSERT_FALSE(bin.empty());

    nlohmann::json open_params = {
        {"textDocument", {
            {"uri", "file:///tmp/test.mora"},
            {"languageId", "mora"},
            {"version", 1},
            {"text", "namespace t\nbandit(:\n"},  // syntax error
        }},
    };
    nlohmann::json open_msg = {
        {"jsonrpc","2.0"},
        {"method","textDocument/didOpen"},
        {"params", open_params},
    };

    auto in = frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})")
            + frame(R"({"jsonrpc":"2.0","method":"initialized","params":{}})")
            + frame(open_msg.dump())
            + frame(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})")
            + frame(R"({"jsonrpc":"2.0","method":"exit"})");

    std::string out = run_lsp(bin, in);

    // We should see a `publishDiagnostics` notification somewhere.
    EXPECT_NE(out.find("publishDiagnostics"), std::string::npos)
        << "publishDiagnostics not found in LSP output";

    // The diagnostics array should contain at least one entry with a "range".
    auto diag_pos = out.find("\"diagnostics\":");
    ASSERT_NE(diag_pos, std::string::npos) << "no diagnostics key found";
    // Look for a non-empty array (i.e. contains "range" key)
    EXPECT_NE(out.find("\"range\""), std::string::npos)
        << "expected at least one diagnostic with a range";
}
