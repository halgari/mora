#include <gtest/gtest.h>
#include <sstream>
#include "mora/lsp/framing.h"

using mora::lsp::read_message;
using mora::lsp::write_message;
using mora::lsp::ReadResult;

TEST(LspFraming, ReadValidMessage) {
    std::stringstream s;
    s << "Content-Length: 17\r\n\r\n{\"jsonrpc\":\"2.0\"}";
    std::string body;
    ReadResult r = read_message(s, body);
    EXPECT_EQ(r, ReadResult::Ok);
    EXPECT_EQ(body, R"({"jsonrpc":"2.0"})");
}

TEST(LspFraming, ReadEofBeforeHeader) {
    std::stringstream s;
    std::string body;
    EXPECT_EQ(read_message(s, body), ReadResult::Eof);
}

TEST(LspFraming, ReadEofMidHeader) {
    std::stringstream s;
    s << "Content-Length: 17\r\n";  // missing terminating \r\n
    std::string body;
    EXPECT_EQ(read_message(s, body), ReadResult::Eof);
}

TEST(LspFraming, ReadEofMidBody) {
    std::stringstream s;
    s << "Content-Length: 100\r\n\r\n{\"jsonrpc\":\"2.0\"}";  // body too short
    std::string body;
    EXPECT_EQ(read_message(s, body), ReadResult::Eof);
}

TEST(LspFraming, ReadMissingContentLength) {
    std::stringstream s;
    s << "Some-Other-Header: 17\r\n\r\n{}";
    std::string body;
    EXPECT_EQ(read_message(s, body), ReadResult::ProtocolError);
}

TEST(LspFraming, ReadIgnoresContentType) {
    std::stringstream s;
    s << "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n"
      << "Content-Length: 2\r\n\r\n{}";
    std::string body;
    EXPECT_EQ(read_message(s, body), ReadResult::Ok);
    EXPECT_EQ(body, "{}");
}

TEST(LspFraming, WriteFormatsHeaderAndBody) {
    std::stringstream s;
    write_message(s, R"({"jsonrpc":"2.0"})");
    EXPECT_EQ(s.str(), "Content-Length: 17\r\n\r\n{\"jsonrpc\":\"2.0\"}");
}

TEST(LspFraming, WriteHandlesUnicode) {
    std::stringstream s;
    std::string body = "{\"text\":\"héllo\"}";  // 'é' is 2 UTF-8 bytes
    write_message(s, body);
    std::string expected = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    EXPECT_EQ(s.str(), expected);
}
