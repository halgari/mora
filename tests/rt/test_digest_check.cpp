#include "mora/rt/mapped_patch_file.h"
#include "mora/emit/flat_file_writer.h"
#include "mora/emit/patch_file_v2.h"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

using namespace mora;

static std::filesystem::path write_temp(const std::vector<uint8_t>& bytes) {
    auto p = std::filesystem::temp_directory_path()
           / ("mora_dig_" + std::to_string(std::rand()) + ".bin");
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return p;
}

TEST(DigestCheck, HeaderRoundTripsDigest) {
    std::array<uint8_t, 32> d{};
    d[0] = 0x42;
    emit::FlatFileWriter w;
    w.set_esp_digest(d);
    auto bytes = w.finish();
    auto path = write_temp(bytes);

    rt::MappedPatchFile mpf;
    ASSERT_TRUE(mpf.open(path.string()));
    EXPECT_EQ(mpf.header().esp_digest, d);

    std::filesystem::remove(path);
}

TEST(DigestCheck, HelperDetectsMismatch) {
    GTEST_SKIP() << "digest-verify helper pending SKSE-side plugin enumeration";
}
