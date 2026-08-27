#pragma once

#include <cstdint>

#include "pdk/de/rv_dv.hpp"

namespace rv_pdklib
{

// Полная запись ноты — то, что физически лежит в секции: заголовок (читатель
// накладывает на него Elf64_Nhdr), имя владельца, payload. Поля заголовка —
// uint32_t даже в ELF64 (elf(5)). Имя — встроенный массив, не указатель:
// в файл должны попасть сами символы, а не адрес.
struct rv_mppc_note {
	uint32_t namesz, descsz, type;
	char name[sizeof(RV_MPPC_NOTE_OWNER_DEF)];
	rv_pdk::rv_mppc_note_desc desc;
};

// Раскладка обязана совпасть с тем, что читает rv_pcloader::pre_dlopen_check:
// 12 байт заголовка + имя (кратно 4, паддинг внутри массива) + 16 байт payload,
// без дыр от выравнивания между полями.
static_assert(sizeof(RV_MPPC_NOTE_OWNER_DEF) % 4 == 0,
	"note owner length must be a multiple of 4; pad the string or fix the reader walk");
static_assert(sizeof(rv_mppc_note) ==
	12 + sizeof(RV_MPPC_NOTE_OWNER_DEF) + sizeof(rv_pdk::rv_mppc_note_desc));

} // namespace rv_pdklib

// Сажает запись ноты в диск. Вызывается один раз в translation unit, который
// генерирует burner, — исходники игры про версию не знают. Значения major/minor
// берутся из заголовков PDK, которыми идёт сборка диска: в ноту ложится
// «версия PDK, против которой диск был скомпилирован».
#define RV_MPPC_DISC_VERSION_DEF                                                    \
	__attribute__((section(RV_MPPC_SECTION_NAME_DEF), used, retain)) alignas(       \
		4) static const rv_pdklib::rv_mppc_note rv_mppc_disc_version_note = {       \
		sizeof(RV_MPPC_NOTE_OWNER_DEF),                                             \
		sizeof(rv_pdk::rv_mppc_note_desc),                                          \
		rv_pdk::RV_MPPC_NOTE_TYPE,                                                  \
		RV_MPPC_NOTE_OWNER_DEF,                                                     \
		{ RV_MPPC_NOTE_MAGIC_DEF, static_cast<uint32_t>(rv_pdk::RV_MPPC_VER_MAJOR), \
			static_cast<uint32_t>(rv_pdk::RV_MPPC_VER_MINOR) }                      \
	}
