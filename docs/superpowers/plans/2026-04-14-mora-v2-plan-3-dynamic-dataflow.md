# Mora v2 Plan 3 — Differential Dataflow Engine + Dynamic Rules

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring dynamic rules online. Introduce the first set of `ref/*`, `player/*`, `world/*`, and `event/*` relations; compile `maintain` and `on` rules into an operator-DAG bytecode; ship the DAG in the v2 patch file; run a small differential dataflow engine at runtime that consumes SKSE event deltas, probes against mmap'd arrangements from Plan 2, and dispatches effects through a handler registry with MaintainSink auto-retraction.

**Architecture:** A minimal single-threaded delta-propagation loop. Rules become operator DAGs at compile time; operators are typed nodes serialized into a new `DagBytecode` section of the patch file. At runtime, SKSE hooks translate game events into `(tuple, diff)` pairs, feed them to the DAG's source operators, and the engine propagates deltas to quiescence. `MaintainSink` tracks per-binding effect handles so retractions auto-undo; `OnSink` discards retractions and fires effects only on `+1`. Dynamic `ref/*` reads go through a small handler registry keyed by `HandlerId`.

**Tech Stack:** C++20, xmake, gtest. No new third-party dependencies. The runtime DLL adds SKSE hook registration (Windows-side only).

**Reference spec:** `docs/superpowers/specs/2026-04-14-mora-v2-static-dynamic-datalog-design.md` (Sections 3, 4, 6)

**Preconditions:** Plans 1 and 2 are complete. `kRelations`, verb/cardinality checking, `maintain`/`on` parse+classify, v2 patch files, static arrangements, and `ArrangementView` are all in place.

---

## Scope and non-goals

**In scope:**
- New `ref/*`, `player/*`, `world/*`, `event/*` relations added to `kRelations` with matching handler/hook registrations
- Operator DAG: typed node graph (Filter, Project, HashJoin, StaticProbe, MaintainSink, OnSink)
- DAG compiler: walks `maintain`/`on` rules, produces a DAG
- DAG bytecode serialization/deserialization as a new `SectionId::DagBytecode` section
- Runtime engine: delta queue, operator dispatch, `MaintainSink` handle tracking
- SKSE hook wiring on Windows — at least `OnLocationChange`, `OnCombatStateChange`, and one inventory hook
- Event/State pair pattern: `event/entered_location` (edge) paired with `ref/current_location` (state) maintained by the engine

**Not in scope (intentionally):**
- Iterative recursion / stratified Datalog (single pass per rule for now)
- Time-windowed/timestamped dataflow (Naiad-style)
- `+=` delta arithmetic in `maintain` rules (spec defers this)
- Save/load persistence (spec flags as future work)
- Performance tuning beyond "correct and measurable"
- Real `esp_digest` enforcement (Plan 2 stubbed this; real SKSE plugin enumeration is a separate task)

---

## File structure

**New files:**
- `include/mora/model/dynamic_relations.h` — namespace for new `ref/*`, `player/*`, `world/*`, `event/*` entries (built on top of the existing `RelationEntry` shape)
- Extensions to `src/model/relations_seed.cpp` with the new entries
- `include/mora/dag/opcode.h` — `DagOpcode` enum
- `include/mora/dag/node.h` — `DagNode` struct (serialized form) and `DagGraph` (in-memory builder)
- `include/mora/dag/compile.h` / `src/dag/compile.cpp` — lower `maintain`/`on` rules to `DagGraph`
- `include/mora/dag/bytecode.h` / `src/dag/bytecode.cpp` — serialize `DagGraph` to the `DagBytecode` section; deserialize at runtime
- `include/mora/rt/delta.h` — `Delta { Tuple tuple; int8_t diff; }`
- `include/mora/rt/dag_engine.h` / `src/rt/dag_engine.cpp` — runtime delta propagation engine
- `include/mora/rt/handler_registry.h` / `src/rt/handler_registry.cpp` — dispatch table keyed by `HandlerId`
- `include/mora/rt/skse_hooks.h` / `src/rt/skse_hooks.cpp` — Windows-only hook registration
- `include/mora/rt/maintain_sink.h` / `src/rt/maintain_sink.cpp` — per-binding effect handle tracking
- Tests throughout `tests/dag/` and `tests/rt/`

**Modified files:**
- `src/model/relations_seed.cpp` — add new relations
- `src/emit/patch_table.cpp` — emit DAG bytecode section
- `src/rt/patch_walker.cpp` — call DAG engine init after loading patches
- `src/rt/plugin_entry.cpp` — register SKSE hooks
- `src/main.cpp` — call the DAG compiler over all `maintain`/`on` rules
- `xmake.lua` — new source files

---

## Phase A — New relations and handlers (Tasks 1–4)

Goal: extend `kRelations` with a minimal but meaningful set of dynamic relations so that `maintain`/`on` rules can type-check and classify correctly, and so the DAG compiler has real inputs to operate on.

### Task 1: New `HandlerId` entries and a generic handler registry

**Files:**
- Modify: `include/mora/model/handler_ids.h`
- Modify: `include/mora/model/handlers.h`
- Create: `include/mora/rt/handler_registry.h`
- Create: `src/rt/handler_registry.cpp`
- Tests: `tests/model/test_handler_ids.cpp`, `tests/rt/test_handler_registry.cpp`

- [ ] **Step 1: Write failing test for new HandlerId entries**

```cpp
// tests/model/test_handler_ids.cpp
#include "mora/model/handlers.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(HandlerIds, RefAddKeywordRegistered) {
    const HandlerEntry* e = find_handler(HandlerId::RefAddKeyword);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->id, HandlerId::RefAddKeyword);
}

TEST(HandlerIds, RefRemoveKeywordRegistered) {
    const HandlerEntry* e = find_handler(HandlerId::RefRemoveKeyword);
    ASSERT_NE(e, nullptr);
}

TEST(HandlerIds, PlayerAddGoldRegistered) {
    const HandlerEntry* e = find_handler(HandlerId::PlayerAddGold);
    ASSERT_NE(e, nullptr);
}

TEST(HandlerIds, RefReadCurrentLocationRegistered) {
    const HandlerEntry* e = find_handler(HandlerId::RefReadCurrentLocation);
    ASSERT_NE(e, nullptr);
}
```

