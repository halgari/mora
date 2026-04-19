#pragma once

#include "mora/ext/extension.h"

namespace mora_parquet {

// Foundation stub entry point for the parquet extension. Will register
// ParquetSnapshotSink + ParquetSnapshotSource once the extension is
// populated. Called from main() after ExtensionContext construction.
void register_parquet(mora::ext::ExtensionContext& ctx);

} // namespace mora_parquet
