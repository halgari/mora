#include "mora/harness/ini_reader.h"
#include <fstream>
#include <string>

namespace mora::harness {

HarnessConfig read_ini(const std::filesystem::path& path) {
    HarnessConfig config;

    std::ifstream file(path);
    if (!file.is_open()) return config;

    std::string line;
    while (std::getline(file, line)) {
        auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line[0] == ';' || line[0] == '#' || line[0] == '[') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        auto key = line.substr(0, eq);
        auto value = line.substr(eq + 1);

        auto end = value.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) value = value.substr(0, end + 1);

        if (key == "port") {
            config.port = static_cast<uint16_t>(std::stoi(value));
        } else if (key == "dump_path") {
            config.dump_path = value;
        }
    }

    return config;
}

} // namespace mora::harness