- [ ] **Step 2: Run, verify FAIL (ids don't exist yet)**

- [ ] **Step 3: Add HandlerId entries**

Edit `include/mora/model/handler_ids.h`:

```cpp
enum class HandlerId : uint16_t {
    None = 0,
    // ref/* effect handlers
    RefAddKeyword         = 1,
    RefRemoveKeyword      = 2,
    // ref/* relation readers (MemoryRead doesn't fit — these need game-state access)
    RefReadCurrentLocation = 10,
    RefReadInCombat        = 11,
    RefReadBaseForm        = 12,
    // player/* effect handlers
    PlayerAddGold         = 20,
    PlayerSubGold         = 21,
    PlayerShowNotification = 22,
};
```

Edit `include/mora/model/handlers.h`:

```cpp
inline constexpr HandlerEntry kHandlers[] = {
    { HandlerId::None,                   "none",                    true  },
    { HandlerId::RefAddKeyword,          "ref_add_keyword",         false },
    { HandlerId::RefRemoveKeyword,       "ref_remove_keyword",      false },
    { HandlerId::RefReadCurrentLocation, "ref_read_current_location", false },
    { HandlerId::RefReadInCombat,        "ref_read_in_combat",      false },
    { HandlerId::RefReadBaseForm,        "ref_read_base_form",      false },
    { HandlerId::PlayerAddGold,          "player_add_gold",         false },
    { HandlerId::PlayerSubGold,          "player_sub_gold",         false },
    { HandlerId::PlayerShowNotification, "player_show_notification", false },
};
```

- [ ] **Step 4: Run test, verify PASS**

- [ ] **Step 5: Write failing test for handler registry**

```cpp
// tests/rt/test_handler_registry.cpp
#include "mora/rt/handler_registry.h"
#include <gtest/gtest.h>

using namespace mora;
using namespace mora::model;

TEST(HandlerRegistry, DefaultsToUnboundStubs) {
    rt::HandlerRegistry r;
    EXPECT_FALSE(r.has_impl(HandlerId::RefAddKeyword));
}

TEST(HandlerRegistry, BindAndInvokeEffect) {
    rt::HandlerRegistry r;
    int call_count = 0;
    std::vector<uint32_t> received_args;
    r.bind_effect(HandlerId::RefAddKeyword,
        [&](const rt::EffectArgs& args) {
            ++call_count;
            received_args.insert(received_args.end(),
                args.args.begin(), args.args.end());
            return rt::EffectHandle{42};
        });

    EXPECT_TRUE(r.has_impl(HandlerId::RefAddKeyword));
    rt::EffectArgs ea{.args = {0xAAAAu, 0xBBBBu}};
    auto h = r.invoke_effect(HandlerId::RefAddKeyword, ea);
    EXPECT_EQ(call_count, 1);
    EXPECT_EQ(h.id, 42u);
    ASSERT_EQ(received_args.size(), 2u);
    EXPECT_EQ(received_args[0], 0xAAAAu);
}

TEST(HandlerRegistry, BindAndInvokeRetract) {
    rt::HandlerRegistry r;
    int calls = 0;
    r.bind_retract(HandlerId::RefRemoveKeyword,
        [&](rt::EffectHandle) { ++calls; });
    r.invoke_retract(HandlerId::RefRemoveKeyword, rt::EffectHandle{7});
    EXPECT_EQ(calls, 1);
}
```

- [ ] **Step 6: Implement the registry**

```cpp
// include/mora/rt/handler_registry.h
#pragma once
#include "mora/model/handler_ids.h"
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace mora::rt {

struct EffectHandle {
    uint64_t id = 0;  // 0 = null. Meaning is handler-specific.
};

struct EffectArgs {
    std::vector<uint32_t> args;  // packed arg values; interpretation per handler
};

using EffectFn  = std::function<EffectHandle(const EffectArgs&)>;
using RetractFn = std::function<void(EffectHandle)>;
using ReadFn    = std::function<std::vector<uint32_t>(const EffectArgs&)>;

class HandlerRegistry {
public:
    void bind_effect(model::HandlerId id, EffectFn fn);
    void bind_retract(model::HandlerId id, RetractFn fn);
    void bind_read(model::HandlerId id, ReadFn fn);

    bool has_impl(model::HandlerId id) const;
    EffectHandle invoke_effect(model::HandlerId id, const EffectArgs& a);
    void invoke_retract(model::HandlerId id, EffectHandle h);
    std::vector<uint32_t> invoke_read(model::HandlerId id, const EffectArgs& a);

private:
    std::unordered_map<uint16_t, EffectFn> effects_;
    std::unordered_map<uint16_t, RetractFn> retracts_;
    std::unordered_map<uint16_t, ReadFn> reads_;
};

} // namespace mora::rt
```

```cpp
// src/rt/handler_registry.cpp
#include "mora/rt/handler_registry.h"

namespace mora::rt {

void HandlerRegistry::bind_effect(model::HandlerId id, EffectFn fn) {
    effects_[static_cast<uint16_t>(id)] = std::move(fn);
}
void HandlerRegistry::bind_retract(model::HandlerId id, RetractFn fn) {
    retracts_[static_cast<uint16_t>(id)] = std::move(fn);
}
void HandlerRegistry::bind_read(model::HandlerId id, ReadFn fn) {
    reads_[static_cast<uint16_t>(id)] = std::move(fn);
}

bool HandlerRegistry::has_impl(model::HandlerId id) const {
    auto key = static_cast<uint16_t>(id);
    return effects_.count(key) || retracts_.count(key) || reads_.count(key);
}

EffectHandle HandlerRegistry::invoke_effect(model::HandlerId id, const EffectArgs& a) {
    auto it = effects_.find(static_cast<uint16_t>(id));
    if (it == effects_.end()) return {};
    return it->second(a);
}

void HandlerRegistry::invoke_retract(model::HandlerId id, EffectHandle h) {
    auto it = retracts_.find(static_cast<uint16_t>(id));
    if (it != retracts_.end()) it->second(h);
}

std::vector<uint32_t> HandlerRegistry::invoke_read(model::HandlerId id, const EffectArgs& a) {
    auto it = reads_.find(static_cast<uint16_t>(id));
    if (it == reads_.end()) return {};
    return it->second(a);
}

} // namespace mora::rt
```

- [ ] **Step 7: Run all tests, verify PASS**

- [ ] **Step 8: Commit**

```bash
git add include/mora/model/handler_ids.h include/mora/model/handlers.h \
        include/mora/rt/handler_registry.h src/rt/handler_registry.cpp \
        tests/model/test_handler_ids.cpp tests/rt/test_handler_registry.cpp
git commit -m "rt: add HandlerId entries and runtime HandlerRegistry"
```

---

### Task 2: Seed `ref/*` relations

**Files:**
- Modify: `src/model/relations_seed.cpp`
- Test: `tests/model/test_ref_relations.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// tests/model/test_ref_relations.cpp
#include "mora/model/relations.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(RefRelations, RefKeywordIsDynamicSet) {
    const auto* r = find_relation("ref", "keyword", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->source, RelationSourceKind::Handler);
    EXPECT_EQ(r->cardinality, Cardinality::Set);
    EXPECT_EQ(r->apply_handler, HandlerId::RefAddKeyword);
    EXPECT_EQ(r->retract_handler, HandlerId::RefRemoveKeyword);
}

TEST(RefRelations, RefCurrentLocationIsFunctional) {
    const auto* r = find_relation("ref", "current_location", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->source, RelationSourceKind::Handler);
    EXPECT_EQ(r->cardinality, Cardinality::Functional);
}

TEST(RefRelations, RefBaseFormIsFunctional) {
    const auto* r = find_relation("ref", "base_form", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->cardinality, Cardinality::Functional);
}

TEST(RefRelations, RefInCombatIsFunctional) {
    const auto* r = find_relation("ref", "in_combat", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->cardinality, Cardinality::Functional);
}
```

- [ ] **Step 2: Run test, verify FAIL**

- [ ] **Step 3: Append entries to `kRelations`**

In `src/model/relations_seed.cpp`, append before the closing `}` of the kRelations array:

```cpp
    // ── ref/* dynamic relations ────────────────────────────────────────
    { .namespace_ = "ref", .name = "keyword",
      .args = {{RelValueType::FormRef, "R"}, {RelValueType::FormRef, "KW"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Set,
      .source = RelationSourceKind::Handler,
      .apply_handler   = HandlerId::RefAddKeyword,
      .retract_handler = HandlerId::RefRemoveKeyword,
      .docs = "Keywords added dynamically to a specific placed reference." },

    { .namespace_ = "ref", .name = "current_location",
      .args = {{RelValueType::FormRef, "R"}, {RelValueType::FormRef, "LOC"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Handler,
      .docs = "The location this reference is currently in. Maintained by the runtime from event/entered_location." },

    { .namespace_ = "ref", .name = "base_form",
      .args = {{RelValueType::FormRef, "R"}, {RelValueType::FormRef, "F"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Handler,
      .docs = "Bridge from live reference to its base record (static↔dynamic)." },

    { .namespace_ = "ref", .name = "in_combat",
      .args = {{RelValueType::FormRef, "R"}}, .arg_count = 1,
      .value_type = RelValueType::Int, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Handler,
      .docs = "True when this reference is currently in combat." },
```

- [ ] **Step 4: Run test, verify PASS**

- [ ] **Step 5: Commit**

```bash
git add src/model/relations_seed.cpp tests/model/test_ref_relations.cpp
git commit -m "model: seed ref/* dynamic relations"
```

---

### Task 3: Seed `player/*`, `world/*`, `event/*` relations

**Files:**
- Modify: `src/model/relations_seed.cpp`
- Test: `tests/model/test_dynamic_relations.cpp`

- [ ] **Step 1: Failing test**

```cpp
// tests/model/test_dynamic_relations.cpp
#include "mora/model/relations.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(DynamicRelations, PlayerGoldIsCountable) {
    const auto* r = find_relation("player", "gold", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->cardinality, Cardinality::Countable);
    EXPECT_EQ(r->value_type, RelValueType::Int);
    EXPECT_EQ(r->apply_handler, HandlerId::PlayerAddGold);
}

TEST(DynamicRelations, PlayerNotificationIsSet) {
    const auto* r = find_relation("player", "notification", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->cardinality, Cardinality::Set);
    EXPECT_EQ(r->apply_handler, HandlerId::PlayerShowNotification);
}

TEST(DynamicRelations, EventEnteredLocationIsEventSource) {
    const auto* r = find_relation("event", "entered_location", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->source, RelationSourceKind::Event);
}

TEST(DynamicRelations, WorldTimeOfDayExists) {
    const auto* r = find_relation("world", "time_of_day", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
}
```

- [ ] **Step 2: Append more entries**

```cpp
    // ── player/* relations ────────────────────────────────────────────
    { .namespace_ = "player", .name = "gold",
      .args = {{RelValueType::FormRef, "P"}, {RelValueType::Int, "N"}}, .arg_count = 2,
      .value_type = RelValueType::Int, .cardinality = Cardinality::Countable,
      .source = RelationSourceKind::Handler,
      .apply_handler = HandlerId::PlayerAddGold,
      // No retract for gold; it's edge-only (one-shot).
      .docs = "Player gold count (additive delta via 'add')." },

    { .namespace_ = "player", .name = "notification",
      .args = {{RelValueType::FormRef, "P"}, {RelValueType::String, "S"}}, .arg_count = 2,
      .value_type = RelValueType::String, .cardinality = Cardinality::Set,
      .source = RelationSourceKind::Handler,
      .apply_handler = HandlerId::PlayerShowNotification,
      // No retract; notification is displayed once and dismissed by the UI.
      .docs = "Queue a UI notification string." },

    // ── world/* globals ────────────────────────────────────────────
    { .namespace_ = "world", .name = "time_of_day",
      .args = {{RelValueType::Float, "T"}}, .arg_count = 1,
      .value_type = RelValueType::Float, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Hook,
      .hook = {"OnTimeOfDayChanged", HookSpec::Kind::State},
      .docs = "Current in-game hour 0..24." },

    // ── event/* edge-triggered inputs ────────────────────────────
    { .namespace_ = "event", .name = "entered_location",
      .args = {{RelValueType::FormRef, "R"}, {RelValueType::FormRef, "LOC"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Set,
      .source = RelationSourceKind::Event,
      .hook = {"OnLocationChange", HookSpec::Kind::Edge},
      .docs = "Fires when a reference enters a new location." },

    { .namespace_ = "event", .name = "combat_state_changed",
      .args = {{RelValueType::FormRef, "R"}, {RelValueType::Int, "STATE"}}, .arg_count = 2,
      .value_type = RelValueType::Int, .cardinality = Cardinality::Set,
      .source = RelationSourceKind::Event,
      .hook = {"OnCombatStateChanged", HookSpec::Kind::Edge},
      .docs = "Fires on combat state transitions; STATE: 0=none, 1=combat, 2=searching." },
```

- [ ] **Step 3: Run, verify PASS; commit**

```bash
git add src/model/relations_seed.cpp tests/model/test_dynamic_relations.cpp
git commit -m "model: seed player/*, world/*, event/* relations"
```

---

### Task 4: Activate the dormant type-checker tests (from Plan 1 Task 19)

The two `GTEST_SKIP`'d tests in `tests/sema/test_verb_legality.cpp` can now exercise real dynamic relations. Remove the skips and write proper test bodies.

**Files:**
- Modify: `tests/sema/test_verb_legality.cpp`

- [ ] **Step 1: Replace the skipped tests**

```cpp
TEST(MaintainRules, MaintainOnNonRetractableFails) {
    // player/notification has apply_handler but no retract_handler →
    // attempting to use it in a 'maintain' rule should error.
    auto errs = check_source(
        "namespace x.y\n"
        "maintain r(P):\n"
        "    form/npc(P)\n"
        "    => add player/notification(P, \"hi\")\n");
    EXPECT_GE(errs.size(), 1u);
}

TEST(MaintainRules, MaintainUsingEventFails) {
    // event/entered_location in a maintain rule must error.
    auto errs = check_source(
        "namespace x.y\n"
        "maintain r(R, L):\n"
        "    event/entered_location(R, L)\n");
    EXPECT_GE(errs.size(), 1u);
}

TEST(MaintainRules, MaintainOnRetractableSetSucceeds) {
    // ref/keyword has retract_handler; legal in maintain.
    auto errs = check_source(
        "namespace x.y\n"
        "maintain r(R):\n"
        "    ref/base_form(R, Base)\n"
        "    form/faction(Base, @BanditFaction)\n"
        "    => add ref/keyword(R, @DangerMark)\n");
    EXPECT_EQ(errs.size(), 0u) << "first: " << (errs.empty() ? "" : errs[0]);
}

TEST(OnRules, OnUsingEventSucceeds) {
    auto errs = check_source(
        "namespace x.y\n"
        "on r(P, Loc):\n"
        "    event/entered_location(P, Loc)\n"
        "    => add player/gold(P, 100)\n");
    EXPECT_EQ(errs.size(), 0u) << "first: " << (errs.empty() ? "" : errs[0]);
}
```

- [ ] **Step 2: Run, verify tests pass**

Run: `xmake build test_verb_legality && xmake run test_verb_legality`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/sema/test_verb_legality.cpp
git commit -m "sema: activate dormant maintain/event legality tests with real dynamic relations"
```

---

## Phase B — Operator DAG representation (Tasks 5–7)

### Task 5: DagOpcode enum and DagNode struct

**Files:**
- Create: `include/mora/dag/opcode.h`
- Create: `include/mora/dag/node.h`
- Test: `tests/dag/test_dag_types.cpp`

- [ ] **Step 1: Failing test**

```cpp
// tests/dag/test_dag_types.cpp
#include "mora/dag/node.h"
#include "mora/dag/opcode.h"
#include <gtest/gtest.h>

using namespace mora::dag;

TEST(DagTypes, OpcodeEnumDistinct) {
    EXPECT_NE(DagOpcode::Filter, DagOpcode::Project);
    EXPECT_NE(DagOpcode::StaticProbe, DagOpcode::HashJoin);
    EXPECT_NE(DagOpcode::MaintainSink, DagOpcode::OnSink);
}

TEST(DagTypes, DagNodeDefaults) {
    DagNode n;
    EXPECT_EQ(n.opcode, DagOpcode::Unknown);
    EXPECT_EQ(n.input_count, 0u);
}

TEST(DagTypes, MaxInputsCompileTimeCheck) {
    static_assert(kMaxDagInputs >= 2);
    SUCCEED();
}
```

- [ ] **Step 2: Implement**

```cpp
// include/mora/dag/opcode.h
#pragma once
#include <cstdint>

namespace mora::dag {

enum class DagOpcode : uint16_t {
    Unknown       = 0,
    EventSource   = 1,  // input: SKSE event stream tagged by relation_id
    StateSource   = 2,  // input: reads a ref/player/world relation via a handler
    Filter        = 3,  // param: column index + constant
    Project       = 4,  // param: column permutation
    HashJoin      = 5,  // stateful join of two inputs on a key column
    StaticProbe   = 6,  // probe against a static arrangement
    MaintainSink  = 7,  // terminal: maintained effect
    OnSink        = 8,  // terminal: edge-triggered effect
};

constexpr uint8_t kMaxDagInputs    = 2;
constexpr uint8_t kMaxDagParams    = 4;

} // namespace mora::dag
```

```cpp
// include/mora/dag/node.h
#pragma once
#include "mora/dag/opcode.h"
#include "mora/model/handler_ids.h"
#include <cstdint>

namespace mora::dag {

struct DagNode {
    uint32_t  node_id    = 0;
    DagOpcode opcode     = DagOpcode::Unknown;
    uint16_t  relation_id = 0;   // for sources / probes
    model::HandlerId handler_id = model::HandlerId::None; // for sinks / state sources
    uint8_t   input_count = 0;
    uint32_t  inputs[kMaxDagInputs]{0, 0};
    uint32_t  params[kMaxDagParams]{0, 0, 0, 0};
};
static_assert(sizeof(DagNode) <= 64);

} // namespace mora::dag
```

- [ ] **Step 3: Run, verify PASS**

- [ ] **Step 4: Commit**

```bash
git add include/mora/dag/opcode.h include/mora/dag/node.h tests/dag/test_dag_types.cpp
git commit -m "dag: add operator opcodes and node struct"
```

---

### Task 6: DagGraph builder (in-memory DAG)

**Files:**
- Create: `include/mora/dag/graph.h`
- Create: `src/dag/graph.cpp`
- Test: `tests/dag/test_dag_graph.cpp`

- [ ] **Step 1: Failing test**

```cpp
// tests/dag/test_dag_graph.cpp
#include "mora/dag/graph.h"
#include <gtest/gtest.h>

using namespace mora::dag;

TEST(DagGraph, AddNodeAssignsSequentialIds) {
    DagGraph g;
    auto a = g.add_node({.opcode = DagOpcode::EventSource, .relation_id = 1});
    auto b = g.add_node({.opcode = DagOpcode::Filter});
    EXPECT_EQ(a, 0u);
    EXPECT_EQ(b, 1u);
    EXPECT_EQ(g.node_count(), 2u);
}

TEST(DagGraph, ConnectInput) {
    DagGraph g;
    auto src = g.add_node({.opcode = DagOpcode::EventSource});
    auto flt = g.add_node({.opcode = DagOpcode::Filter});
    g.set_input(flt, 0, src);
    EXPECT_EQ(g.node(flt).inputs[0], src);
    EXPECT_EQ(g.node(flt).input_count, 1u);
}

TEST(DagGraph, TopologicalOrderIsInsertionOrderWhenLinear) {
    DagGraph g;
    auto a = g.add_node({.opcode = DagOpcode::EventSource});
    auto b = g.add_node({.opcode = DagOpcode::Filter});
    g.set_input(b, 0, a);
    auto order = g.topological_order();
    EXPECT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], a);
    EXPECT_EQ(order[1], b);
}
```

- [ ] **Step 2: Implement**

```cpp
// include/mora/dag/graph.h
#pragma once
#include "mora/dag/node.h"
#include <cstdint>
#include <vector>

