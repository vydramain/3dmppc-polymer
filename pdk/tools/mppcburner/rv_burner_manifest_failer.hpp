#pragma once

#include <string>
namespace rv_pdktools
{

namespace rv_burner_manifest_failer
{

inline bool fail(int line, const std::string &message, std::string &error)
{
	if (error.empty()) {
		error = "line " + std::to_string(line) + ": " + message;
	}
	return false;
};

} // namespace rv_burner_manifest_failer

} // namespace rv_pdktools
