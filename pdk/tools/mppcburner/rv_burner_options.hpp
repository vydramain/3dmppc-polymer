#pragma once

#include <string>
#include <cstdint>

namespace rv_pdktools
{

// Command line as parsed. Every subcommand fills the same struct and reads only
// the fields its own options can set.
struct rv_burner_options {
    std::string operand;                               // the one non-option word: disc directory
                                                       // for build, .mppcdisc file for inspect
    std::string bake_source;                           // bake-texture's second operand: the
                                                       // source file to bake, relative to operand
    std::string output;                                // -o
    std::string unpacked;                              // -u, an unpacked disc directory instead
                                                       // of a .mppcdisc; exclusive with -o
    // -p, -l: include directories a generated disc project compiles against. Empty
    // means not given; main fills it with the copy beside mppcburner.
    std::string pdk_dir = "";                          // -p
    std::string pdklib_dir = "";                       // -l
    std::string baker = "";                            // -b, empty: find the bakers next to mppcburner, then $PATH
    std::string build_dir = "";                        // -k=PATH, empty: <operand>/.mppcburn
    int jobs = 0;                                      // -j, 0: let cmake decide
    bool keep_build = false;                           // -k
    std::string map = "";                              // -m, where to write the source-to-entry map
    std::string target = "";                           // -t, empty: disc
    std::string emsdk = "";                            // -e, empty: the directory setup web installs
};

// One bit per subcommand. Lets an option name the commands that accept it.
enum rv_burner_command_mask : uint32_t {
    RV_BURNER_MASK_BUILD = 1u << 0,
    RV_BURNER_MASK_INSPECT = 1u << 1,
};

// One row of the option table. `arity` is plain int because it carries getopt's
// no_argument / required_argument / optional_argument, which are macros, not
// enumerators, and is copied into struct option verbatim.
struct rv_burner_option_spec {
    char letter;
    const char *name;
    int arity;
    const char *help;
    uint32_t mask;
};

// What a subcommand does once its options and operand are parsed.
using rv_burner_handler = int (*)(const rv_burner_options &options);

// One row of the command table. `operands` is the exact number of non-option
// words the command takes, not a minimum.
struct rv_burner_command_spec {
    const char *name;
    uint32_t mask;
    int operands;
    rv_burner_handler handler;
};

} // namespace rv_pdktools
