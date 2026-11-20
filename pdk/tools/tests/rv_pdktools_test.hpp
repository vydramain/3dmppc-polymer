// The PDK tools' own minimal test harness, the same shape as editor/tests/rv_editor_test.hpp: no dependency,
// a test is a function registered at static initialisation, CHECK counts a failure and keeps going.

#pragma once

#include <cstdio>
#include <vector>

namespace rv_pdktools_test
{

struct rv_pdktools_test_case {
    const char *name;
    void (*fn)();
};

// A function-local static: registration runs during static init of many files, in no fixed order.
inline std::vector<rv_pdktools_test_case> &rv_pdktools_test_all()
{
    static std::vector<rv_pdktools_test_case> all;
    return all;
}

inline int &rv_pdktools_test_failures()
{
    static int failures = 0;
    return failures;
}

struct rv_pdktools_test_register {
    rv_pdktools_test_register(const char *name, void (*fn)())
    {
        rv_pdktools_test_all().push_back({ name, fn });
    }
};

} // namespace rv_pdktools_test

#define RV_PDKTOOLS_TEST(name)                                                         \
    static void name();                                                                \
    static rv_pdktools_test::rv_pdktools_test_register name##_registered(#name, name); \
    static void name()

#define RV_PDKTOOLS_CHECK(cond)                                                           \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++rv_pdktools_test::rv_pdktools_test_failures();                              \
        }                                                                                 \
    } while (0)
