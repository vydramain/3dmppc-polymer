#include "rv_manifest_failer.hpp"

#include <string>
#include <string_view>

namespace rv_pdklib
{

bool rv_manifest_failer::fail(int line, const std::string &message)
{
	if (!exhausted()) {
		entries_.push_back(entry{ line, message });
	}
	return false;
}

std::string rv_manifest_failer::report(std::string_view origin) const
{
	std::string out;
	for (const entry &e : entries_) {
		if (!out.empty()) {
			out += '\n';
		}
		if (origin.empty()) {
			out += "line " + std::to_string(e.line) + ": ";
		} else {
			out += std::string(origin) + ":" + std::to_string(e.line) + ": ";
		}
		out += e.message;
	}
	return out;
}

} // namespace rv_pdklib
