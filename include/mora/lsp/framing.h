#pragma once
#include <iosfwd>
#include <string>

namespace mora::lsp {

enum class ReadResult { Ok, Eof, ProtocolError };

// Read one Content-Length-framed JSON-RPC message from `in`. The body
// (no headers, no trailing newline) is written to `body_out`. Returns:
//   Ok            — body contains the message
//   Eof           — clean end of input (no partial message read)
//   ProtocolError — headers were malformed (no Content-Length, etc.)
ReadResult read_message(std::istream& in, std::string& body_out);

// Write `body` to `out` framed with a Content-Length header.
// Flushes `out` so the message hits the pipe immediately.
void write_message(std::ostream& out, std::string_view body);

} // namespace mora::lsp
