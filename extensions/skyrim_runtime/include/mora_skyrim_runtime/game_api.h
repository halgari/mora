#pragma once

#include "mora/data/value.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mora_skyrim_runtime {

// Abstract dispatch interface. Each call applies one effect fact.
// The `field` string is the Mora keyword payload (e.g. "GoldValue",
// "Damage", "Keyword") — the concrete implementation maps it to the
// corresponding game API call.
class GameAPI {
public:
    virtual ~GameAPI() = default;
    virtual void set     (uint32_t target, std::string_view field, const mora::Value& v) = 0;
    virtual void add     (uint32_t target, std::string_view field, const mora::Value& v) = 0;
    virtual void remove  (uint32_t target, std::string_view field, const mora::Value& v) = 0;
    virtual void multiply(uint32_t target, std::string_view field, const mora::Value& v) = 0;
};

struct MockCall {
    std::string    op;      // "set" | "add" | "remove" | "multiply"
    uint32_t       target;
    std::string    field;
    mora::Value    value;
};

// Test double — records every dispatch into `calls` in call order.
class MockGameAPI : public GameAPI {
public:
    std::vector<MockCall> calls;
    void set     (uint32_t t, std::string_view f, const mora::Value& v) override;
    void add     (uint32_t t, std::string_view f, const mora::Value& v) override;
    void remove  (uint32_t t, std::string_view f, const mora::Value& v) override;
    void multiply(uint32_t t, std::string_view f, const mora::Value& v) override;
};

} // namespace mora_skyrim_runtime
