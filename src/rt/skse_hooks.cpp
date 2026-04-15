#ifdef MORA_WITH_COMMONLIB
#include "mora/rt/skse_hooks.h"
#include "mora/model/relations.h"
#include "mora/dag/graph.h"
#include "mora/rt/dag_engine.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <cstdint>
#include <string_view>

namespace mora::rt {

static DagRuntime* g_dag = nullptr;

// Find the DAG source node id for a given (namespace, name) relation.
static uint32_t find_source_node(const DagRuntime& dr,
                                 std::string_view ns, std::string_view name) {
    const auto& g = dr.dag();
    for (uint32_t i = 0; i < g.node_count(); ++i) {
        const auto& n = g.node(i);
        if (n.opcode != dag::DagOpcode::EventSource
            && n.opcode != dag::DagOpcode::StateSource) continue;
        if (n.relation_id >= model::kRelationCount) continue;
        const auto& r = model::kRelations[n.relation_id];
        if (r.namespace_ == ns && r.name == name) return i;
    }
    return static_cast<uint32_t>(-1);
}

// NOTE: TESActivateEvent is a placeholder for OnLocationChange; Skyrim's
// true location-changed event is BGSLocationChangeEvent and this should be
// refined once the handler semantics are nailed down.
class LocationChangeSink : public RE::BSTEventSink<RE::TESActivateEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESActivateEvent* evt,
        RE::BSTEventSource<RE::TESActivateEvent>*) override {
        if (!g_dag || !evt) return RE::BSEventNotifyControl::kContinue;
        auto* engine = g_dag->engine();
        if (!engine) return RE::BSEventNotifyControl::kContinue;

        uint32_t node = find_source_node(*g_dag, "event", "entered_location");
        if (node == static_cast<uint32_t>(-1)) return RE::BSEventNotifyControl::kContinue;

        uint32_t actor = evt->actionRef       ? evt->actionRef->GetFormID()       : 0u;
        uint32_t tgt   = evt->objectActivated ? evt->objectActivated->GetFormID() : 0u;

        Delta d{.tuple = {actor, tgt}, .diff = +1};
        engine->inject_delta(node, std::move(d));
        engine->run_to_quiescence();
        return RE::BSEventNotifyControl::kContinue;
    }
};

// TESDeathEvent fires twice per death: once with dead=false (entering the
// dying state) and once with dead=true (fully dead). We emit the "killed"
// delta on the dead=true pass so it fires once per kill.
class ActorKilledSink : public RE::BSTEventSink<RE::TESDeathEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESDeathEvent* evt,
        RE::BSTEventSource<RE::TESDeathEvent>*) override {
        if (!g_dag || !evt || !evt->dead) return RE::BSEventNotifyControl::kContinue;
        auto* engine = g_dag->engine();
        if (!engine) return RE::BSEventNotifyControl::kContinue;

        uint32_t node = find_source_node(*g_dag, "event", "killed");
        if (node == static_cast<uint32_t>(-1)) return RE::BSEventNotifyControl::kContinue;

        uint32_t victim = evt->actorDying.get()  ? evt->actorDying->GetFormID()  : 0u;
        uint32_t killer = evt->actorKiller.get() ? evt->actorKiller->GetFormID() : 0u;

        Delta d{.tuple = {victim, killer}, .diff = +1};
        engine->inject_delta(node, std::move(d));
        engine->run_to_quiescence();
        return RE::BSEventNotifyControl::kContinue;
    }
};

void register_skse_hooks(DagRuntime& dr,
                         const std::unordered_set<std::string>& needed_hooks) {
    g_dag = &dr;
    auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
    if (!holder) return;

    if (needed_hooks.count("OnLocationChange")) {
        static LocationChangeSink s_location_sink;
        holder->AddEventSink(&s_location_sink);
    }
    if (needed_hooks.count("OnActorKilled")) {
        static ActorKilledSink s_killed_sink;
        holder->AddEventSink(&s_killed_sink);
    }
}

} // namespace mora::rt
#else
#include "mora/rt/skse_hooks.h"
namespace mora::rt {
void register_skse_hooks(DagRuntime&, const std::unordered_set<std::string>&) { /* no-op on Linux */ }
} // namespace mora::rt
#endif
