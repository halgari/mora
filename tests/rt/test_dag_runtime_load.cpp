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
    EXPECT_NE(dr.engine(), nullptr);

    std::filesystem::remove(path);
}

TEST(DagRuntimeLoad, SucceedsOnFileWithoutDagSection) {
    // No DAG section in file — init should still succeed with empty graph.
    std::vector<PatchEntry> entries = { {0x1u, 0, 0, 1, 0, 0u} };
    std::array<uint8_t, 32> digest{};
    auto bytes = serialize_patch_table(entries, digest, /*arrangements*/ {},
                                       /*dag*/ std::vector<uint8_t>{});

    auto path = std::filesystem::temp_directory_path()
              / ("mora_dr_empty_" + std::to_string(std::rand()) + ".bin");
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    f.close();

    rt::MappedPatchFile mpf;
    ASSERT_TRUE(mpf.open(path.string()));
    rt::DagRuntime dr;
    ASSERT_TRUE(dr.init_from(mpf));
    EXPECT_EQ(dr.dag().node_count(), 0u);

    std::filesystem::remove(path);
}
