#include "mora/lsp/framing.h"

#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <cstdlib>

namespace mora::lsp {

namespace {

// Read one CRLF-terminated header line. Returns false on EOF before any
// bytes were read. The trailing \r\n is consumed but not included.
bool read_header_line(std::istream& in, std::string& line) {
    line.clear();
    char c{};
    while (in.get(c)) {
        if (c == '\r') {
            char next{};
            if (in.get(next) && next == '\n') return true;
            // \r without \n — malformed, but treat as end of line.
            return true;
        }
        line.push_back(c);
    }
    return !line.empty();
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        char a = s[i], b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

} // namespace

ReadResult read_message(std::istream& in, std::string& body_out) {
    body_out.clear();
    size_t content_length = 0;
    bool have_length = false;

    while (true) {
        std::string line;
        if (!read_header_line(in, line)) {
            // Both clean EOF and mid-header truncation surface as Eof
            // — see LspFraming.ReadEofMidHeader; the caller treats both
            // as "stop reading."
            return ReadResult::Eof;
        }
        if (line.empty()) break; // end of headers
        if (starts_with_ci(line, "Content-Length:")) {
            std::string_view v(line);
            v.remove_prefix(std::string_view("Content-Length:").size());
            // Skip leading whitespace.
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
                v.remove_prefix(1);
            }
            std::string const num(v);
            char* end = nullptr;
            unsigned long long const n = std::strtoull(num.c_str(), &end, 10);
            if (end == num.c_str()) return ReadResult::ProtocolError;
            content_length = static_cast<size_t>(n);
            have_length = true;
        }
        // Other headers (Content-Type, etc.) are ignored.
    }

    if (!have_length) return ReadResult::ProtocolError;

    body_out.resize(content_length);
    in.read(body_out.data(), static_cast<std::streamsize>(content_length));
    if (static_cast<size_t>(in.gcount()) != content_length) {
        return ReadResult::Eof;
    }
    return ReadResult::Ok;
}

void write_message(std::ostream& out, std::string_view body) {
    out << "Content-Length: " << body.size() << "\r\n\r\n";
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    out.flush();
}

} // namespace mora::lsp
