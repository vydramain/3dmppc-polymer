#pragma once

#include <cstdarg>
#include <cstdio>

namespace rv_pdklib
{

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif

inline int rv_fprintf(std::FILE *stream, const char *format, ...)
{
#if defined(__EMSCRIPTEN__)
	// TODO: Browser interpretation
	return -1;
#elif defined(_WIN32)
	// TODO: Windows interpretation
	return -1;
#elif defined(__unix__) || defined(__APPLE__)
	std::va_list _va_list;
	va_start(_va_list, format);
	int res = std::vfprintf(stream, format, _va_list);
	va_end(_va_list);
	return res;
#else
#error "rv_stdio.hpp: unsupported platform, add a branch to rv_fprintf"
#endif
}

} // namespace rv_pdklib
