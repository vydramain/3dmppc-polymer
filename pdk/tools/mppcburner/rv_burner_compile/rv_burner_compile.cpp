
#include "rv_burner_compile.hpp"

#include "lua.hpp"
#include "rv_burner_print.hpp"

bool rv_pdktools::rv_burner_compile_script(const std::string &lua_path,
	const std::string &out_path,
	std::string &error)
{
	// TODO(2). Реализация в .cpp — четыре вызова LuaJIT:
	//
	//     luaL_newstate()                    временная VM, живёт только на время компиляции
	//     luaL_loadfile(L, lua_path)         текст -> функция на стеке (тут ловятся синтаксические ошибки)
	//     lua_dump(L, writer, &buffer)       функция -> байты
	//     lua_close(L)
	//
	//   lua_dump НЕ возвращает буфер. Он зовёт ТВОЙ callback (lua_Writer) столько
	//   раз, сколько ему удобно, и передаёт очередной кусок. Твой writer просто
	//   дописывает кусок в std::vector<unsigned char>. Сигнатура callback'а лежит
	//   в lua.h, строка 61 — прочитай её глазами, прежде чем писать.
	//
	//   Ошибку luaL_loadfile выводи как есть: LuaJIT уже написал в ней имя файла и
	//   номер строки, и это ровно то, что автор диска хочет увидеть.
	//

	lua_State *L = luaL_newstate();
	if (L == nullptr) {
		rv_pdktools::rv_burner_print_error("cannot initialize local lua vm");
		return false;
	}

	int loadf_r = luaL_loadfile(L, lua_path.c_str());
	if (loadf_r > 0) {
		rv_pdktools::rv_burner_print_error("cannot load lua file: '" + std::atos(loadf_r) + "'");
		return false;
	}

	size_t addr_sz;
	void *addr_p, addr_ud;
	lua_Writer lua_writer = lua_Writer(L, addr_p, addr_sz, addr_ud);

	int dmp = lua_dump(L, lua_writer, lua_buffer);

	lua_close(L);
};
