#pragma once
#include <iosfwd>

namespace mora {

// Generate markdown reference documentation from the form model and
// write it to `out`. Used by: `mora docs`.
void generate_docs(std::ostream& out);

} // namespace mora
