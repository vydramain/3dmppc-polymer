// mppcmusbaker: bakes a WAV or an MP3 into the headerless S16LE mono the console plays.
#include <getopt.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/de/rv_dv.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_stdio/rv_stdio.hpp"
#include "rv_musbaker_mp3.hpp"
#include "rv_musbaker_wav.hpp"

namespace
{

// Exit codes: 1 is a job that could not be done, 2 a command line
// not understood.
constexpr int RV_MUSBAKER_EXIT_SUCCESS = 0;
constexpr int RV_MUSBAKER_EXIT_FAILURE = 1;
constexpr int RV_MUSBAKER_EXIT_USAGE = 2;

// Prefix of every message. Fixed rather than the invoked name, so output does not
// depend on the name the binary was copied under.
constexpr const char *RV_MUSBAKER_PROGNAME = "mppcmusbaker";

// Argc for `mppcmusbaker --version`: program name plus the one flag.
constexpr int RV_MUSBAKER_ARGC_VERSION_CHECK = 2;

// Positional arguments required after option processing: input and output paths.
constexpr int RV_MUSBAKER_POSITIONAL_ARGS_REQUIRED = 2;

// What a failed step hands back to the CLI. `show_usage` is set by argument
// parsing only; an empty message means the failure has already explained itself
// on stderr, which is how getopt reports an unknown flag.
struct musbaker_error {
    std::string message;
    bool show_usage = false;
};

struct musbaker_args {
    std::string input;
    std::string output;
    bool help = false;
};

void print_usage(std::FILE *out)
{
    rv_pdklib::rv_fprintf(out,
        "usage: mppcmusbaker <input.wav|input.mp3> <output.pcm>\n"
        "\n"
        "  A .wav is baked to headerless S16LE mono at 44100 Hz, the shape the console\n"
        "  plays. It must already be PCM 16-bit or 24-bit 44100 Hz, mono or stereo (stereo is\n"
        "  averaged to mono).\n"
        "\n"
        "  A .mp3 is decoded the same way; it must be 44100 Hz (it is not resampled).\n"
        "\n"
        "mppcmusbaker --version prints one line, `mppcmusbaker <major>.<minor>`.\n");
}

// The ONE place a failure becomes something the user sees and a code the shell
// gets. An empty message means the failure has already explained itself.
int report(const musbaker_error &error)
{
    if (error.show_usage) {
        print_usage(stderr);
    }
    if (!error.message.empty()) {
        rv_pdklib::rv_fprintf(stderr, "%s: %s\n", RV_MUSBAKER_PROGNAME, error.message.c_str());
    }
    return error.message.empty() ? RV_MUSBAKER_EXIT_USAGE : RV_MUSBAKER_EXIT_FAILURE;
}

// Fills `out` from argv. --help is not a failure: it sets out->help and succeeds.
int parse_args(int argc, char **argv, musbaker_args *out, musbaker_error *error)
{
    static struct option long_opts[] = { { "help", no_argument, 0, 'h' }, { 0, 0, 0, 0 } };

    int c;
    while ((c = getopt_long(argc, argv, "h", long_opts, nullptr)) != -1) {
        switch (c) {
        case 'h':
            out->help = true;
            return RV_OK;
        case '?':
            // getopt has already named the offending option on stderr.
            error->show_usage = true;
            return RV_ERR_INVAL;
        }
    }

    if (argc - optind != RV_MUSBAKER_POSITIONAL_ARGS_REQUIRED) {
        error->message = "expected exactly one input and one output path";
        error->show_usage = true;
        return RV_ERR_INVAL;
    }
    out->input = argv[optind];
    out->output = argv[optind + 1];
    return RV_OK;
}

// One open, one write, one close. The message says what was being attempted.
int write_file(const std::string &path, const std::vector<uint8_t> &bytes, musbaker_error *error)
{
    std::FILE *out = std::fopen(path.c_str(), "wb");
    if (out == nullptr) {
        error->message = "cannot open '" + path + "' for writing";
        return RV_ERR_IO;
    }
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), out);
    const bool closed = std::fclose(out) == 0;
    if (written != bytes.size() || !closed) {
        error->message = "failed to write '" + path + "' (disk full?)";
        return RV_ERR_IO;
    }
    return RV_OK;
}

// The reader follows the extension, case-insensitively. Anything else is read as
// WAVE, which refuses what it does not understand.
bool has_mp3_extension(const std::string &path)
{
    constexpr std::string_view RV_MP3_EXTENSION = ".mp3";
    if (path.size() < RV_MP3_EXTENSION.size()) {
        return false;
    }
    const std::string_view tail = std::string_view(path).substr(path.size() - RV_MP3_EXTENSION.size());
    return std::ranges::equal(tail, RV_MP3_EXTENSION, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == b;
    });
}

int run(int argc, char **argv)
{
    musbaker_args args;
    musbaker_error error;

    if (parse_args(argc, argv, &args, &error) != RV_OK) {
        return report(error);
    }
    if (args.help) {
        print_usage(stdout);
        return RV_MUSBAKER_EXIT_SUCCESS;
    }

    std::vector<uint8_t> pcm;
    std::string load_error;
    const bool is_mp3 = has_mp3_extension(args.input);
    const rv_err loaded = is_mp3 ? load_mp3_pcm(args.input, &pcm, &load_error) : load_wav_pcm(args.input, &pcm, &load_error);
    if (loaded != RV_OK) {
        error.message = load_error;
        return report(error);
    }
    if (write_file(args.output, pcm, &error) != RV_OK) {
        return report(error);
    }

    std::printf("%s: %s -> %s (%zu samples, %zu bytes)\n",
        RV_MUSBAKER_PROGNAME,
        args.input.c_str(),
        args.output.c_str(),
        pcm.size() / sizeof(int16_t),
        pcm.size());
    return RV_MUSBAKER_EXIT_SUCCESS;
}

} // namespace

int main(int argc, char **argv)
{
    // One line a front end can show and compare.
    if (argc == RV_MUSBAKER_ARGC_VERSION_CHECK && std::string_view(argv[1]) == "--version") {
        rv_pdklib::rv_fprintf(stdout, "%s %d.%d\n", RV_MUSBAKER_PROGNAME, RV_MPPC_VER_MAJOR, RV_MPPC_VER_MINOR);
        return RV_MUSBAKER_EXIT_SUCCESS;
    }
    return run(argc, argv);
}
