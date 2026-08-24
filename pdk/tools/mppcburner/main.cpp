// mppcburner - burn a disc directory into one .mppcdisc, or inspect a burned one.
//
// A prepared directory goes in (sources, disc.toml, artwork) and a single
// .mppcdisc comes out. That image is the only thing the console loads.
//
// Streams: inspect writes its listing to stdout and nothing else, so it pipes.
// build writes every progress line to stderr, because its product is a file,
// not a stream. Any refusal exits non-zero.
//
// The command line is table driven: OPTIONS and COMMANDS below are the only
// place a subcommand or an option is declared.

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include <getopt.h>

#include "pdklib/rv_stdio.hpp"

#include "rv_build.hpp"
#include "rv_burner_options.hpp"
#include "rv_burner_print.hpp"
#include "rv_inspect.hpp"

namespace rv_pdktools
{
namespace
{

void rv_burner_print_usage(std::FILE *stream)
{
	rv_pdklib::rv_fprintf(stream,
		"mppcburner - burn a disc directory into a .mppcdisc image\n"
		"\n"
		"Usage:\n"
		"  mppcburner build <disc-directory> -o <output.mppcdisc> [options]\n"
		"  mppcburner inspect <file.mppcdisc>\n"
		"  mppcburner help\n"
		"\n"
		"Commands:\n"
		"  build     Compile the disc directory and burn the image. Progress goes\n"
		"            to stderr, nothing to stdout.\n"
		"  inspect   Print the manifest and the entry sizes to stdout. Nothing is\n"
		"            unpacked and nothing is written.\n"
		"  help      Print this text.\n"
		"\n"
		"Options (build):\n"
		"  -o, --output PATH        Image to write. Required.\n"
		"  -p, --pdk PATH           PDK include directory the disc compiles\n"
		"                           against. Default: " RV_BURNER_DEFAULT_PDK
		"\n"
		"  -l, --pdklib PATH        Disc-side SDK include directory.\n"
		"                           Default: " RV_BURNER_DEFAULT_PDKLIB
		"\n"
		"  -b, --baker PATH         mppcbaker used to bake textures. Default: the\n"
		"                           copy next to mppcburner, then $PATH.\n"
		"  -j, --jobs N             Parallel compile jobs. Default: cmake decides.\n"
		"  -k, --keep-build[=PATH]  Keep the generated CMake project instead of\n"
		"                           deleting it. PATH must be attached with '='.\n"
		"\n"
		"Options (any command):\n"
		"  -h, --help               Print this text.\n");
}

// Every option the tool has, declared once. Letters are unique tool-wide, which
// is what lets a single switch store the options of every subcommand.
constexpr rv_burner_option_spec OPTIONS[] = {
	{ 'h', "help", no_argument, "print this text",
		RV_BURNER_MASK_BUILD | RV_BURNER_MASK_INSPECT },
	{ 'o', "output", required_argument, "image to write (required)",
		RV_BURNER_MASK_BUILD },
	{ 'p', "pdk", required_argument,
		"PDK include directory the disc compiles against",
		RV_BURNER_MASK_BUILD },
	{ 'l', "pdklib", required_argument, "disc-side SDK include directory",
		RV_BURNER_MASK_BUILD },
	{ 'b', "baker", required_argument, "mppcbaker used to bake textures",
		RV_BURNER_MASK_BUILD },
	{ 'j', "jobs", required_argument, "parallel compile jobs",
		RV_BURNER_MASK_BUILD },
	{ 'k', "keep-build", optional_argument,
		"keep the generated CMake project, optionally at PATH",
		RV_BURNER_MASK_BUILD },
};

int nullable_handler(const rv_burner_options &)
{
	rv_pdktools::rv_burner_print_usage(stderr);
	return -1;
}

// Every subcommand, plus the three spellings of help. A new command is a row
// here, never a branch in main.
constexpr rv_burner_command_spec COMMANDS[] = {
	{ "build", RV_BURNER_MASK_BUILD, 1, rv_burn_run },
	{ "inspect", RV_BURNER_MASK_INSPECT, 1, rv_inspect_run },
	{ "help", 0, 0, nullable_handler },
	{ "-h", 0, 0, nullable_handler },
	{ "--help", 0, 0, nullable_handler },
};

} // namespace
} // namespace rv_pdktools

