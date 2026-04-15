#pragma once
#include <string>
#include <string_view>

namespace mora::lsp {

// Convert a filesystem path to a file:// URI. Spaces and other reserved
// characters are percent-encoded. Returns "" for empty input.
//
// Accepts both POSIX absolute paths ("/home/u/foo") and Windows-style
// paths ("C:/Users/u/foo"). Drive letters are preserved as the first
// path segment.
std::string uri_from_path(std::string_view path);

// Convert a file:// URI back to a filesystem path. Percent-encoded
// characters are decoded. Returns "" if the URI is not a file:// URI
// (e.g. https://, untitled:) or is otherwise unparseable.
std::string path_from_uri(std::string_view uri);

} // namespace mora::lsp
