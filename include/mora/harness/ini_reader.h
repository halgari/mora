#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace mora::harness {

struct HarnessConfig {
    uint16_t port = 9742;
    std::string dump_path = "Data/MoraCache/dumps";
};

HarnessConfig read_ini(const std::filesystem::path& path);

} // namespace mora::harness
