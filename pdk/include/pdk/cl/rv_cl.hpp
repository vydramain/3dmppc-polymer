#pragma once

#include <cstdint>

namespace rv_pdk
{

enum rv_cl_type : int {
	RV_CL_TYPE_NIL = 0,
	RV_CL_TYPE_BOOLEAN = 1,
	RV_CL_TYPE_NUMBER = 2,
	RV_CL_TYPE_STRING = 3,
	RV_CL_TYPE_FUNCTION = 4,
	RV_CL_TYPE_TABLE = 5,
	RV_CL_TYPE_OTHER = 6, // a value the contract has no vocabulary for
};

class rv_cl
{
public:
	virtual ~rv_cl() = default;

	virtual int64_t script_load(const void *bytecode, int64_t size, const char *name) = 0;
	virtual int64_t script_free(int64_t handle) = 0;

	virtual int64_t stack_push_nil() = 0;
	virtual int64_t stack_push_boolean(bool value) = 0;
	virtual int64_t stack_push_integer(int64_t value) = 0;
	virtual int64_t stack_push_number(double value) = 0;
	virtual int64_t stack_push_string(const char *text, int64_t length) = 0;
	virtual int64_t stack_drop(int64_t count) = 0;
	virtual int64_t stack_count() = 0;

	virtual int64_t value_type(int64_t index) = 0;
	virtual int64_t value_boolean(int64_t index, bool *out) = 0;
	virtual int64_t value_integer(int64_t index, int64_t *out) = 0;
	virtual int64_t value_number(int64_t index, double *out) = 0;

	// ┌───────────────────────────────────────┬──────────────────────────────────────────┐
	// │                 Вызов                 │                Что делает                │
	// ├───────────────────────────────────────┼──────────────────────────────────────────┤
	// │ value_string(idx, nullptr, 0)         │ ничего не пишет, возвращает полную длину │
	// ├───────────────────────────────────────┼──────────────────────────────────────────┤
	// │ value_string(idx, buf, N), вернул ≤ N │ влезло целиком                           │
	// ├───────────────────────────────────────┼──────────────────────────────────────────┤
	// │ value_string(idx, buf, N), вернул > N │ обрезано, вернулось нужное N             │
	// └───────────────────────────────────────┴──────────────────────────────────────────┘
	virtual int64_t value_string(int64_t index, char *baddr, int64_t baddr_size) = 0;

	virtual int64_t script_call(int64_t handle, const char *fname, int64_t argc, int64_t retc) = 0;
};

} // namespace rv_pdk
