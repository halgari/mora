#pragma once
#include "mora/core/string_pool.h"
#include "mora/esp/load_order.h"
#include "mora/esp/plugin_index.h"
#include <vector>

namespace mora {

class FactDB;

// Populate the `plugin_*` relations with one row per loaded plugin.
// Schemas are expected to be registered in `SchemaRegistry` already;
// this function just pushes tuples. Plugin names are lowercased (to
// match plugins.txt convention and `requires mod("...")` lookup).
//
// Relations populated:
//   plugin_exists(P)
//   plugin_load_index(P, Idx)       — 0-0xFD for regular, 0-0xFFF for light
//   plugin_is_master(P)             — TES4 ESM flag
//   plugin_is_light(P)              — TES4 ESL flag (covers ESL and ESPFESL)
//   plugin_master_of(Child, Parent) — Child's TES4 MAST list
//   plugin_version(P, V: Float)     — TES4 HEDR version
//   plugin_extension(P, E: String)  — `.esm` / `.esp` / `.esl`
void populate_plugin_facts(FactDB& db, StringPool& pool,
                            const LoadOrder& lo,
                            const std::vector<PluginInfo>& infos,
                            const RuntimeIndexMap& rtmap);

} // namespace mora
