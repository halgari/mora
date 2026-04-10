#pragma once
#include "mora/cli/terminal.h"
#include <chrono>
#include <string>
#include <cstddef>

namespace mora {

class Progress {
public:
    explicit Progress(bool use_color = true, bool is_tty = true);

    void print_header(const std::string& version);
    void start_phase(const std::string& name);
    void finish_phase(const std::string& detail, const std::string& timing);
    void print_success(const std::string& message);
    void print_failure(const std::string& message);
    void print_summary(size_t frozen_rules, size_t frozen_patches,
                       size_t dynamic_rules, size_t conflicts,
                       size_t errors, size_t warnings,
                       const std::string& spatch_size);

private:
    bool color_;
    bool is_tty_;
    std::chrono::steady_clock::time_point phase_start_;
};

} // namespace mora
