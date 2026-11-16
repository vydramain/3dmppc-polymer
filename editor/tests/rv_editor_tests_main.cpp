// Runs every registered test; the exit code is non-zero when any CHECK failed, which is what ctest reads.

#include "rv_editor_test.hpp"

int main()
{
    for (const auto &t : rv_editor_test::rv_editor_test_all()) {
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
    }
    std::printf("%zu tests, %d failed checks\n", rv_editor_test::rv_editor_test_all().size(),
        rv_editor_test::rv_editor_test_failures());
    return rv_editor_test::rv_editor_test_failures() == 0 ? 0 : 1;
}