namespace mora::dag {

class DagGraph {
public:
    uint32_t add_node(DagNode n);
    void     set_input(uint32_t node_id, uint8_t slot, uint32_t source_id);
    const DagNode& node(uint32_t id) const { return nodes_[id]; }
    DagNode&       node(uint32_t id)       { return nodes_[id]; }
    size_t         node_count() const      { return nodes_.size(); }
    const std::vector<DagNode>& nodes() const { return nodes_; }

    std::vector<uint32_t> topological_order() const;

private:
    std::vector<DagNode> nodes_;
};

} // namespace mora::dag
```

```cpp
// src/dag/graph.cpp
#include "mora/dag/graph.h"
#include <algorithm>
#include <unordered_set>
#include <functional>

namespace mora::dag {

uint32_t DagGraph::add_node(DagNode n) {
    uint32_t id = static_cast<uint32_t>(nodes_.size());
    n.node_id = id;
    nodes_.push_back(n);
    return id;
}

void DagGraph::set_input(uint32_t node_id, uint8_t slot, uint32_t source_id) {
    auto& n = nodes_[node_id];
    n.inputs[slot] = source_id;
    if (slot + 1 > n.input_count) n.input_count = slot + 1;
}

std::vector<uint32_t> DagGraph::topological_order() const {
    // Inputs must have smaller node ids than their consumer — a post-condition
    // the builder maintains since add_node is monotonic. Just return insertion
    // order (0..N).
    std::vector<uint32_t> out(nodes_.size());
    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<uint32_t>(i);
    return out;
}

} // namespace mora::dag
```

- [ ] **Step 3: Run, PASS, commit**

```bash
git add include/mora/dag/graph.h src/dag/graph.cpp tests/dag/test_dag_graph.cpp
git commit -m "dag: add DagGraph in-memory builder"
```

---

### Task 7: DAG bytecode section serialization

**Files:**
- Create: `include/mora/dag/bytecode.h`
- Create: `src/dag/bytecode.cpp`
- Test: `tests/dag/test_bytecode_roundtrip.cpp`

- [ ] **Step 1: Failing test**

```cpp
// tests/dag/test_bytecode_roundtrip.cpp
#include "mora/dag/bytecode.h"
#include "mora/dag/graph.h"
#include <gtest/gtest.h>

