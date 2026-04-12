#include <gtest/gtest.h>
#include "mora/eval/patch_buffer.h"
#include "mora/eval/patch_set.h"  // for FieldOp

using namespace mora;

// ---------------------------------------------------------------------------
// 1. EmitAndSort — emit entries in random order, sort, verify sorted by formid
// ---------------------------------------------------------------------------
TEST(PatchBufferTest, EmitAndSort) {
    PatchBuffer buf;
    buf.emit(300, 1, 1, 0, 0xAAA);
    buf.emit(100, 1, 1, 0, 0xBBB);
    buf.emit(200, 1, 1, 0, 0xCCC);

    buf.sort_and_dedup();

    ASSERT_EQ(buf.size(), 3u);
    EXPECT_EQ(buf.entries()[0].formid, 100u);
    EXPECT_EQ(buf.entries()[1].formid, 200u);
    EXPECT_EQ(buf.entries()[2].formid, 300u);
}

// ---------------------------------------------------------------------------
// 2. DedupAddOps — same (formid, field, Add, value) emitted twice -> deduped
// ---------------------------------------------------------------------------
TEST(PatchBufferTest, DedupAddOps) {
    PatchBuffer buf;
    uint8_t add_op = static_cast<uint8_t>(FieldOp::Add);
    buf.emit(100, 6, add_op, 0, 0xBEEF);
    buf.emit(100, 6, add_op, 0, 0xBEEF);  // duplicate

    buf.sort_and_dedup();

    ASSERT_EQ(buf.size(), 1u);
    EXPECT_EQ(buf.entries()[0].formid, 100u);
    EXPECT_EQ(buf.entries()[0].value, 0xBEEFu);
}

// ---------------------------------------------------------------------------
// 3. DedupSetOps — two Set ops on same (formid, field) -> keep last
// ---------------------------------------------------------------------------
TEST(PatchBufferTest, DedupSetOps) {
    PatchBuffer buf;
    uint8_t set_op = static_cast<uint8_t>(FieldOp::Set);
    buf.emit(100, 2, set_op, 0, 10);  // first Set
    buf.emit(100, 2, set_op, 0, 20);  // second Set (should win)

    buf.sort_and_dedup();

    ASSERT_EQ(buf.size(), 1u);
    EXPECT_EQ(buf.entries()[0].value, 20u);
}

// ---------------------------------------------------------------------------
// 4. MixedOps — Add and Set on same field -> both kept (different ops)
// ---------------------------------------------------------------------------
TEST(PatchBufferTest, MixedOps) {
    PatchBuffer buf;
    uint8_t set_op = static_cast<uint8_t>(FieldOp::Set);
    uint8_t add_op = static_cast<uint8_t>(FieldOp::Add);
    buf.emit(100, 6, set_op, 0, 42);
    buf.emit(100, 6, add_op, 0, 99);

    buf.sort_and_dedup();

    ASSERT_EQ(buf.size(), 2u);
    // Sorted by op: Set=0 comes before Add=1
    EXPECT_EQ(buf.entries()[0].op, set_op);
    EXPECT_EQ(buf.entries()[1].op, add_op);
}

// ---------------------------------------------------------------------------
// 5. LargeBuffer — emit 100K entries, sort_and_dedup, verify performance
// ---------------------------------------------------------------------------
TEST(PatchBufferTest, LargeBuffer) {
    PatchBuffer buf;
    buf.reserve(100000);

    uint8_t add_op = static_cast<uint8_t>(FieldOp::Add);
    for (uint32_t i = 0; i < 100000; i++) {
        // Create some duplicates: formid cycles through 10000 values
        buf.emit(i % 10000, 6, add_op, 0, i % 500);
    }

    buf.sort_and_dedup();

    // Should be significantly reduced due to dedup
    EXPECT_LT(buf.size(), 100000u);
    EXPECT_GT(buf.size(), 0u);

    // Verify sorted order
    for (size_t i = 1; i < buf.size(); i++) {
        const auto& a = buf.entries()[i - 1];
        const auto& b = buf.entries()[i];
        bool order_ok = (a.formid < b.formid) ||
                        (a.formid == b.formid && a.field_id < b.field_id) ||
                        (a.formid == b.formid && a.field_id == b.field_id && a.op < b.op) ||
                        (a.formid == b.formid && a.field_id == b.field_id && a.op == b.op && a.value <= b.value);
        EXPECT_TRUE(order_ok) << "Entry " << i << " is out of order";
    }
}

// ---------------------------------------------------------------------------
// 6. Empty — sort_and_dedup on empty buffer, no crash
// ---------------------------------------------------------------------------
TEST(PatchBufferTest, Empty) {
    PatchBuffer buf;
    buf.sort_and_dedup();
    EXPECT_EQ(buf.size(), 0u);
}

// ---------------------------------------------------------------------------
// 7. SingleEntry — sort_and_dedup on single entry
// ---------------------------------------------------------------------------
TEST(PatchBufferTest, SingleEntry) {
    PatchBuffer buf;
    buf.emit(42, 1, 0, 0, 99);
    buf.sort_and_dedup();
    ASSERT_EQ(buf.size(), 1u);
    EXPECT_EQ(buf.entries()[0].formid, 42u);
}

// ---------------------------------------------------------------------------
// 8. AddDistinctValues — Add ops with distinct values on same (formid, field)
//    should all be kept
// ---------------------------------------------------------------------------
TEST(PatchBufferTest, AddDistinctValues) {
    PatchBuffer buf;
    uint8_t add_op = static_cast<uint8_t>(FieldOp::Add);
    buf.emit(100, 6, add_op, 0, 0xAAA);
    buf.emit(100, 6, add_op, 0, 0xBBB);
    buf.emit(100, 6, add_op, 0, 0xCCC);

    buf.sort_and_dedup();

    ASSERT_EQ(buf.size(), 3u);
}
