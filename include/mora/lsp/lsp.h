#pragma once

namespace mora::lsp {

// Run the LSP server over stdio. Reads JSON-RPC frames from std::cin and
// writes responses/notifications to std::cout. Logs (when enabled) go to a
// file passed via --log. Returns 0 on clean shutdown, non-zero on protocol
// or I/O error.
//
// argc/argv are the args AFTER `mora lsp` (so `mora lsp --log foo.txt`
// passes argc=2, argv={"--log", "foo.txt"}).
int run(int argc, char** argv);

} // namespace mora::lsp
