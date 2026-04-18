#include "mora_skyrim_compile/register.h"
#include "mora_skyrim_compile/esp_data_source.h"
#include "mora/ext/extension.h"

#include <memory>

namespace mora_skyrim_compile {

void register_skyrim(mora::ext::ExtensionContext& ctx) {
    ctx.register_data_source(std::make_unique<SkyrimEspDataSource>());
}

} // namespace mora_skyrim_compile
