#pragma once

#include <cstdarg>
#include <cstdio>

namespace rv_pdktool
{

// rv_fprintf(stream, format, ...)
//              |
//              | определить FILE *
//              |
//              | создать va_list
//              |
//              | начать чтение аргументов после format
//              |
//              | передать FILE *, format, va_list
//              | в printf-подобную функцию
//              |
//              | закончить работу с va_list
//              |
//              v
//           return result
//
inline int
rv_fprintf(FILE *__restrict stream, const char *__restrict format, ...)
{
	va_list _va_list;

	return vfprintf(stream, format, __va_list_tag * arg);
};

} // namespace rv_pdktool
