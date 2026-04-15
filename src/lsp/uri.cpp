#include "mora/lsp/uri.h"

#include <cstdio>

namespace mora::lsp {

namespace {

bool is_unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~' ||
           c == '/' || c == ':';
}

void percent_encode(std::string& out, unsigned char c) {
    static const char hex[] = "0123456789ABCDEF";
    out.push_back('%');
    out.push_back(hex[c >> 4]);
    out.push_back(hex[c & 0x0F]);
}

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

} // namespace

std::string uri_from_path(std::string_view path) {
    if (path.empty()) return "";
    std::string out = "file://";
    if (path.front() != '/') out.push_back('/'); // Windows drive letter case
    for (unsigned char c : path) {
        if (is_unreserved(c)) out.push_back(static_cast<char>(c));
        else                  percent_encode(out, c);
    }
    return out;
}

std::string path_from_uri(std::string_view uri) {
    constexpr std::string_view kFile = "file://";
    if (uri.empty() || uri.size() < kFile.size() || uri.substr(0, kFile.size()) != kFile) {
        return "";
    }
    std::string_view body = uri.substr(kFile.size());
    // Drop the leading slash on Windows-style ("file:///C:/...") so the
    // result starts with "C:/". POSIX paths keep their leading slash.
    if (body.size() >= 3 && body[0] == '/' &&
        ((body[1] >= 'A' && body[1] <= 'Z') || (body[1] >= 'a' && body[1] <= 'z')) &&
        body[2] == ':') {
        body.remove_prefix(1);
    }
    std::string out;
    out.reserve(body.size());
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (c == '%' && i + 2 < body.size()) {
            int hi = hex_val(body[i + 1]);
            int lo = hex_val(body[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

} // namespace mora::lsp