int main(int argc, char **argv)
{
	if (argc < 2) {
		rv_pdktools::rv_burner_print_usage(stderr);
		return 1;
	}

	const std::string_view command_name = argv[1];

	const rv_pdktools::rv_burner_command_spec *cmd = nullptr;
	for (const rv_pdktools::rv_burner_command_spec &command_intern : rv_pdktools::COMMANDS) {
		if (command_intern.name == command_name) {
			cmd = &command_intern;
			break;
		}
	}

	if (nullptr == cmd) {
		rv_pdktools::rv_burner_print_error("unknown subcommand '" + std::string(command_name) + "'");
		rv_pdktools::rv_burner_print_usage(stderr);
		return 1;
	}

	std::vector<struct option> opts;
	std::string opts_string;

	for (const struct rv_pdktools::rv_burner_option_spec &option_intern : rv_pdktools::OPTIONS) {
		if (0 == (option_intern.mask & cmd->mask)) {
			continue;
		}

		opts.push_back(option{
			option_intern.name,
			option_intern.arity,
			0,
			option_intern.letter,
		});

		opts_string.push_back(option_intern.letter);

		if (required_argument == option_intern.arity) {
			opts_string.push_back(':');
		}
		if (optional_argument == option_intern.arity) {
			opts_string.append("::");
		}
	}
	opts.push_back(option{ 0, 0, 0, 0 });

	rv_pdktools::rv_burner_options burner_options{};

	// Input args starts from second array member. So we need to skip command name first to check options
	const int local_argc = argc - 1;
	char **local_argv = argv + 1;
	for (int c;;) {
		c = getopt_long(local_argc, local_argv, opts_string.c_str(), opts.data(), nullptr);

		if (c < 0) {
			break;
		}

		switch (c) {
		case 'h': {
			rv_pdktools::rv_burner_print_usage(stdout);
			return 0;
		}
		case 'o': {
			burner_options.output = optarg;
			break;
		}
		case 'p': {
			burner_options.pdk_dir = optarg;
			break;
		}
		case 'l': {
			burner_options.pdklib_dir = optarg;
			break;
		}
		case 'b': {
			burner_options.baker = optarg;
			break;
		}
		case 'j': {
			char *end = nullptr;
			const long value = std::strtol(optarg, &end, 10);
			if (*optarg == '\0' || *end != '\0' || value <= 0 || value > INT_MAX) {
				rv_pdktools::rv_burner_print_error("invalid --jobs value '" + std::string(optarg) + "'");
				return 1;
			}
			burner_options.jobs = static_cast<int>(value);
			break;
		}
		case 'k': {
			burner_options.keep_build = true;

			if (nullptr != optarg) {
				burner_options.build_dir = optarg;
			}
			break;
		}
		case '?':
		default: {
			// No message here: getopt already printed one (opterr defaults to 1).
			rv_pdktools::rv_burner_print_usage(stderr);
			return 1;
		}
		}
	}

	// Exactly the declared number of operands: getopt left them from optind on.
	if (cmd->operands != local_argc - optind) {
		rv_pdktools::rv_burner_print_error(std::string(command_name) + " expects " + std::to_string(cmd->operands) + " operand(s), got " + std::to_string(local_argc - optind));
		rv_pdktools::rv_burner_print_usage(stderr);
		return 1;
	}

	// Nothing to read for an operandless command: argv[optind] is its nullptr.
	if (0 != local_argc - optind) {
		burner_options.operand = local_argv[optind];
	}

	return cmd->handler(burner_options);
}