using namespace mora::dag;

TEST(DagBytecode, RoundTripSingleNode) {
    DagGraph g;
    g.add_node({.opcode = DagOpcode::EventSource, .relation_id = 7});

    auto bytes = serialize_dag(g);
    auto loaded = deserialize_dag(bytes.data(), bytes.size());
    ASSERT_EQ(loaded.node_count(), 1u);
    EXPECT_EQ(loaded.node(0).opcode, DagOpcode::EventSource);
    EXPECT_EQ(loaded.node(0).relation_id, 7u);
}

TEST(DagBytecode, RoundTripMultipleNodesWithInputs) {
    DagGraph g;
    auto a = g.add_node({.opcode = DagOpcode::EventSource, .relation_id = 1});
    auto b = g.add_node({.opcode = DagOpcode::Filter});
    g.set_input(b, 0, a);
    auto c = g.add_node({.opcode = DagOpcode::OnSink,
                         .handler_id = mora::model::HandlerId::PlayerAddGold});
    g.set_input(c, 0, b);

    auto bytes = serialize_dag(g);
    auto loaded = deserialize_dag(bytes.data(), bytes.size());
    EXPECT_EQ(loaded.node_count(), 3u);
    EXPECT_EQ(loaded.node(c).inputs[0], b);
    EXPECT_EQ(loaded.node(c).handler_id, mora::model::HandlerId::PlayerAddGold);
}
```

- [ ] **Step 2: Implement**

```cpp
// include/mora/dag/bytecode.h
#pragma once
#include "mora/dag/graph.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mora::dag {

// Layout:
//   uint32 magic = 'DAG1'
//   uint32 node_count
//   DagNode[node_count]
std::vector<uint8_t> serialize_dag(const DagGraph& g);
DagGraph deserialize_dag(const uint8_t* data, size_t size);

} // namespace mora::dag
```

```cpp
// src/dag/bytecode.cpp
#include "mora/dag/bytecode.h"
#include <cstring>

