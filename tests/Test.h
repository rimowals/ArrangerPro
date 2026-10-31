#pragma once

// Minimal dependency-free test harness.

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    std::function<void()> body;
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

inline int& failures()
{
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

template <typename A, typename B>
void checkEqual(const A& actual, const B& expected, const char* expr, const char* file, int line)
{
    if (!(actual == expected))
    {
        std::ostringstream msg;
        msg << file << "(" << line << "): CHECK_EQ(" << expr << ") failed: got " << actual << ", expected " << expected;
        std::cerr << msg.str() << "\n";
        ++failures();
    }
}

} // namespace test

#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)
#define TEST_CASE(name)                                                                 \
    static void TEST_CONCAT(test_fn_, __LINE__)();                                      \
    static test::Registrar TEST_CONCAT(test_reg_, __LINE__)(name, TEST_CONCAT(test_fn_, __LINE__)); \
    static void TEST_CONCAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::cerr << __FILE__ << "(" << __LINE__ << "): CHECK(" #cond ") failed\n"; \
            ++test::failures();                                                         \
        }                                                                               \
    } while (0)

#define CHECK_EQ(actual, expected) test::checkEqual((actual), (expected), #actual ", " #expected, __FILE__, __LINE__)
