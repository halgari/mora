#pragma once

#include "mora/ext/extension.h"

namespace mora_synthetic {

// Foundation stub entry point for the synthetic demo extension. Will
// register one nominal type, one relation, and one sink — purely to
// keep the core honest about being domain-free. Called from main()
// after ExtensionContext construction.
void register_synthetic(mora::ext::ExtensionContext& ctx);

} // namespace mora_synthetic
