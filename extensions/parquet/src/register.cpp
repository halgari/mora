#include "mora_parquet/register.h"
#include "mora_parquet/snapshot_sink.h"
#include "mora/ext/extension.h"

#include <memory>

namespace mora_parquet {

void register_parquet(mora::ext::ExtensionContext& ctx) {
    ctx.register_sink(std::make_unique<ParquetSnapshotSink>());
}

} // namespace mora_parquet
