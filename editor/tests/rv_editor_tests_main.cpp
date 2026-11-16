// Runs every registered test; the exit code is non-zero when any CHECK failed, which is what ctest reads.

#include <string>

#include "rv_editor_test.hpp"
#include "text/rv_editor_text.hpp"

int main()
{
    // The editor texts are embedded; rv_editor_text returns the id itself until they are loaded.
    std::string error;
    if (rv_editor::rv_editor_text_load({}, error) != 0) {
        std::fprintf(stderr, "texts: %s\n", error.c_str());
        return 2;
    }
    for (const auto &t : rv_editor_test::rv_editor_test_all()) {
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
    }
    std::printf("%zu tests, %d failed checks\n",
        rv_editor_test::rv_editor_test_all().size(),
        rv_editor_test::rv_editor_test_failures());
    return rv_editor_test::rv_editor_test_failures() == 0 ? 0 : 1;
}