namespace mora::dag {

static constexpr uint32_t kDagMagic = 0x31474144u; // 'DAG1' little-endian

std::vector<uint8_t> serialize_dag(const DagGraph& g) {
    uint32_t header[2] = { kDagMagic, static_cast<uint32_t>(g.node_count()) };
    size_t total = sizeof(header) + g.node_count() * sizeof(DagNode);
    std::vector<uint8_t> out(total);
    std::memcpy(out.data(), header, sizeof(header));
    if (g.node_count())
        std::memcpy(out.data() + sizeof(header), g.nodes().data(),
                    g.node_count() * sizeof(DagNode));
    return out;
}

DagGraph deserialize_dag(const uint8_t* data, size_t size) {
    DagGraph g;
    if (size < 8) return g;
    uint32_t magic = 0, count = 0;
    std::memcpy(&magic, data, 4);
    std::memcpy(&count, data + 4, 4);
    if (magic != kDagMagic) return g;
    if (size < 8 + count * sizeof(DagNode)) return g;
    for (uint32_t i = 0; i < count; ++i) {
        DagNode n;
        std::memcpy(&n, data + 8 + i * sizeof(DagNode), sizeof(DagNode));
        g.add_node(n);
    }
    return g;
}

} // namespace mora::dag
```

- [ ] **Step 3: Run, PASS, commit**

```bash
git add include/mora/dag/bytecode.h src/dag/bytecode.cpp tests/dag/test_bytecode_roundtrip.cpp
git commit -m "dag: serialize/deserialize DagGraph bytecode"
```

---

## Phase C — Rule-to-DAG compilation (Tasks 8–9)

### Task 8: Minimal DAG compiler for `on` rules with one event source

This task supports only the simplest case: an `on` rule whose body has one `event/*` pattern, optional `form/*` qualifier patterns (via `StaticProbe`), and a single effect. That already covers meaningful use cases like "give gold on city entry".

**Files:**
- Create: `include/mora/dag/compile.h`
- Create: `src/dag/compile.cpp`
- Test: `tests/dag/test_compile_simple.cpp`

- [ ] **Step 1: Failing test**

```cpp
// tests/dag/test_compile_simple.cpp
#include "mora/dag/compile.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include <gtest/gtest.h>

using namespace mora;
using namespace mora::dag;

static Module parse(StringPool& pool, const char* src) {
    Lexer lex(src, "t.mora", pool);
    auto t = lex.tokenize();
    Parser p(t, pool);
    auto m = p.parse();
    DiagBag d;
    NameResolver nr(pool, d); nr.resolve(m);
    return m;
}

TEST(DagCompile, SimpleOnRuleProducesSourceAndSink) {
    StringPool pool;
    auto m = parse(pool,
        "namespace x.y\n"
        "on gift(P, L):\n"
        "    event/entered_location(P, L)\n"
        "    => add player/gold(P, 100)\n");

    DagGraph g;
    CompileResult res = compile_dynamic_rules(m, pool, g);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.errors.size(), 0u);

    // Expect at least: EventSource + OnSink.
    bool has_src = false, has_sink = false;
    for (size_t i = 0; i < g.node_count(); ++i) {
        if (g.node(i).opcode == DagOpcode::EventSource) has_src = true;
        if (g.node(i).opcode == DagOpcode::OnSink)      has_sink = true;
    }
    EXPECT_TRUE(has_src);
    EXPECT_TRUE(has_sink);
}

TEST(DagCompile, StaticRuleIsNotLowered) {
    StringPool pool;
    auto m = parse(pool,
        "namespace x.y\n"
        "r(W):\n"
        "    form/weapon(W)\n"
        "    => set form/damage(W, 20)\n");

    DagGraph g;
    CompileResult res = compile_dynamic_rules(m, pool, g);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(g.node_count(), 0u);  // static rule: not lowered to DAG
}
```

- [ ] **Step 2: Implement**

```cpp
// include/mora/dag/compile.h
#pragma once
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/dag/graph.h"
#include <string>
#include <vector>

namespace mora::dag {

struct CompileResult {
    bool success = false;
    std::vector<std::string> errors;
};

CompileResult compile_dynamic_rules(const Module& m, StringPool& pool, DagGraph& g);

} // namespace mora::dag
```

Implementation sketch (src/dag/compile.cpp):

The compiler processes each `Rule` with `RuleKind == Maintain` or `RuleKind == On`. For the initial scope:

1. Find the `event/*` FactPattern (or the first `ref/*` state pattern for `maintain` rules) — that's the source node.
2. Add `StaticProbe` nodes for each subsequent `form/*` FactPattern that constrains the binding.
3. Terminate with a sink whose `handler_id` is taken from the effect's `RelationEntry::apply_handler`.

The first iteration can be very literal — one node per body clause, threaded linearly. Join-shape optimization comes later.

```cpp
#include "mora/dag/compile.h"
#include "mora/model/relations.h"

namespace mora::dag {

namespace {

uint32_t relation_index(std::string_view ns, std::string_view name) {
    for (size_t i = 0; i < mora::model::kRelationCount; ++i) {
        if (mora::model::kRelations[i].namespace_ == ns
            && mora::model::kRelations[i].name == name) {
            return static_cast<uint32_t>(i);
        }
    }
    return static_cast<uint32_t>(-1);
}

} // anonymous

CompileResult compile_dynamic_rules(const Module& m, StringPool& pool, DagGraph& g) {
    CompileResult res{.success = true};

    for (const auto& rule : m.rules) {
        if (rule.kind == RuleKind::Static) continue;

        // Find the source clause: first event/* for On, first dynamic ref/player/world
        // for Maintain.
        uint32_t source_node = static_cast<uint32_t>(-1);
        for (const auto& c : rule.body) {
            if (!std::holds_alternative<FactPattern>(c.data)) continue;
            const auto& fp = std::get<FactPattern>(c.data);
            std::string ns{pool.get(fp.qualifier)};
            std::string nm{pool.get(fp.name)};
            auto rel_idx = relation_index(ns, nm);
            if (rel_idx == static_cast<uint32_t>(-1)) continue;

            const auto& rel = mora::model::kRelations[rel_idx];
            if (rule.kind == RuleKind::On && rel.source == mora::model::RelationSourceKind::Event) {
                source_node = g.add_node({.opcode = DagOpcode::EventSource,
                                          .relation_id = static_cast<uint16_t>(rel_idx)});
                break;
            }
            if (rule.kind == RuleKind::Maintain
                && rel.source != mora::model::RelationSourceKind::Static
                && rel.source != mora::model::RelationSourceKind::Event) {
                source_node = g.add_node({.opcode = DagOpcode::StateSource,
                                          .relation_id = static_cast<uint16_t>(rel_idx)});
                break;
            }
        }

        if (source_node == static_cast<uint32_t>(-1)) {
            // Rule has no valid dynamic source — skip silently for now; the
            // semantic checker will already have flagged this in stricter cases.
            continue;
        }

        // Thread StaticProbes for each form/* static pattern.
        uint32_t prev = source_node;
        for (const auto& c : rule.body) {
            if (!std::holds_alternative<FactPattern>(c.data)) continue;
            const auto& fp = std::get<FactPattern>(c.data);
            std::string ns{pool.get(fp.qualifier)};
            if (ns != "form") continue;
            std::string nm{pool.get(fp.name)};
            auto rel_idx = relation_index(ns, nm);
            if (rel_idx == static_cast<uint32_t>(-1)) continue;
            uint32_t probe = g.add_node({
                .opcode = DagOpcode::StaticProbe,
                .relation_id = static_cast<uint16_t>(rel_idx),
            });
            g.set_input(probe, 0, prev);
            prev = probe;
        }

        // Terminal: one sink per effect (simple: chain them from the same prev).
        DagOpcode sink_op = (rule.kind == RuleKind::Maintain)
                            ? DagOpcode::MaintainSink
                            : DagOpcode::OnSink;
        for (const auto& e : rule.effects) {
            std::string ns{pool.get(e.namespace_)};
            std::string nm{pool.get(e.name)};
            auto rel_idx = relation_index(ns, nm);
            mora::model::HandlerId hid = mora::model::HandlerId::None;
            if (rel_idx != static_cast<uint32_t>(-1)) {
                hid = mora::model::kRelations[rel_idx].apply_handler;
            }
            uint32_t sink = g.add_node({
                .opcode = sink_op,
                .relation_id = static_cast<uint16_t>(rel_idx),
                .handler_id  = hid,
            });
            g.set_input(sink, 0, prev);
        }
    }

    return res;
}

} // namespace mora::dag
```

- [ ] **Step 3: Run, PASS, commit**

```bash
git add include/mora/dag/compile.h src/dag/compile.cpp tests/dag/test_compile_simple.cpp
git commit -m "dag: compile simple maintain/on rules to operator DAG"
```

---

### Task 9: Emit the DagBytecode section at compile time

**Files:**
- Modify: `include/mora/emit/patch_table.h`
- Modify: `src/emit/patch_table.cpp`
- Modify: `src/main.cpp`
- Test: `tests/cli/test_dag_section_emitted.cpp`

- [ ] **Step 1: Failing test**

```cpp
// tests/cli/test_dag_section_emitted.cpp
#include "mora/emit/patch_table.h"
#include "mora/emit/patch_file_v2.h"
#include "mora/dag/graph.h"
#include "mora/dag/bytecode.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace mora;

TEST(DagSectionEmitted, DagBytecodeSectionPresentWhenGraphNonEmpty) {
    dag::DagGraph g;
    g.add_node({.opcode = dag::DagOpcode::EventSource, .relation_id = 0});
    auto dag_bytes = dag::serialize_dag(g);

    std::vector<PatchEntry> entries = { {0x1u, 0, 0, 1, 0, 0u} };
    std::array<uint8_t, 32> digest{};
    auto bytes = serialize_patch_table(entries, digest, /*arrangements*/ {}, dag_bytes);

    emit::PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    const emit::SectionDirectoryEntry* dir =
        reinterpret_cast<const emit::SectionDirectoryEntry*>(bytes.data() + sizeof(h));
    bool found = false;
    for (uint32_t i = 0; i < h.section_count; ++i) {
        if (dir[i].section_id == static_cast<uint32_t>(emit::SectionId::DagBytecode)) {
            found = true;
            EXPECT_EQ(dir[i].size, dag_bytes.size());
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(DagSectionEmitted, EmptyDagOmitsSection) {
    std::vector<PatchEntry> entries = { {0x1u, 0, 0, 1, 0, 0u} };
    std::array<uint8_t, 32> digest{};
    auto bytes = serialize_patch_table(entries, digest, {}, /*dag*/ {});
    emit::PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    const emit::SectionDirectoryEntry* dir =
        reinterpret_cast<const emit::SectionDirectoryEntry*>(bytes.data() + sizeof(h));
    for (uint32_t i = 0; i < h.section_count; ++i) {
        EXPECT_NE(dir[i].section_id, static_cast<uint32_t>(emit::SectionId::DagBytecode));
    }
}
```

- [ ] **Step 2: Add a new overload**

In `include/mora/emit/patch_table.h`:

```cpp
std::vector<uint8_t> serialize_patch_table(
    const std::vector<PatchEntry>& entries,
    const std::array<uint8_t, 32>& esp_digest,
    const std::vector<uint8_t>& arrangements_section,
    const std::vector<uint8_t>& dag_bytecode);
```

In `src/emit/patch_table.cpp`:

```cpp
std::vector<uint8_t> serialize_patch_table(
    const std::vector<PatchEntry>& entries,
    const std::array<uint8_t, 32>& esp_digest,
    const std::vector<uint8_t>& arrangements_section,
    const std::vector<uint8_t>& dag_bytecode) {

    emit::FlatFileWriter w;
    w.set_esp_digest(esp_digest);
    w.add_section(emit::SectionId::Patches,
                  entries.data(), entries.size() * sizeof(PatchEntry));
    if (!arrangements_section.empty())
        w.add_section(emit::SectionId::Arrangements,
                      arrangements_section.data(), arrangements_section.size());
    if (!dag_bytecode.empty())
        w.add_section(emit::SectionId::DagBytecode,
                      dag_bytecode.data(), dag_bytecode.size());
    return w.finish();
}
```

- [ ] **Step 3: Wire into main.cpp**

After the CLI has parsed and resolved the module, call `compile_dynamic_rules(module, pool, g)` to build the DAG, serialize it, and pass the bytes into the new serializer overload.

- [ ] **Step 4: Run, PASS, commit**

```bash
git add include/mora/emit/patch_table.h src/emit/patch_table.cpp src/main.cpp \
        tests/cli/test_dag_section_emitted.cpp
git commit -m "emit: write DagBytecode section for compiled dynamic rules"
```

---

## Phase D — Runtime engine (Tasks 10–13)

### Task 10: Delta type and DeltaQueue

**Files:**
- Create: `include/mora/rt/delta.h`
- Test: `tests/rt/test_delta.cpp`

- [ ] **Step 1: Failing test**

```cpp
// tests/rt/test_delta.cpp
#include "mora/rt/delta.h"
#include <gtest/gtest.h>

using namespace mora::rt;

TEST(Delta, PositiveAndNegative) {
    Delta d{.tuple = {1, 2, 3}, .diff = +1};
    EXPECT_EQ(d.diff, 1);
    EXPECT_EQ(d.tuple.size(), 3u);

    Delta r{.tuple = {1, 2, 3}, .diff = -1};
    EXPECT_EQ(r.diff, -1);
}

TEST(DeltaQueue, FifoPushPop) {
    DeltaQueue q;
    q.push(0, {.tuple = {1}, .diff = +1});
    q.push(0, {.tuple = {2}, .diff = +1});
    ASSERT_EQ(q.size(), 2u);
    auto [node, d] = q.pop();
    EXPECT_EQ(node, 0u);
    EXPECT_EQ(d.tuple[0], 1u);
}
```

- [ ] **Step 2: Implement**

```cpp
// include/mora/rt/delta.h
#pragma once
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace mora::rt {

using TupleU32 = std::vector<uint32_t>;

struct Delta {
    TupleU32 tuple;
    int8_t   diff = 0;  // +1 insert, -1 retract
};

class DeltaQueue {
public:
    void push(uint32_t node_id, Delta d) { q_.push_back({node_id, std::move(d)}); }
    std::pair<uint32_t, Delta> pop() {
        auto v = std::move(q_.front()); q_.pop_front(); return v;
    }
    bool empty() const { return q_.empty(); }
    size_t size() const { return q_.size(); }

private:
    std::deque<std::pair<uint32_t, Delta>> q_;
};

} // namespace mora::rt
```

- [ ] **Step 3: Run, PASS, commit**

```bash
git add include/mora/rt/delta.h tests/rt/test_delta.cpp
git commit -m "rt: add Delta type and DeltaQueue"
```

---

### Task 11: DagEngine — delta propagation loop

**Files:**
- Create: `include/mora/rt/dag_engine.h`
- Create: `src/rt/dag_engine.cpp`
- Test: `tests/rt/test_dag_engine.cpp`

- [ ] **Step 1: Failing test**

```cpp
// tests/rt/test_dag_engine.cpp
#include "mora/rt/dag_engine.h"
#include "mora/rt/handler_registry.h"
#include "mora/dag/graph.h"
#include <gtest/gtest.h>

using namespace mora::rt;
using namespace mora::dag;
using namespace mora::model;

TEST(DagEngine, EventThroughSourceToOnSinkFiresHandler) {
    DagGraph g;
    auto src = g.add_node({.opcode = DagOpcode::EventSource, .relation_id = 0});
    auto sink = g.add_node({
        .opcode = DagOpcode::OnSink,
        .handler_id = HandlerId::PlayerAddGold,
    });
    g.set_input(sink, 0, src);

    HandlerRegistry reg;
    int calls = 0;
    reg.bind_effect(HandlerId::PlayerAddGold,
        [&](const EffectArgs&){ ++calls; return EffectHandle{1}; });

    DagEngine engine(g, reg);
    engine.inject_delta(src, Delta{.tuple = {14u, 100u}, .diff = +1});
    engine.run_to_quiescence();
    EXPECT_EQ(calls, 1);

    // Retractions don't fire for OnSink.
    engine.inject_delta(src, Delta{.tuple = {14u, 100u}, .diff = -1});
    engine.run_to_quiescence();
    EXPECT_EQ(calls, 1);
}

TEST(DagEngine, MaintainSinkCallsApplyThenRetract) {
    DagGraph g;
    auto src = g.add_node({.opcode = DagOpcode::StateSource, .relation_id = 0});
    auto sink = g.add_node({
        .opcode = DagOpcode::MaintainSink,
        .handler_id = HandlerId::RefAddKeyword,
    });
    g.set_input(sink, 0, src);

    HandlerRegistry reg;
    int applies = 0, retracts = 0;
    reg.bind_effect(HandlerId::RefAddKeyword,
        [&](const EffectArgs&){ ++applies; return EffectHandle{42}; });
    reg.bind_retract(HandlerId::RefAddKeyword,
        [&](EffectHandle h){ ++retracts; EXPECT_EQ(h.id, 42u); });

    DagEngine engine(g, reg);
    engine.inject_delta(src, Delta{.tuple = {7u, 9u}, .diff = +1});
    engine.run_to_quiescence();
    EXPECT_EQ(applies, 1);
    EXPECT_EQ(retracts, 0);

    engine.inject_delta(src, Delta{.tuple = {7u, 9u}, .diff = -1});
    engine.run_to_quiescence();
    EXPECT_EQ(applies, 1);
    EXPECT_EQ(retracts, 1);
}
```

- [ ] **Step 2: Implement**

```cpp
// include/mora/rt/dag_engine.h
#pragma once
#include "mora/dag/graph.h"
#include "mora/rt/delta.h"
#include "mora/rt/handler_registry.h"
#include <unordered_map>
#include <vector>

namespace mora::rt {

class DagEngine {
public:
    DagEngine(const dag::DagGraph& g, HandlerRegistry& reg);

    void inject_delta(uint32_t source_node_id, Delta d);
    void run_to_quiescence();

private:
    // For each node, list of downstream consumer node ids.
    std::vector<std::vector<uint32_t>> consumers_;
    // Per-(node_id, tuple) binding state for MaintainSinks.
    std::unordered_map<uint64_t, EffectHandle> maintain_state_;

    const dag::DagGraph& graph_;
    HandlerRegistry& reg_;
    DeltaQueue queue_;

    void propagate(uint32_t node_id, const Delta& d);
    void fire_sink(const dag::DagNode& node, const Delta& d);
    uint64_t maintain_key(uint32_t node_id, const TupleU32& tuple) const;
};

} // namespace mora::rt
```

```cpp
// src/rt/dag_engine.cpp
#include "mora/rt/dag_engine.h"
#include <cstring>

namespace mora::rt {

DagEngine::DagEngine(const dag::DagGraph& g, HandlerRegistry& reg)
    : graph_(g), reg_(reg) {
    consumers_.resize(g.node_count());
    for (uint32_t i = 0; i < g.node_count(); ++i) {
        const auto& n = g.node(i);
        for (uint8_t s = 0; s < n.input_count; ++s) {
            consumers_[n.inputs[s]].push_back(i);
        }
    }
}

void DagEngine::inject_delta(uint32_t source_node_id, Delta d) {
    queue_.push(source_node_id, std::move(d));
}

uint64_t DagEngine::maintain_key(uint32_t node_id, const TupleU32& t) const {
    uint64_t h = node_id * 0x9E3779B1u;
    for (auto v : t) h = h * 0x100000001B3ull + v;
    return h;
}

void DagEngine::fire_sink(const dag::DagNode& node, const Delta& d) {
    EffectArgs args{.args = d.tuple};
    if (node.opcode == dag::DagOpcode::OnSink) {
        if (d.diff > 0) reg_.invoke_effect(node.handler_id, args);
        // retractions ignored
    } else if (node.opcode == dag::DagOpcode::MaintainSink) {
        uint64_t key = maintain_key(node.node_id, d.tuple);
        if (d.diff > 0) {
            auto h = reg_.invoke_effect(node.handler_id, args);
            maintain_state_[key] = h;
        } else {
            auto it = maintain_state_.find(key);
            if (it != maintain_state_.end()) {
                reg_.invoke_retract(node.handler_id, it->second);
                maintain_state_.erase(it);
            }
        }
    }
}

void DagEngine::propagate(uint32_t node_id, const Delta& d) {
    const auto& node = graph_.node(node_id);
    // Terminal sinks handle the delta directly.
    if (node.opcode == dag::DagOpcode::OnSink
        || node.opcode == dag::DagOpcode::MaintainSink) {
        fire_sink(node, d);
        return;
    }
    // Pass-through / simple nodes: forward to consumers.
    // TODO(plan3+): Filter, Project, StaticProbe, HashJoin actually transform
    // the delta before forwarding. The initial engine treats them as
    // pass-throughs so we get an end-to-end pipe working.
    for (uint32_t c : consumers_[node_id]) {
        queue_.push(c, d);
    }
}

void DagEngine::run_to_quiescence() {
    while (!queue_.empty()) {
        auto [nid, d] = queue_.pop();
        propagate(nid, d);
    }
}

} // namespace mora::rt
```

- [ ] **Step 3: Run, PASS, commit**

```bash
git add include/mora/rt/dag_engine.h src/rt/dag_engine.cpp tests/rt/test_dag_engine.cpp
git commit -m "rt: add DagEngine delta propagation loop with sinks"
```

---

### Task 12: StaticProbe operator joining against an ArrangementView

**Files:**
- Modify: `include/mora/rt/dag_engine.h`
- Modify: `src/rt/dag_engine.cpp`
- Test: extend `tests/rt/test_dag_engine.cpp`

The current engine treats all non-sink nodes as pass-throughs. This task implements real semantics for `StaticProbe`: on a delta whose key column matches an arrangement row, emit a widened delta that includes the matched column(s). This is what lets `event/entered_location(P, Loc)` join against `form/keyword(Loc, @LocTypeCity)`.

- [ ] **Step 1: Failing test**

```cpp
TEST(DagEngine, StaticProbeFiltersAndWidens) {
    // Build a tiny arrangement: locations with @LocTypeCity.
    // Arrangement rows: (Loc, KW).
    auto bytes = mora::emit::build_u32_arrangement(
        /*relation_id=*/0,
        std::vector<std::array<uint32_t, 2>>{{{100u, 999u}}}, // Loc=100 has KW=999
        /*key_column=*/0);

    DagGraph g;
    auto src   = g.add_node({.opcode = DagOpcode::EventSource, .relation_id = 0});
    auto probe = g.add_node({.opcode = DagOpcode::StaticProbe, .relation_id = 1});
    auto sink  = g.add_node({
        .opcode = DagOpcode::OnSink,
        .handler_id = HandlerId::PlayerAddGold,
    });
    g.set_input(probe, 0, src);
    g.set_input(sink, 0, probe);

    HandlerRegistry reg;
    int calls = 0;
    reg.bind_effect(HandlerId::PlayerAddGold,
        [&](const EffectArgs&){ ++calls; return EffectHandle{1}; });

    DagEngine engine(g, reg);
    engine.register_arrangement(probe, bytes.data(), bytes.size(), /*key_col_in_delta=*/1);

    // Delta tuple: (Player, Loc). Loc=100 matches, Loc=200 doesn't.
    engine.inject_delta(src, Delta{.tuple = {14u, 100u}, .diff = +1});
    engine.run_to_quiescence();
    EXPECT_EQ(calls, 1);

    engine.inject_delta(src, Delta{.tuple = {14u, 200u}, .diff = +1});
    engine.run_to_quiescence();
    EXPECT_EQ(calls, 1);  // no match, no additional call
}
```

- [ ] **Step 2: Implement probe semantics**

Extend the engine:

```cpp
// dag_engine.h — add:
void register_arrangement(uint32_t probe_node_id,
                          const uint8_t* data, size_t size,
                          uint8_t key_col_in_delta);
```

```cpp
// dag_engine.cpp — new field
std::unordered_map<uint32_t, ArrangementView> probes_;
std::unordered_map<uint32_t, uint8_t> probe_key_cols_;

void DagEngine::register_arrangement(uint32_t node_id,
                                     const uint8_t* data, size_t size,
                                     uint8_t key_col_in_delta) {
    probes_.emplace(node_id, ArrangementView(data, size));
    probe_key_cols_[node_id] = key_col_in_delta;
}

// In propagate(), replace the StaticProbe pass-through branch:
if (node.opcode == dag::DagOpcode::StaticProbe) {
    auto it = probes_.find(node_id);
    if (it == probes_.end()) return;  // no arrangement registered
    uint8_t kc = probe_key_cols_[node_id];
    if (d.tuple.size() <= kc) return;
    uint32_t key = d.tuple[kc];
    auto rng = it->second.equal_range_u32(key);
    if (rng.count == 0) return;  // filter: no match, drop delta
    for (uint32_t c : consumers_[node_id]) {
        queue_.push(c, d);  // pass-through (widening comes in a later plan)
    }
    return;
}
```

For the initial implementation we just use StaticProbe as a filter. Widening (carrying additional columns into the delta) is a more invasive change and can come in a later plan.

- [ ] **Step 3: Run, PASS, commit**

```bash
git add include/mora/rt/dag_engine.h src/rt/dag_engine.cpp tests/rt/test_dag_engine.cpp
git commit -m "rt: StaticProbe operator filters via ArrangementView"
```

---

### Task 13: Integrate DAG engine into patch_walker load flow

**Files:**
- Modify: `src/rt/patch_walker.cpp`
- Create: `include/mora/rt/dag_runtime.h`
- Create: `src/rt/dag_runtime.cpp`
- Test: `tests/rt/test_dag_runtime_load.cpp`

Wire the engine into the runtime boot sequence. When `load_patches()` opens a v2 file and finds a `DagBytecode` section, deserialize it and stand up a `DagEngine` with an associated `HandlerRegistry`.

- [ ] **Step 1: Failing test**

```cpp
// tests/rt/test_dag_runtime_load.cpp
#include "mora/rt/dag_runtime.h"
#include "mora/emit/patch_table.h"
#include "mora/dag/graph.h"
#include "mora/dag/bytecode.h"
#include "mora/rt/mapped_patch_file.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

using namespace mora;

TEST(DagRuntimeLoad, InitializesFromDagBytecodeSection) {
    dag::DagGraph g;
    g.add_node({.opcode = dag::DagOpcode::EventSource, .relation_id = 0});
    auto dag_bytes = dag::serialize_dag(g);

    std::vector<PatchEntry> entries = { {0x1u, 0, 0, 1, 0, 0u} };
    std::array<uint8_t, 32> digest{};
    auto bytes = serialize_patch_table(entries, digest, {}, dag_bytes);

    auto path = std::filesystem::temp_directory_path()
              / ("mora_dr_" + std::to_string(std::rand()) + ".bin");
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    f.close();

    rt::MappedPatchFile mpf;
    ASSERT_TRUE(mpf.open(path.string()));
    rt::DagRuntime dr;
    ASSERT_TRUE(dr.init_from(mpf));
    EXPECT_EQ(dr.dag().node_count(), 1u);

    std::filesystem::remove(path);
}
```

- [ ] **Step 2: Implement**

```cpp
// include/mora/rt/dag_runtime.h
#pragma once
#include "mora/rt/mapped_patch_file.h"
#include "mora/rt/dag_engine.h"
#include "mora/dag/graph.h"
#include "mora/rt/handler_registry.h"
#include <memory>

namespace mora::rt {

class DagRuntime {
public:
    bool init_from(const MappedPatchFile& mpf);
    const dag::DagGraph& dag() const { return graph_; }
    DagEngine* engine() { return engine_.get(); }
    HandlerRegistry& registry() { return registry_; }

private:
    dag::DagGraph graph_;
    HandlerRegistry registry_;
    std::unique_ptr<DagEngine> engine_;
};

} // namespace mora::rt
```

```cpp
// src/rt/dag_runtime.cpp
#include "mora/rt/dag_runtime.h"
#include "mora/dag/bytecode.h"

namespace mora::rt {

bool DagRuntime::init_from(const MappedPatchFile& mpf) {
    auto sec = mpf.section(emit::SectionId::DagBytecode);
    if (!sec.data) return true;  // no dynamic rules; OK.
    graph_ = dag::deserialize_dag(sec.data, sec.size);
    engine_ = std::make_unique<DagEngine>(graph_, registry_);
    return true;
}

} // namespace mora::rt
```

In `src/rt/patch_walker.cpp`, after `load_patches()` finishes loading the mapped file, construct a `DagRuntime` as a new static global and initialize it.

- [ ] **Step 3: Run, PASS, commit**

```bash
git add include/mora/rt/dag_runtime.h src/rt/dag_runtime.cpp src/rt/patch_walker.cpp \
        tests/rt/test_dag_runtime_load.cpp
git commit -m "rt: load DagBytecode section into DagRuntime at patch load time"
```

---

## Phase E — Real handler implementations + SKSE hooks (Tasks 14–16)

### Task 14: Implement effect handlers (Windows/runtime build only)

**Files:**
- Create: `src/rt/handlers/ref_handlers.cpp`
- Create: `src/rt/handlers/player_handlers.cpp`
- Modify: `src/rt/patch_walker.cpp` (or wherever the global HandlerRegistry is set up) — bind the handlers.
- Test: Linux-side tests can verify binding; real behavior is Windows-only.

These are Windows-only implementations that talk to CommonLibSSE-NG. Guard with `#ifdef _WIN32` or place in files only compiled for the runtime target (see the existing `patch_walker.cpp` for the pattern).

- [ ] **Step 1: Implement ref handlers**

```cpp
// src/rt/handlers/ref_handlers.cpp
#ifdef _WIN32
#include "mora/rt/handler_registry.h"
#include "mora/rt/form_ops.h"
#include <RE/Skyrim.h>

namespace mora::rt {

void bind_ref_handlers(HandlerRegistry& r) {
    r.bind_effect(model::HandlerId::RefAddKeyword,
        [](const EffectArgs& a) -> EffectHandle {
            if (a.args.size() < 2) return {};
            uint32_t ref_id = a.args[0];
            uint32_t kw_id  = a.args[1];
            auto* form = RE::TESForm::LookupByID(ref_id);
            auto* kw   = RE::TESForm::LookupByID(kw_id);
            if (!form || !kw) return {};
            auto* keyword = kw->As<RE::BGSKeyword>();
            if (!keyword) return {};
            mora_rt_add_keyword(form, keyword);
            // Encode (ref_id, kw_id) into the 64-bit handle for retraction.
            uint64_t handle_id = (uint64_t(ref_id) << 32) | kw_id;
            return { handle_id };
        });

    r.bind_retract(model::HandlerId::RefAddKeyword,
        [](EffectHandle h) {
            uint32_t ref_id = static_cast<uint32_t>(h.id >> 32);
            uint32_t kw_id  = static_cast<uint32_t>(h.id);
            auto* form = RE::TESForm::LookupByID(ref_id);
            auto* kw   = RE::TESForm::LookupByID(kw_id);
            if (!form || !kw) return;
            auto* keyword = kw->As<RE::BGSKeyword>();
            if (!keyword) return;
            mora_rt_remove_keyword(form, keyword);
        });
}

} // namespace mora::rt
#endif
```

Implement `player_handlers.cpp` similarly — `PlayerAddGold` calls the player's inventory add-gold helper; `PlayerShowNotification` calls the HUD notification helper.

- [ ] **Step 2: Call `bind_ref_handlers(reg)` etc. from patch_walker.cpp after DagRuntime init**

- [ ] **Step 3: Commit**

```bash
git add src/rt/handlers/ src/rt/patch_walker.cpp
git commit -m "rt: implement ref/player effect handlers via CommonLibSSE-NG"
```

---

### Task 15: SKSE hooks feed events into the DAG engine

**Files:**
- Create: `include/mora/rt/skse_hooks.h`
- Create: `src/rt/skse_hooks.cpp`
- Modify: `src/rt/plugin_entry.cpp`
- Test: none (runtime-only, requires in-game verification)

- [ ] **Step 1: Implement**

```cpp
// src/rt/skse_hooks.cpp
#ifdef _WIN32
#include "mora/rt/skse_hooks.h"
#include "mora/rt/dag_runtime.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace mora::rt {

static DagRuntime* g_dag = nullptr;

// event/entered_location relation id — resolved on registration.
static uint32_t g_entered_location_node = 0xFFFFFFFFu;

void register_skse_hooks(DagRuntime& dr) {
    g_dag = &dr;

    // Find the EventSource node for event/entered_location.
    for (uint32_t i = 0; i < dr.dag().node_count(); ++i) {
        const auto& n = dr.dag().node(i);
        if (n.opcode == dag::DagOpcode::EventSource) {
            // Match by relation_id against kRelations index.
            const auto& rel = mora::model::kRelations[n.relation_id];
            if (rel.namespace_ == "event" && rel.name == "entered_location") {
                g_entered_location_node = i;
                break;
            }
        }
    }

    // OnLocationChange sink (SKSE BGSActorCellEvent-ish — use existing API).
    // The exact SKSE API depends on CommonLibSSE-NG; below is illustrative.
    class Sink : public RE::BSTEventSink<RE::TESActivateEvent> {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESActivateEvent* evt,
            RE::BSTEventSource<RE::TESActivateEvent>*) override {
            if (evt && g_dag && g_entered_location_node != 0xFFFFFFFFu) {
                Delta d;
                d.tuple = { evt->actionRef ? evt->actionRef->GetFormID() : 0u,
                            evt->objectActivated ? evt->objectActivated->GetFormID() : 0u };
                d.diff = +1;
                g_dag->engine()->inject_delta(g_entered_location_node, std::move(d));
                g_dag->engine()->run_to_quiescence();
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };
    static Sink s;
    RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(&s);
}

} // namespace mora::rt
#endif
```

The actual SKSE hook choice for "entered location" depends on which CommonLibSSE-NG API is most appropriate (the code above uses `TESActivateEvent` as an illustration — in practice you'll want a location-change hook). Pick the closest available event and document the mapping in a comment.

- [ ] **Step 2: Hook into plugin_entry.cpp**

After `DagRuntime::init_from(mpf)` succeeds, call `register_skse_hooks(dag_runtime)`.

- [ ] **Step 3: Commit**

```bash
git add include/mora/rt/skse_hooks.h src/rt/skse_hooks.cpp src/rt/plugin_entry.cpp
git commit -m "rt: register SKSE event sinks that feed deltas into DagEngine"
```

---

### Task 16: End-to-end verification test

**Files:**
- Create: `tests/rt/test_dynamic_end_to_end.cpp`

Linux-side test that exercises compile → serialize → load → engine propagation → handler invocation, using stub handlers. This proves the whole pipeline works without requiring a running Skyrim.

- [ ] **Step 1: Write the test**

```cpp
// tests/rt/test_dynamic_end_to_end.cpp
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include "mora/dag/compile.h"
#include "mora/dag/bytecode.h"
#include "mora/rt/dag_engine.h"
#include "mora/rt/handler_registry.h"
#include <gtest/gtest.h>

using namespace mora;

TEST(DynamicEndToEnd, CitySayHelloOnFire) {
    StringPool pool;
    const char* src =
        "namespace x.y\n"
        "on greet(P, L):\n"
        "    event/entered_location(P, L)\n"
        "    => add player/gold(P, 100)\n";
    Lexer lex(src, "t.mora", pool);
    auto toks = lex.tokenize();
    Parser p(toks, pool);
    auto m = p.parse();
    DiagBag diag;
    NameResolver nr(pool, diag); nr.resolve(m);
    ASSERT_EQ(diag.errors().size(), 0u);

    dag::DagGraph g;
    auto res = dag::compile_dynamic_rules(m, pool, g);
    ASSERT_TRUE(res.success);
    ASSERT_GT(g.node_count(), 0u);

    // Serialize and deserialize to simulate the patch-file round trip.
    auto bytes = dag::serialize_dag(g);
    auto loaded = dag::deserialize_dag(bytes.data(), bytes.size());
    ASSERT_EQ(loaded.node_count(), g.node_count());

    // Stub the PlayerAddGold handler.
    rt::HandlerRegistry reg;
    int fires = 0;
    int received_gold = 0;
    reg.bind_effect(model::HandlerId::PlayerAddGold,
        [&](const rt::EffectArgs& a){
            ++fires;
            if (a.args.size() >= 3) received_gold = a.args[2];
            return rt::EffectHandle{1};
        });

    rt::DagEngine engine(loaded, reg);

    // Find the EventSource node for event/entered_location.
    uint32_t src_node = static_cast<uint32_t>(-1);
    for (uint32_t i = 0; i < loaded.node_count(); ++i) {
        if (loaded.node(i).opcode == dag::DagOpcode::EventSource) {
            src_node = i;
            break;
        }
    }
    ASSERT_NE(src_node, static_cast<uint32_t>(-1));

    // Inject an "entered_location" event.
    engine.inject_delta(src_node, rt::Delta{.tuple = {14u, 100u}, .diff = +1});
    engine.run_to_quiescence();

    EXPECT_EQ(fires, 1);
}
```

- [ ] **Step 2: Run and verify PASS**

- [ ] **Step 3: Commit**

```bash
git add tests/rt/test_dynamic_end_to_end.cpp
git commit -m "test: end-to-end dynamic rule pipeline (compile → engine → handler)"
```

---

## Completion Criteria

Plan 3 is complete when:

1. `xmake build mora mora_runtime mora_tests` succeeds on Linux, and the `mora_runtime` Windows cross-compile succeeds.
2. `xmake test` passes 100%.
3. `kRelations` contains at least the named `ref/*`, `player/*`, `world/*`, and `event/*` entries from Phase A.
4. The Plan 1 type checker's dormant tests (`MaintainOnNonRetractableFails`, `MaintainUsingEventFails`) are active and passing.
5. Running the CLI on a `.mora` file with `on` or `maintain` rules produces a patch file whose `DagBytecode` section round-trips through the deserializer.
6. The Linux-side end-to-end test (Task 16) shows event → engine → handler invocation works.
7. The Windows runtime DLL, when loaded by Skyrim, has SKSE hooks registered (Task 15) — verification here is manual (in-game) and can be flagged as follow-up if out of scope for a purely CI-runnable plan.

---

## Self-Review

Coverage check against the spec:

- **Section 3 (namespace inventory):** ref/, player/, world/, event/ represented by at least one relation each. The bridge `ref/base_form` is present.
- **Section 4 (runtime engine):** Delta model + DeltaQueue (Task 10). Operator DAG + sinks (Tasks 5–7, 11). StaticProbe with arrangement (Task 12). MaintainSink handle tracking (Task 11). Single-threaded, no timestamps. Save/load persistence explicitly deferred.
- **Section 5 (.mora.patch):** New `DagBytecode` section type (Task 7, 9).
- **Section 6 (metaprogramming):** `HandlerId` registry + static_assert validation continues to work (Phase A adds new entries consistently).

Scope:
- The DAG compiler (Task 8) supports only the simplest rule shapes — one event/state source + static probes + one or more sinks. No joins between two dynamic sources yet; that's out of scope for v1 of the engine.
- StaticProbe is a filter, not a widener (Task 12). Widening tuples during joins is deferred.
- No SKSE plugin enumeration for real `esp_digest` verification; that's flagged as follow-up.
- Windows-side behavior (real CommonLibSSE-NG handlers and real SKSE hook APIs) is specified but not exercised by Linux CI; verification there is manual.

Placeholder scan: no TBDs. Task 15's SKSE event binding uses a placeholder event type that must be matched to the actual CommonLibSSE-NG API during implementation — documented in the task.

Type consistency: `HandlerId`, `DagOpcode`, `DagNode`, `DagGraph`, `DagEngine`, `HandlerRegistry`, `EffectArgs`, `EffectHandle`, `Delta`, `DagRuntime`, `MappedPatchFile` used consistently across tasks.
