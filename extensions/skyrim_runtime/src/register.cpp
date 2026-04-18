#include "mora_skyrim_runtime/register.h"
#include "mora/ext/extension.h"

namespace mora_skyrim_runtime {
void register_skyrim_runtime(mora::ext::ExtensionContext& /*ctx*/) {
    // No registrations yet. When `on`/`maintain` land, this hooks a
    // live-game DataSource and a game-apply Sink into the context.
}
} // namespace mora_skyrim_runtime
