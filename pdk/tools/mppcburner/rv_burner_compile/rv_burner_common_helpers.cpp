#include "rv_burner_common_helpers.hpp"

#include <sys/wait.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <string>

std::string rv_pdktools::shell_quote(const std::string &text)
{
	std::string out = "'";
	for (const char c : text) {
		if (c == '\'') {
			out += "'\\''";
		} else {
			out += c;
		}
	}
	out += "'";
	return out;
}

int rv_pdktools::run_capture(const std::string &command, std::string &output)
{
	output.clear();
	const std::string full = command + " 2>&1";
	std::FILE *pipe = ::popen(full.c_str(), "r");
	if (pipe == nullptr) {
		return -1;
	}
	std::array<char, 4096> buffer{};
	while (true) {
		const std::size_t got = std::fread(buffer.data(), 1, buffer.size(), pipe);
		if (got == 0) {
			break;
		}
		output.append(buffer.data(), got);
	}
	const int status = ::pclose(pipe);
	if (status == -1) {
		return -1;
	}
	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	}
	return 128;
}

void rv_pdktools::dump_child_output(const std::string &output)
{
	if (output.empty()) {
		return;
	}
	std::fwrite(output.data(), 1, output.size(), stderr);
	if (output.back() != '\n') {
		std::fputc('\n', stderr);
	}
}
