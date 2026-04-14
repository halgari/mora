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
    auto bytes = serialize_patch_table(entries, digest, {}, dag_bytes);

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
    auto bytes = serialize_patch_table(entries, digest, {}, /*dag*/ std::vector<uint8_t>{});
    emit::PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    const emit::SectionDirectoryEntry* dir =
        reinterpret_cast<const emit::SectionDirectoryEntry*>(bytes.data() + sizeof(h));
    for (uint32_t i = 0; i < h.section_count; ++i) {
        EXPECT_NE(dir[i].section_id, static_cast<uint32_t>(emit::SectionId::DagBytecode));
    }
}
