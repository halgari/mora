//
// Unit tests for mora::harness::read_npc_fields.
//
// Verifies every scalar + form-ref field added to NpcData. Collections
// (spells/perks/factions/shouts) route through CommonLibSSE typed
// access in the real runtime; on Linux their vectors stay empty and
// integration tests (Part 3) validate end-to-end.

#include <gtest/gtest.h>
#include "mora/harness/npc_dumper.h"
#include <cstring>
#include <sstream>

namespace {

// Layout matches include/mora/data/form_model.h kNpcSlots + kNpcDirectMembers.
// Total footprint — 0x260 bytes is enough room for outfitForm @ 0x218.
struct FakeNpcInit {
    uint32_t formid          = 0;
    uint32_t acbs_flags      = 0;
    uint16_t level           = 0;
    uint16_t calc_level_min  = 0;
    uint16_t calc_level_max  = 0;
    uint16_t speed_mult      = 0;

    // Form pointers (we build fake TESForm-like 32-byte stubs in-buffer
    // and stash their absolute addresses at the right offsets).
    uint32_t race_formid      = 0;
    uint32_t class_formid     = 0;
    uint32_t voice_type_formid = 0;
    uint32_t skin_formid      = 0;
    uint32_t outfit_formid    = 0;
};

// Single-shot fake NPC builder. Returns a buffer + stores form stubs at
// the tail so their pointer values can be embedded back into the header.
// NOTE: the returned vector owns the memory; the embedded absolute pointers
// remain valid only while the vector is alive and unresized.
static std::vector<uint8_t> make_fake_npc(const FakeNpcInit& init) {
    std::vector<uint8_t> buf(0x300, 0);

    // TESForm.formID + formType
    std::memcpy(buf.data() + 0x14, &init.formid, sizeof(init.formid));
    buf[0x1A] = 0x2B;  // TESNPC formType

    // ACBS packed stats
    std::memcpy(buf.data() + 0x038, &init.acbs_flags,     sizeof(init.acbs_flags));
    std::memcpy(buf.data() + 0x040, &init.level,          sizeof(init.level));
    std::memcpy(buf.data() + 0x042, &init.calc_level_min, sizeof(init.calc_level_min));
    std::memcpy(buf.data() + 0x044, &init.calc_level_max, sizeof(init.calc_level_max));
    std::memcpy(buf.data() + 0x046, &init.speed_mult,     sizeof(init.speed_mult));

    // Allocate 5 × 32-byte fake form stubs past the NPC footprint; each
    // stub stores its FormID at +0x14.
    auto write_stub = [&](size_t stub_offset, uint32_t fid) -> void* {
        if (fid == 0) return nullptr;
        uint8_t* stub = buf.data() + stub_offset;
        std::memcpy(stub + 0x14, &fid, sizeof(fid));
        return stub;
    };

    void* race_ptr   = write_stub(0x220, init.race_formid);
    void* class_ptr  = write_stub(0x240, init.class_formid);
    void* voice_ptr  = write_stub(0x260, init.voice_type_formid);
    void* skin_ptr   = write_stub(0x280, init.skin_formid);
    void* outfit_ptr = write_stub(0x2A0, init.outfit_formid);
    // Reserve room — buffer must be long enough to hold the stubs.
    if (buf.size() < 0x2C0) buf.resize(0x2C0, 0);

    // Write pointer values at the expected absolute offsets.
    std::memcpy(buf.data() + 0x158, &race_ptr,   sizeof(race_ptr));
    std::memcpy(buf.data() + 0x1C0, &class_ptr,  sizeof(class_ptr));
    std::memcpy(buf.data() + 0x058, &voice_ptr,  sizeof(voice_ptr));
    std::memcpy(buf.data() + 0x108, &skin_ptr,   sizeof(skin_ptr));
    std::memcpy(buf.data() + 0x218, &outfit_ptr, sizeof(outfit_ptr));

    return buf;
}

TEST(NpcDumperTest, ScalarsAndFlags) {
    auto buf = make_fake_npc(FakeNpcInit{
        .formid          = 0x00013BBF,
        // essential(bit 1) + auto_calc_stats(bit 4) set, protected(bit 11) clear.
        .acbs_flags      = (1u << 1) | (1u << 4),
        .level           = 7,
        .calc_level_min  = 5,
        .calc_level_max  = 10,
        .speed_mult      = 100,
    });

    mora::harness::NpcData data;
    mora::harness::read_npc_fields(buf.data(), data);

    EXPECT_EQ(data.formid,         0x00013BBFu);
    EXPECT_EQ(data.level,          7);
    EXPECT_EQ(data.calc_level_min, 5);
    EXPECT_EQ(data.calc_level_max, 10);
    EXPECT_EQ(data.speed_mult,     100);

    EXPECT_TRUE (data.essential);
    EXPECT_TRUE (data.auto_calc_stats);
    EXPECT_FALSE(data.protected_flag);
}

TEST(NpcDumperTest, FormReferences) {
    auto buf = make_fake_npc(FakeNpcInit{
        .formid            = 0x00013BBF,
        .race_formid       = 0x0008884D,
        .class_formid      = 0x00013CBD,
        .voice_type_formid = 0x0001327F,
        .skin_formid       = 0x00013EFF,
        .outfit_formid     = 0x0001BA9F,
    });

    mora::harness::NpcData data;
    mora::harness::read_npc_fields(buf.data(), data);

    EXPECT_EQ(data.race_formid,           0x0008884Du);
    EXPECT_EQ(data.class_formid,          0x00013CBDu);
    EXPECT_EQ(data.voice_type_formid,     0x0001327Fu);
    EXPECT_EQ(data.skin_formid,           0x00013EFFu);
    EXPECT_EQ(data.default_outfit_formid, 0x0001BA9Fu);
}

TEST(NpcDumperTest, NullFormRefsYieldZero) {
    auto buf = make_fake_npc(FakeNpcInit{.formid = 0x1});
    mora::harness::NpcData data;
    mora::harness::read_npc_fields(buf.data(), data);

    EXPECT_EQ(data.race_formid,           0u);
    EXPECT_EQ(data.class_formid,          0u);
    EXPECT_EQ(data.voice_type_formid,     0u);
    EXPECT_EQ(data.skin_formid,           0u);
    EXPECT_EQ(data.default_outfit_formid, 0u);
}

TEST(NpcDumperTest, JsonlContainsAllFields) {
    mora::harness::NpcData d;
    d.formid = 0x00013BBF;
    d.name   = "Nazeem";
    d.level  = 7;
    d.calc_level_min = 5;
    d.calc_level_max = 10;
    d.speed_mult     = 100;
    d.essential      = true;
    d.protected_flag = false;
    d.auto_calc_stats = true;
    d.race_formid           = 0x0008884D;
    d.class_formid          = 0x00013CBD;
    d.voice_type_formid     = 0x0001327F;
    d.skin_formid           = 0x00013EFF;
    d.default_outfit_formid = 0x0001BA9F;
    d.keyword_formids = {0x0013794};

    auto s = mora::harness::npc_to_jsonl(d);
    EXPECT_NE(s.find("\"formid\":\"0x00013BBF\""),      std::string::npos);
    EXPECT_NE(s.find("\"name\":\"Nazeem\""),            std::string::npos);
    EXPECT_NE(s.find("\"level\":7"),                    std::string::npos);
    EXPECT_NE(s.find("\"calc_level_min\":5"),           std::string::npos);
    EXPECT_NE(s.find("\"calc_level_max\":10"),          std::string::npos);
    EXPECT_NE(s.find("\"speed_mult\":100"),             std::string::npos);
    EXPECT_NE(s.find("\"essential\":true"),             std::string::npos);
    EXPECT_NE(s.find("\"protected\":false"),            std::string::npos);
    EXPECT_NE(s.find("\"auto_calc_stats\":true"),       std::string::npos);
    EXPECT_NE(s.find("\"race\":\"0x0008884D\""),        std::string::npos);
    EXPECT_NE(s.find("\"class\":\"0x00013CBD\""),       std::string::npos);
    EXPECT_NE(s.find("\"voice_type\":\"0x0001327F\""),  std::string::npos);
    EXPECT_NE(s.find("\"skin\":\"0x00013EFF\""),        std::string::npos);
    EXPECT_NE(s.find("\"default_outfit\":\"0x0001BA9F\""), std::string::npos);
    EXPECT_NE(s.find("\"keywords\":[\"0x00013794\"]"),  std::string::npos);
    EXPECT_NE(s.find("\"spells\":[]"),                  std::string::npos);
    EXPECT_NE(s.find("\"perks\":[]"),                   std::string::npos);
    EXPECT_NE(s.find("\"factions\":[]"),                std::string::npos);
    EXPECT_NE(s.find("\"shouts\":[]"),                  std::string::npos);
}

TEST(NpcDumperTest, SortedByFormId) {
    std::vector<mora::harness::NpcData> npcs;
    mora::harness::NpcData a; a.formid = 0x200; npcs.push_back(a);
    mora::harness::NpcData b; b.formid = 0x100; npcs.push_back(b);

    std::stringstream ss;
    mora::harness::write_npcs_jsonl(npcs, ss);
    std::string line1, line2;
    std::getline(ss, line1);
    std::getline(ss, line2);
    EXPECT_NE(line1.find("\"formid\":\"0x00000100\""), std::string::npos);
    EXPECT_NE(line2.find("\"formid\":\"0x00000200\""), std::string::npos);
}

} // namespace
